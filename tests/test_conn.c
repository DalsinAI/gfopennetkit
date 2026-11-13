/* Connection tests over the mock transport. MIT licence, (c) 2026 AmiCradle Team. */
#include "gfopennetkit/netkit.h"
#include "mock_transport.h"

int main(void)
{
    Mock m;
    NKConn *c;
    NKConnConfig cfg = { 256, 100, 64, 5000 };
    const char *l;
    size_t n;

    {
        int lines = 0;
        memset(&m, 0, sizeof(m));
        mock_feed(&m, "200 server ready\r\n211 3 1 3 misc\r\n");
        m.chunk = 5;
        m.block_every = 3;
        c = nk_conn_new(&cfg, mock_transport(&m), 1000);
        CHECK(c != 0);
        while (m.in_pos < m.in_len) {
            CHECK_EQ(nk_conn_pump(c, 1000), NK_OK);
            while (nk_conn_line(c, &l, &n) == 1) {
                if (lines == 0) CHECK_STRN(l, n, "200 server ready");
                if (lines == 1) CHECK_STRN(l, n, "211 3 1 3 misc");
                lines++;
            }
        }
        CHECK_EQ(lines, 2);
    }

    /* output: CR LF added, CR/LF inside refused, partial writes finish */
    CHECK_EQ(nk_conn_sendline(c, "GROUP misc"), NK_OK);
    CHECK_EQ(nk_conn_sendline(c, "evil\r\nQUIT"), NK_EARG);
    CHECK_EQ(nk_conn_sendf(c, "ARTICLE %d", 3), NK_OK);
    while (nk_conn_outq(c))
        CHECK_EQ(nk_conn_pump(c, 1002), NK_OK);
    CHECK_STRN(m.out, m.out_len, "GROUP misc\r\nARTICLE 3\r\n");

    /* the output queue has a limit */
    {
        char big[70];
        memset(big, 'z', sizeof(big) - 1);
        big[sizeof(big) - 1] = 0;
        CHECK_EQ(nk_conn_sendline(c, big), NK_ENOMEM);
    }

    /* idle timeout from the caller's clock: nothing read since 1000 for 5 s */
    CHECK_EQ(nk_conn_pump(c, 1000 + 4999), NK_OK);
    CHECK_EQ(nk_conn_pump(c, 1000 + 5000), NK_ETIMEDOUT);
    /* the clock may wrap */
    nk_conn_free(c);
    memset(&m, 0, sizeof(m));
    c = nk_conn_new(&cfg, mock_transport(&m), 0xfffffff0u);
    CHECK_EQ(nk_conn_pump(c, 0x10), NK_OK);
    CHECK_EQ(nk_conn_pump(c, 0xfffffff0u + 5000), NK_ETIMEDOUT);

    /* cancel wins over everything */
    nk_conn_cancel(c);
    CHECK_EQ(nk_conn_pump(c, 0), NK_ECANCELLED);
    nk_conn_free(c);
    CHECK_EQ(m.closed, 1);

    /* EOF: lines before it still come out, then EOF sticks */
    memset(&m, 0, sizeof(m));
    mock_feed(&m, "205 bye\r\n");
    m.eof_at_end = 1;
    c = nk_conn_new(0, mock_transport(&m), 0);
    CHECK_EQ(nk_conn_pump(c, 0), NK_OK);
    CHECK_EQ(nk_conn_line(c, &l, &n), 1);
    CHECK_STRN(l, n, "205 bye");
    CHECK_EQ(nk_conn_pump(c, 0), NK_EOF);
    CHECK_EQ(nk_conn_pump(c, 0), NK_EOF);
    nk_conn_free(c);

    /* transport error */
    memset(&m, 0, sizeof(m));
    m.fail_read = NK_EIO;
    c = nk_conn_new(0, mock_transport(&m), 0);
    CHECK_EQ(nk_conn_pump(c, 0), NK_EIO);
    nk_conn_free(c);

    /* backoff: doubles to the cap, jitter stays in [step/2, step] */
    {
        NKBackoff b;
        uint32_t w, i;
        nk_backoff_init(&b, 1000, 60000);
        CHECK_EQ(nk_backoff_next(&b, 0), 500);
        CHECK_EQ(nk_backoff_next(&b, 0xffffffffu), 1000 + 0xffffffffu % 1001);
        for (i = 0; i < 20; i++) {
            w = nk_backoff_next(&b, i * 7919u);
            CHECK(w <= 60000);
        }
        CHECK_EQ(b.step_ms, 60000);
        w = nk_backoff_next(&b, 0);
        CHECK_EQ(w, 30000);
        nk_backoff_reset(&b);
        CHECK_EQ(nk_backoff_next(&b, 0), 500);
    }
    CHECK(strcmp(nk_strerror(NK_ETIMEDOUT), "timed out") == 0);
    TEST_DONE();
}

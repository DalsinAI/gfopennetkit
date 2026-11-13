/* SOCKS5 handshake tests. MIT licence, (c) 2026 AmiCradle Team. */
#include "gfopennetkit/netkit.h"
#include "mock_transport.h"

static int run(NKSocks5 *s, Mock *m)
{
    NKTransport t = mock_transport(m);
    int r, i;
    for (i = 0; i < 1000; i++)
        if ((r = nk_socks5_step(s, &t)) != NK_WOULDBLOCK)
            return r;
    return NK_WOULDBLOCK;
}

int main(void)
{
    NKSocks5 s;
    Mock m;
    static const char noauth[] =
        "\x05\x00"                              /* method: none */
        "\x05\x00\x00\x01\x7f\x00\x00\x01\x1f\x90"; /* ok, bound 127.0.0.1:8080 */
    static const char wantconn[] =
        "\x05\x01\x00"                          /* greeting */
        "\x05\x01\x00\x03\x10news.example.org\x02\x33"; /* CONNECT news.example.org:563 */

    /* no authentication, bytes arriving in ones with WOULDBLOCK between */
    memset(&m, 0, sizeof(m));
    m.in = noauth; m.in_len = sizeof(noauth) - 1;
    m.chunk = 1; m.block_every = 2;
    CHECK_EQ(nk_socks5_init(&s, "news.example.org", 563, 0, 0), NK_OK);
    CHECK_EQ(run(&s, &m), 1);
    CHECK_EQ(m.in_pos, m.in_len);           /* read exactly the reply, no more */
    CHECK_EQ(m.out_len, sizeof(wantconn) - 1);
    CHECK(memcmp(m.out, wantconn, m.out_len) == 0);

    /* user and password (RFC 1929) and a domain-name bound address */
    {
        static const char in[] =
            "\x05\x02" "\x01\x00"
            "\x05\x00\x00\x03\x05proxy\x04\x38";
        static const char want[] =
            "\x05\x02\x00\x02" "\x01\x04" "dale" "\x06" "secret"
            "\x05\x01\x00\x03\x07irc.net\x1a\x2b";
        memset(&m, 0, sizeof(m));
        m.in = in; m.in_len = sizeof(in) - 1;
        CHECK_EQ(nk_socks5_init(&s, "irc.net", 6699, "dale", "secret"), NK_OK);
        CHECK_EQ(run(&s, &m), 1);
        CHECK_EQ(m.out_len, sizeof(want) - 1);
        CHECK(memcmp(m.out, want, m.out_len) == 0);
        CHECK_EQ(m.in_pos, m.in_len);
        CHECK(s.pass[0] == 0);              /* wiped after use */
    }
    /* IPv6 bound address */
    {
        static const char in[] = "\x05\x00" "\x05\x00\x00\x04"
            "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\x01" "\x00\x50" "LEFT";
        memset(&m, 0, sizeof(m));
        m.in = in; m.in_len = sizeof(in) - 1;
        nk_socks5_init(&s, "h", 1, 0, 0);
        CHECK_EQ(run(&s, &m), 1);
        CHECK_EQ(m.in_len - m.in_pos, 4);   /* tunnel data left for the app */
    }
    /* refusals and protocol errors */
    memset(&m, 0, sizeof(m));
    mock_feed(&m, "\x05\xff");
    nk_socks5_init(&s, "h", 1, 0, 0);
    CHECK_EQ(run(&s, &m), NK_EREFUSED);
    {
        static const char in[] = "\x05\x02\x01\x01";
        memset(&m, 0, sizeof(m));
        m.in = in; m.in_len = 4;
        nk_socks5_init(&s, "h", 1, "u", "bad");
        CHECK_EQ(run(&s, &m), NK_EREFUSED);
    }
    {
        static const char in[] = "\x05\x00\x05\x05\x00\x01\x00\x00\x00\x00\x00\x00";  /* refused */
        memset(&m, 0, sizeof(m));
        m.in = in; m.in_len = 12;
        nk_socks5_init(&s, "h", 1, 0, 0);
        CHECK_EQ(run(&s, &m), NK_EREFUSED);
    }
    memset(&m, 0, sizeof(m));
    m.in = "\x04\x00"; m.in_len = 2;
    nk_socks5_init(&s, "h", 1, 0, 0);
    CHECK_EQ(run(&s, &m), NK_EPROTO);
    {
        static const char in[] = "\x05\x02";   /* asks for auth we didn't offer */
        memset(&m, 0, sizeof(m));
        m.in = in; m.in_len = 2;
        nk_socks5_init(&s, "h", 1, 0, 0);
        CHECK_EQ(run(&s, &m), NK_EPROTO);
    }
    memset(&m, 0, sizeof(m));
    mock_feed(&m, "\x05");
    m.eof_at_end = 1;
    nk_socks5_init(&s, "h", 1, 0, 0);
    CHECK_EQ(run(&s, &m), NK_EOF);
    /* arguments */
    CHECK_EQ(nk_socks5_init(&s, "", 1, 0, 0), NK_EARG);
    CHECK_EQ(nk_socks5_init(&s, "h", 0, 0, 0), NK_EARG);
    TEST_DONE();
}

/* TLS adapter tests against a fake TLS library. MIT licence, (c) 2026 AmiCradle Team. */
#include "gfopennetkit/netkit.h"
#include "mock_transport.h"

typedef struct {
    int hs_blocks;       /* handshakes answering WOULDBLOCK first */
    long hs_result;
    const char *data;
    size_t pos, len;
    long end;            /* read result after data: 0 clean, -4 cut */
    int notify, freed;
    char order[8];
    int n_order;
    Mock *tcp;
} FakeTls;

static long f_hs(void *p) { FakeTls *f = p; return f->hs_blocks-- > 0 ? NK_TLSERR_WOULDBLOCK : f->hs_result; }
static long f_read(void *p, void *b, long n)
{
    FakeTls *f = p;
    if (f->pos >= f->len) return f->end;
    if ((size_t)n > f->len - f->pos) n = (long)(f->len - f->pos);
    memcpy(b, f->data + f->pos, (size_t)n);
    f->pos += (size_t)n;
    return n;
}
static long f_write(void *p, const void *b, long n) { (void)p; (void)b; return n > 3 ? 3 : n; }
static long f_pending(void *p) { FakeTls *f = p; return (long)(f->len - f->pos); }
static long f_close(void *p) { FakeTls *f = p; f->notify++; f->order[f->n_order++] = 'n'; return 0; }
static void f_free(void *p) { FakeTls *f = p; f->freed++; f->order[f->n_order++] = 'f';
                              if (f->tcp->closed) f->order[f->n_order++] = 'X'; }
static const char *f_err(void *p) { (void)p; return "certificate is not for this host"; }

static const NKTlsOps ops = { f_hs, f_read, f_write, f_pending, f_close, f_free, f_err };

int main(void)
{
    Mock m;
    FakeTls f;
    NKTransport tcp, t;
    NKConn *c;
    const char *l, *txt;
    size_t n;

    memset(&m, 0, sizeof(m));
    memset(&f, 0, sizeof(f));
    f.tcp = &m;
    f.hs_blocks = 2;
    f.data = "200 news.example.org ready\r\n";
    f.len = strlen(f.data);
    f.end = NK_TLSERR_WOULDBLOCK;            /* the server stays open for now */
    tcp = mock_transport(&m);
    CHECK_EQ(nk_tls_wrap(&ops, &f, &tcp, &t), NK_OK);
    CHECK(tcp.ctx == 0);                     /* taken over */
    /* nothing moves before the handshake */
    {
        char b[4];
        CHECK_EQ(t.read(t.ctx, b, 4), NK_WOULDBLOCK);
        CHECK_EQ(t.pending(t.ctx), 0);
    }
    CHECK_EQ(nk_tls_handshake(&t), NK_WOULDBLOCK);
    CHECK_EQ(nk_tls_handshake(&t), NK_WOULDBLOCK);
    CHECK_EQ(nk_tls_handshake(&t), NK_OK);
    CHECK_EQ(nk_tls_handshake(&t), NK_OK);   /* idempotent */
    CHECK_EQ(t.pending(t.ctx), (long)f.len);

    c = nk_conn_new(0, t, 0);
    CHECK_EQ(nk_conn_pump(c, 0), NK_OK);
    CHECK_EQ(nk_conn_line(c, &l, &n), 1);
    CHECK_STRN(l, n, "200 news.example.org ready");
    CHECK_EQ(nk_conn_sendline(c, "QUIT"), NK_OK);
    CHECK_EQ(nk_conn_pump(c, 0), NK_OK);      /* short TLS writes are finished */
    CHECK_EQ(nk_conn_outq(c), 0);
    f.end = 0;
    CHECK_EQ(nk_conn_pump(c, 0), NK_EOF);     /* clean close_notify */
    CHECK_EQ(nk_tls_truncated(&t), 0);
    nk_conn_free(c);
    /* close order: close_notify, free the TLS connection, then the socket */
    CHECK_EQ(f.notify, 1);
    CHECK_EQ(f.freed, 1);
    CHECK_EQ(m.closed, 1);
    CHECK_STRN(f.order, (size_t)f.n_order, "nf");

    /* cut-off close: EOF, flagged, and no close_notify sent into a dead link */
    memset(&m, 0, sizeof(m));
    memset(&f, 0, sizeof(f));
    f.tcp = &m;
    f.end = NK_TLSERR_CLOSED;
    tcp = mock_transport(&m);
    nk_tls_wrap(&ops, &f, &tcp, &t);
    CHECK_EQ(nk_tls_handshake(&t), NK_OK);
    {
        char b[4];
        CHECK_EQ(t.read(t.ctx, b, 4), NK_EOF);
    }
    CHECK_EQ(nk_tls_truncated(&t), 1);
    CHECK_EQ(nk_tls_error(&t, 0), NK_TLSERR_CLOSED);
    nk_transport_close(&t);
    CHECK_EQ(f.notify, 0);
    CHECK_EQ(m.closed, 1);

    /* handshake failure keeps the library's code and words */
    memset(&m, 0, sizeof(m));
    memset(&f, 0, sizeof(f));
    f.tcp = &m;
    f.hs_result = -11;                        /* OTERR_HOSTNAME */
    tcp = mock_transport(&m);
    nk_tls_wrap(&ops, &f, &tcp, &t);
    CHECK_EQ(nk_tls_handshake(&t), NK_EPROTO);
    CHECK_EQ(nk_tls_error(&t, &txt), -11);
    CHECK(txt && strstr(txt, "not for this host"));
    nk_transport_close(&t);
    CHECK_EQ(f.notify, 0);                    /* never shook hands */
    CHECK_EQ(f.freed, 1);

    /* wrong arguments, and a plain transport isn't a TLS one */
    CHECK_EQ(nk_tls_wrap(0, &f, &tcp, &t), NK_EARG);
    tcp = mock_transport(&m);
    CHECK_EQ(nk_tls_handshake(&tcp), NK_EARG);
    CHECK_EQ(nk_tls_truncated(&tcp), 0);
    TEST_DONE();
}

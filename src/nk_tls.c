/* TLS transport adapter over NKTlsOps (gfopentls.library on the Amiga).
 * MIT licence, (c) 2026 AmiCradle Team. */
#include <stdlib.h>
#include "gfopennetkit/netkit.h"

typedef struct {
    uint32_t        magic;
    const NKTlsOps *ops;
    void           *tls;
    NKTransport     tcp;
    int             shaken;
    int             truncated;
    long            last;
} TlsCtx;

#define NK_TLS_MAGIC 0x4e4b544cu  /* "NKTL" */

static long map(TlsCtx *c, long r, int eof_ok)
{
    if (r >= 0)
        return r;
    if (r == NK_TLSERR_WOULDBLOCK)
        return NK_WOULDBLOCK;
    c->last = r;
    if (r == NK_TLSERR_CLOSED && eof_ok) {
        c->truncated = 1;
        return NK_EOF;
    }
    return NK_EIO;
}

static long t_read(void *ctx, void *buf, long len)
{
    TlsCtx *c = (TlsCtx *)ctx;
    long r;
    if (!c->shaken)
        return NK_WOULDBLOCK;
    r = c->ops->read(c->tls, buf, len);
    if (r == 0)
        return NK_EOF;        /* close_notify */
    return map(c, r, 1);
}

static long t_write(void *ctx, const void *buf, long len)
{
    TlsCtx *c = (TlsCtx *)ctx;
    long r;
    if (!c->shaken)
        return NK_WOULDBLOCK;
    r = c->ops->write(c->tls, buf, len);
    if (r == 0 && len > 0)
        return NK_WOULDBLOCK;
    return map(c, r, 0);
}

static long t_pending(void *ctx)
{
    TlsCtx *c = (TlsCtx *)ctx;
    long r = c->shaken && c->ops->pending ? c->ops->pending(c->tls) : 0;
    return r > 0 ? r : 0;
}

static void t_close(void *ctx)
{
    TlsCtx *c = (TlsCtx *)ctx;
    if (c->shaken && !c->truncated && c->ops->close_notify)
        c->ops->close_notify(c->tls);     /* best effort, one try */
    if (c->ops->free)
        c->ops->free(c->tls);
    nk_transport_close(&c->tcp);
    c->magic = 0;
    free(c);
}

static int t_fd(void *ctx)
{
    TlsCtx *c = (TlsCtx *)ctx;
    return c->tcp.fd ? c->tcp.fd(c->tcp.ctx) : -1;
}

static TlsCtx *ctx_of(NKTransport *t)
{
    TlsCtx *c = t && t->read == t_read ? (TlsCtx *)t->ctx : 0;
    return c && c->magic == NK_TLS_MAGIC ? c : 0;
}

int nk_tls_wrap(const NKTlsOps *ops, void *tls, NKTransport *tcp, NKTransport *out)
{
    TlsCtx *c;
    if (!ops || !ops->handshake || !ops->read || !ops->write || !tls || !tcp || !out)
        return NK_EARG;
    c = (TlsCtx *)calloc(1, sizeof(*c));
    if (!c)
        return NK_ENOMEM;
    c->magic = NK_TLS_MAGIC;
    c->ops = ops;
    c->tls = tls;
    c->tcp = *tcp;
    tcp->ctx = 0;
    tcp->close = 0;
    out->ctx = c;
    out->read = t_read;
    out->write = t_write;
    out->pending = t_pending;
    out->close = t_close;
    out->fd = t_fd;
    return NK_OK;
}

int nk_tls_handshake(NKTransport *t)
{
    TlsCtx *c = ctx_of(t);
    long r;
    if (!c)
        return NK_EARG;
    if (c->shaken)
        return NK_OK;
    r = c->ops->handshake(c->tls);
    if (r == NK_TLSERR_WOULDBLOCK)
        return NK_WOULDBLOCK;
    if (r < 0) {
        c->last = r;
        return NK_EPROTO;
    }
    c->shaken = 1;
    return NK_OK;
}

long nk_tls_error(NKTransport *t, const char **text)
{
    TlsCtx *c = ctx_of(t);
    if (text)
        *text = c && c->ops->errtext ? c->ops->errtext(c->tls) : 0;
    return c ? c->last : 0;
}

int nk_tls_truncated(NKTransport *t)
{
    TlsCtx *c = ctx_of(t);
    return c ? c->truncated : 0;
}

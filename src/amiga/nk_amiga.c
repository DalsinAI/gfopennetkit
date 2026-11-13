/* bsdsocket.library TCP transport and gfopentls.library TLS ops.
 * MIT licence, (c) 2026 AmiCradle Team.
 *
 * Built only for the Amiga (build-amiga.sh). The TLS behaviour itself lives
 * in src/nk_tls.c, which the host tests run against a fake TLS library. */
#include <exec/types.h>
#include <exec/memory.h>
#include <proto/exec.h>
#include <proto/socket.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <netdb.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <libraries/gfopentls.h>
#include <proto/gfopentls.h>
#include "gfopennetkit/netkit_amiga.h"

typedef struct {
    struct Library *base;
    LONG fd;
} AmiTcp;

/* proto/socket.h and proto/gfopentls.h call through these names */
#define SocketBase (c->base)

static long am_read(void *ctx, void *buf, long len)
{
    AmiTcp *c = (AmiTcp *)ctx;
    LONG n = recv(c->fd, buf, len, 0);
    if (n > 0)
        return n;
    if (n == 0)
        return NK_EOF;
    n = Errno();
    return (n == EWOULDBLOCK || n == EINTR) ? NK_WOULDBLOCK : NK_EIO;
}

static long am_write(void *ctx, const void *buf, long len)
{
    AmiTcp *c = (AmiTcp *)ctx;
    LONG n = send(c->fd, (APTR)buf, len, 0);
    if (n >= 0)
        return n;
    n = Errno();
    return (n == EWOULDBLOCK || n == EINTR) ? NK_WOULDBLOCK
         : n == EPIPE ? NK_EOF : NK_EIO;
}

static void am_close(void *ctx)
{
    AmiTcp *c = (AmiTcp *)ctx;
    CloseSocket(c->fd);
    free(c);
}

static int am_fd(void *ctx)
{
    return (int)((AmiTcp *)ctx)->fd;
}

int nk_amiga_tcp(struct Library *socketBase, const char *host, uint16_t port,
                 NKTransport *out)
{
    AmiTcp *c;
    struct hostent *he;
    struct sockaddr_in sa;
    uint32_t ip;
    LONG one = 1;
    if (!socketBase || !host || !out)
        return NK_EARG;
    c = (AmiTcp *)malloc(sizeof(*c));
    if (!c)
        return NK_ENOMEM;
    c->base = socketBase;
    memset(&sa, 0, sizeof(sa));
    sa.sin_len = sizeof(sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    /* A dotted address goes straight in: some stacks' gethostbyname()
     * fails on one when no resolver is set up. */
    if (nk_parse_ipv4(host, &ip)) {
        sa.sin_addr.s_addr = htonl(ip);
    } else {
        he = gethostbyname((STRPTR)host);
        if (!he || he->h_addrtype != AF_INET) {
            free(c);
            return NK_ENOTFOUND;
        }
        memcpy(&sa.sin_addr, he->h_addr, sizeof(sa.sin_addr));
    }
    c->fd = socket(AF_INET, SOCK_STREAM, 0);
    if (c->fd < 0) {
        free(c);
        return NK_ESOCKET;
    }
    if (connect(c->fd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        LONG e = Errno();
        CloseSocket(c->fd);
        free(c);
        return e == ETIMEDOUT ? NK_ETIMEDOUT : NK_EREFUSED;
    }
    IoctlSocket(c->fd, FIONBIO, (char *)&one);
    out->ctx = c;
    out->read = am_read;
    out->write = am_write;
    out->pending = 0;
    out->close = am_close;
    out->fd = am_fd;
    return NK_OK;
}

#undef SocketBase

/* ---- gfopentls ops ---------------------------------------------------- */
typedef struct {
    struct Library *base;
    struct OTConnection *conn;
} AmiTls;

#define OpenTLSBase (t->base)

static long tl_handshake(void *p) { AmiTls *t = p; return OT_Handshake(t->conn); }
static long tl_read(void *p, void *b, long n) { AmiTls *t = p; return OT_Read(t->conn, b, n); }
static long tl_write(void *p, const void *b, long n) { AmiTls *t = p; return OT_Write(t->conn, (CONST_APTR)b, n); }
static long tl_pending(void *p) { AmiTls *t = p; return OT_Pending(t->conn); }
static long tl_close(void *p) { AmiTls *t = p; return OT_Close(t->conn); }
static const char *tl_errtext(void *p) { AmiTls *t = p; return (const char *)OT_GetErrorText(t->conn); }
static void tl_free(void *p) { AmiTls *t = p; OT_FreeConnection(t->conn); free(t); }

static const NKTlsOps nk_ot_ops = {
    tl_handshake, tl_read, tl_write, tl_pending, tl_close, tl_free, tl_errtext
};

int nk_amiga_tls(struct Library *tlsBase, struct OTContext *ctx,
                 const char *host, long verify, const uint8_t *pin,
                 NKTransport *tcp, NKTransport *out)
{
    AmiTls *t;
    LONG err = 0;
    int r;
    if (!tlsBase || !ctx || !host || !tcp || tcp->read != am_read)
        return NK_EARG;
    t = (AmiTls *)malloc(sizeof(*t));
    if (!t)
        return NK_ENOMEM;
    t->base = tlsBase;
    t->conn = OT_NewConnection(ctx, (CONST_STRPTR)host, &err);
    if (!t->conn) {
        free(t);
        return NK_ENOMEM;
    }
    {
        AmiTcp *c = (AmiTcp *)tcp->ctx;
        OT_SetSocket(t->conn, c->fd, c->base);
    }
    if (verify >= 0)
        OT_SetVerify(t->conn, verify);
    if (pin)
        OT_PinCertificate(t->conn, (CONST UBYTE *)pin);
    r = nk_tls_wrap(&nk_ot_ops, t, tcp, out);
    if (r != NK_OK)
        tl_free(t);
    return r;
}

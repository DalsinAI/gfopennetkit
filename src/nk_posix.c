/* POSIX TCP transport, for the host tests and host tools only.
 * MIT licence, (c) 2026 AmiCradle Team. */
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include "gfopennetkit/netkit.h"

typedef struct { int fd; } PosixCtx;

static long px_read(void *ctx, void *buf, long len)
{
    ssize_t n = recv(((PosixCtx *)ctx)->fd, buf, (size_t)len, 0);
    if (n > 0)
        return (long)n;
    if (n == 0)
        return NK_EOF;
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
        return NK_WOULDBLOCK;
    return NK_EIO;
}

static long px_write(void *ctx, const void *buf, long len)
{
#ifdef MSG_NOSIGNAL
    ssize_t n = send(((PosixCtx *)ctx)->fd, buf, (size_t)len, MSG_NOSIGNAL);
#else
    ssize_t n = send(((PosixCtx *)ctx)->fd, buf, (size_t)len, 0);
#endif
    if (n >= 0)
        return (long)n;
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
        return NK_WOULDBLOCK;
    return errno == EPIPE ? NK_EOF : NK_EIO;
}

static void px_close(void *ctx)
{
    close(((PosixCtx *)ctx)->fd);
    free(ctx);
}

static int px_fd(void *ctx)
{
    return ((PosixCtx *)ctx)->fd;
}

/* what a failed connect() says, as a netkit code */
static int nk_connect_errno(int e)
{
    switch (e) {
    case ECONNREFUSED: return NK_EREFUSED;
    case ETIMEDOUT:    return NK_ETIMEDOUT;
    case ENETUNREACH:
    case EHOSTUNREACH: return NK_EUNREACH;
    default:           return NK_EIO;
    }
}

int nk_posix_tcp(const char *host, uint16_t port, NKTransport *out)
{
    struct addrinfo hints, *res = 0, *ai;
    char ps[8];
    int fd = -1;
    PosixCtx *ctx;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    snprintf(ps, sizeof(ps), "%u", (unsigned)port);
    int err = NK_ESOCKET;
    if (getaddrinfo(host, ps, &hints, &res) != 0)
        return NK_ERESOLVE;
    for (ai = res; ai; ai = ai->ai_next) {
        fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0)
            continue;
        if (connect(fd, ai->ai_addr, ai->ai_addrlen) == 0)
            break;
        err = nk_connect_errno(errno);
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    if (fd < 0)
        return err;
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
    ctx = (PosixCtx *)malloc(sizeof(*ctx));
    if (!ctx) {
        close(fd);
        return NK_ENOMEM;
    }
    ctx->fd = fd;
    out->ctx = ctx;
    out->read = px_read;
    out->write = px_write;
    out->pending = 0;
    out->close = px_close;
    out->fd = px_fd;
    return NK_OK;
}

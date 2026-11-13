/* Connection: transport + line reader + output queue + timeout + cancel.
 * MIT licence, (c) 2026 AmiCradle Team. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gfopennetkit/netkit.h"

struct NKConn {
    NKTransport  t;
    NKLineReader lr;
    char        *in;
    char        *out;
    size_t       out_cap, out_len, out_pos;
    uint32_t     idle_ms, last_rx;
    volatile int cancelled;
    int          dead;          /* sticky result after EOF/EIO */
};

NKConn *nk_conn_new(const NKConnConfig *cfg, NKTransport t, uint32_t now_ms)
{
    NKConnConfig d = { 16384, 8190, 65536, 0 };
    NKConn *c;
    if (cfg) {
        if (cfg->in_cap)   d.in_cap = cfg->in_cap;
        if (cfg->max_line) d.max_line = cfg->max_line;
        if (cfg->out_cap)  d.out_cap = cfg->out_cap;
        d.idle_timeout_ms = cfg->idle_timeout_ms;
    }
    c = (NKConn *)calloc(1, sizeof(*c));
    if (!c)
        return 0;
    c->in = (char *)malloc(d.in_cap);
    c->out = (char *)malloc(d.out_cap);
    if (!c->in || !c->out) {
        free(c->in);
        free(c->out);
        free(c);
        return 0;
    }
    c->t = t;
    nk_lr_init(&c->lr, c->in, d.in_cap, d.max_line);
    c->out_cap = d.out_cap;
    c->idle_ms = d.idle_timeout_ms;
    c->last_rx = now_ms;
    return c;
}

void nk_conn_free(NKConn *c)
{
    if (!c)
        return;
    nk_transport_close(&c->t);
    free(c->in);
    free(c->out);
    free(c);
}

int nk_conn_send(NKConn *c, const void *data, size_t len)
{
    if (c->out_pos && c->out_len + len > c->out_cap) {
        memmove(c->out, c->out + c->out_pos, c->out_len - c->out_pos);
        c->out_len -= c->out_pos;
        c->out_pos = 0;
    }
    if (c->out_len + len > c->out_cap)
        return NK_ENOMEM;
    memcpy(c->out + c->out_len, data, len);
    c->out_len += len;
    return NK_OK;
}

int nk_conn_sendline(NKConn *c, const char *line)
{
    size_t n = strlen(line);
    if (memchr(line, '\r', n) || memchr(line, '\n', n))
        return NK_EARG;
    if (c->out_len - c->out_pos + n + 2 > c->out_cap)
        return NK_ENOMEM;
    nk_conn_send(c, line, n);
    return nk_conn_send(c, "\r\n", 2);
}

int nk_conn_sendf(NKConn *c, const char *fmt, ...)
{
    char small[1024], *p = small;
    int n, r;
    va_list ap;
    va_start(ap, fmt);
    n = vsnprintf(small, sizeof(small), fmt, ap);
    va_end(ap);
    if (n < 0)
        return NK_EARG;
    if ((size_t)n >= sizeof(small)) {
        p = (char *)malloc((size_t)n + 1);
        if (!p)
            return NK_ENOMEM;
        va_start(ap, fmt);
        vsnprintf(p, (size_t)n + 1, fmt, ap);
        va_end(ap);
    }
    r = nk_conn_sendline(c, p);
    if (p != small)
        free(p);
    return r;
}

int nk_conn_pump(NKConn *c, uint32_t now_ms)
{
    long n;
    int got = 0;
    if (c->cancelled)
        return NK_ECANCELLED;
    if (c->dead)
        return c->dead;
    /* write what the transport takes */
    while (c->out_pos < c->out_len) {
        n = c->t.write(c->t.ctx, c->out + c->out_pos, (long)(c->out_len - c->out_pos));
        if (n == NK_WOULDBLOCK)
            break;
        if (n <= 0)
            return c->dead = (n == 0 ? NK_EOF : (int)n);
        c->out_pos += (size_t)n;
    }
    if (c->out_pos == c->out_len)
        c->out_pos = c->out_len = 0;
    /* read until the transport has nothing or the buffer is full */
    for (;;) {
        size_t room;
        char *dst = nk_lr_space(&c->lr, &room);
        if (!room)
            break;
        n = c->t.read(c->t.ctx, dst, (long)room);
        if (n == NK_WOULDBLOCK)
            break;
        if (n <= 0) {
            c->dead = (n == 0 ? NK_EOF : (int)n);
            /* lines already held stay readable; report the end after */
            return got || nk_lr_buffered(&c->lr) ? NK_OK : c->dead;
        }
        nk_lr_commit(&c->lr, (size_t)n);
        got = 1;
        if (c->cancelled)
            return NK_ECANCELLED;
    }
    if (got)
        c->last_rx = now_ms;
    else if (c->idle_ms && (uint32_t)(now_ms - c->last_rx) >= c->idle_ms)
        return NK_ETIMEDOUT;
    return NK_OK;
}

int nk_conn_line(NKConn *c, const char **line, size_t *len)
{
    return nk_lr_next(&c->lr, line, len);
}

size_t nk_conn_outq(const NKConn *c)
{
    return c->out_len - c->out_pos;
}

int nk_conn_fd(const NKConn *c)
{
    return c->t.fd ? c->t.fd(c->t.ctx) : -1;
}

NKLineReader *nk_conn_reader(NKConn *c)
{
    return &c->lr;
}

void nk_conn_cancel(NKConn *c)
{
    c->cancelled = 1;
}

/* A scripted in-memory transport for the unit tests.
 * MIT licence, (c) 2026 AmiCradle Team. */
#ifndef NK_MOCK_TRANSPORT_H
#define NK_MOCK_TRANSPORT_H
#include "gfopennetkit/netkit.h"
#include "nk_test.h"
#if defined(__GNUC__)
#define NK_UNUSED __attribute__((unused))
#else
#define NK_UNUSED
#endif

typedef struct Mock {
    const char *in;       /* bytes the peer sends */
    size_t in_len, in_pos;
    size_t chunk;         /* most bytes per read/write call (0 = any) */
    int block_every;      /* every Nth call answers WOULDBLOCK (0 = never) */
    int calls;
    int eof_at_end;       /* EOF after `in`, else WOULDBLOCK */
    long fail_read;       /* answer this from read once `in` is used up */
    char out[8192];
    size_t out_len, out_cap;
    int closed;
} Mock;

static NK_UNUSED long mock_read(void *ctx, void *buf, long len)
{
    Mock *m = (Mock *)ctx;
    size_t n;
    m->calls++;
    if (m->block_every && m->calls % m->block_every == 0)
        return NK_WOULDBLOCK;
    if (m->in_pos >= m->in_len)
        return m->fail_read ? m->fail_read : m->eof_at_end ? NK_EOF : NK_WOULDBLOCK;
    n = m->in_len - m->in_pos;
    if ((size_t)len < n) n = (size_t)len;
    if (m->chunk && m->chunk < n) n = m->chunk;
    memcpy(buf, m->in + m->in_pos, n);
    m->in_pos += n;
    return (long)n;
}

static NK_UNUSED long mock_write(void *ctx, const void *buf, long len)
{
    Mock *m = (Mock *)ctx;
    size_t n = (size_t)len, cap = m->out_cap ? m->out_cap : sizeof(m->out);
    m->calls++;
    if (m->block_every && m->calls % m->block_every == 0)
        return NK_WOULDBLOCK;
    if (m->chunk && m->chunk < n) n = m->chunk;
    if (m->out_len + n > cap) n = cap - m->out_len;
    if (!n) return NK_WOULDBLOCK;
    memcpy(m->out + m->out_len, buf, n);
    m->out_len += n;
    return (long)n;
}

static NK_UNUSED void mock_close(void *ctx) { ((Mock *)ctx)->closed++; }

static NK_UNUSED NKTransport mock_transport(Mock *m)
{
    NKTransport t;
    t.ctx = m;
    t.read = mock_read;
    t.write = mock_write;
    t.pending = 0;
    t.close = mock_close;
    t.fd = 0;
    return t;
}

static NK_UNUSED void mock_feed(Mock *m, const char *s)
{
    m->in = s;
    m->in_len = strlen(s);
    m->in_pos = 0;
}
#endif

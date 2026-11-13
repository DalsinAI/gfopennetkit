/* Line reader: LF or CR LF lines with a size cap.
 * MIT licence, (c) 2026 AmiCradle Team. */
#include <string.h>
#include "gfopennetkit/netkit.h"

void nk_lr_init(NKLineReader *lr, char *buf, size_t cap, size_t max_line)
{
    lr->buf = buf;
    lr->cap = cap;
    lr->start = lr->len = lr->scan = 0;
    /* a full line plus CR LF has to fit in the buffer */
    if (max_line == 0 || max_line + 2 > cap)
        max_line = cap > 2 ? cap - 2 : 0;
    lr->max_line = max_line;
    lr->skipping = 0;
}

char *nk_lr_space(NKLineReader *lr, size_t *room)
{
    if (lr->start && (lr->start + lr->len == lr->cap || lr->len == 0)) {
        memmove(lr->buf, lr->buf + lr->start, lr->len);
        lr->start = 0;
    }
    *room = lr->cap - lr->start - lr->len;
    return lr->buf + lr->start + lr->len;
}

void nk_lr_commit(NKLineReader *lr, size_t n)
{
    lr->len += n;
}

int nk_lr_next(NKLineReader *lr, const char **line, size_t *len)
{
    for (;;) {
        char *base = lr->buf + lr->start;
        char *lf = lr->scan < lr->len
                 ? (char *)memchr(base + lr->scan, '\n', lr->len - lr->scan) : 0;
        if (!lf) {
            lr->scan = lr->len;
            if (lr->skipping) {
                /* throw away the middle of an over-long line */
                lr->start = lr->len = lr->scan = 0;
                return 0;
            }
            if (lr->len > lr->max_line + 1) {
                /* no LF within the cap (+1 for a CR still to come) */
                lr->skipping = 1;
                lr->start = lr->len = lr->scan = 0;
                return NK_ETOOLONG;
            }
            return 0;
        } else {
            size_t n = (size_t)(lf - base), l = n;
            int was_skipping = lr->skipping;
            if (l && base[l - 1] == '\r')
                l--;
            lr->start += n + 1;
            lr->len -= n + 1;
            lr->scan = 0;
            lr->skipping = 0;
            if (lr->len == 0)
                lr->start = 0;
            if (was_skipping)
                continue;           /* the tail of the dropped line */
            if (l > lr->max_line)
                return NK_ETOOLONG;
            *line = base;
            *len = l;
            return 1;
        }
    }
}

size_t nk_lr_buffered(const NKLineReader *lr)
{
    return lr->len;
}

size_t nk_lr_take(NKLineReader *lr, void *out, size_t max)
{
    size_t n = lr->len < max ? lr->len : max;
    memcpy(out, lr->buf + lr->start, n);
    lr->start += n;
    lr->len -= n;
    lr->scan = 0;
    if (lr->len == 0)
        lr->start = 0;
    return n;
}

/* Error strings, transport and backoff helpers.
 * MIT licence, (c) 2026 AmiCradle Team. */
#include "gfopennetkit/netkit.h"

const char *nk_strerror(int err)
{
    switch (err) {
    case NK_OK:         return "ok";
    case NK_WOULDBLOCK: return "would block";
    case NK_EOF:        return "connection closed by peer";
    case NK_EIO:        return "input/output error";
    case NK_ENOMEM:     return "out of memory";
    case NK_ETOOLONG:   return "line too long";
    case NK_EPROTO:     return "protocol error";
    case NK_ETIMEDOUT:  return "timed out";
    case NK_EARG:       return "bad argument";
    case NK_ECANCELLED: return "cancelled";
    case NK_EREFUSED:   return "refused";
    case NK_ECORRUPT:   return "damaged record";
    case NK_ERESOLVE:   return "host name not found";
    case NK_EUNREACH:   return "host unreachable";
    case NK_ESOCKET:    return "no socket (is the TCP/IP stack running?)";
    default:            return "unknown error";
    }
}

int nk_parse_ipv4(const char *s, uint8_t out[4])
{
    int part;
    for (part = 0; part < 4; part++) {
        unsigned v = 0, d = 0;
        while (*s >= '0' && *s <= '9' && d < 4) {
            v = v * 10 + (unsigned)(*s++ - '0');
            d++;
        }
        if (!d || d > 3 || v > 255)
            return 0;
        out[part] = (uint8_t)v;
        if (part < 3 && *s++ != '.')
            return 0;
    }
    return *s == 0;
}

void nk_transport_close(NKTransport *t)
{
    if (t && t->close)
        t->close(t->ctx);
    if (t) {
        t->ctx = 0;
        t->close = 0;
    }
}

void nk_backoff_init(NKBackoff *b, uint32_t base_ms, uint32_t max_ms)
{
    b->base_ms = base_ms ? base_ms : 1000;
    b->max_ms = max_ms < b->base_ms ? b->base_ms : max_ms;
    b->step_ms = b->base_ms;
    b->attempts = 0;
}

uint32_t nk_backoff_next(NKBackoff *b, uint32_t rnd)
{
    uint32_t step = b->step_ms, half = step / 2;
    uint32_t wait = half + (half ? rnd % (step - half + 1) : 0);
    b->attempts++;
    b->step_ms = step >= b->max_ms / 2 ? b->max_ms : step * 2;
    return wait;
}

void nk_backoff_reset(NKBackoff *b)
{
    b->step_ms = b->base_ms;
    b->attempts = 0;
}

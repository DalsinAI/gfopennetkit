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
    default:            return "unknown error";
    }
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

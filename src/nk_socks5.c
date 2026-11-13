/* SOCKS5 client handshake (RFC 1928, RFC 1929), non-blocking.
 * MIT licence, (c) 2026 AmiCradle Team. */
#include <string.h>
#include "gfopennetkit/netkit.h"

enum { S_GREET, S_METHOD, S_AUTH, S_AUTHREPLY, S_CONNECT, S_REPLY, S_REPLYADDR, S_DONE };

int nk_socks5_init(NKSocks5 *s, const char *host, uint16_t port,
                   const char *user, const char *pass)
{
    size_t hl = host ? strlen(host) : 0;
    memset(s, 0, sizeof(*s));
    if (!hl || hl > 255 || !port)
        return NK_EARG;
    if ((user && strlen(user) > 255) || (pass && strlen(pass) > 255))
        return NK_EARG;
    memcpy(s->host, host, hl);
    s->port = port;
    if (user && *user) {
        strcpy(s->user, user);
        strcpy(s->pass, pass ? pass : "");
    }
    s->state = S_GREET;
    return NK_OK;
}

static void put_out(NKSocks5 *s, size_t len)
{
    s->out_len = len;
    s->sent = 0;
}

/* NK_OK when all queued bytes went, else NK_WOULDBLOCK or an error */
static int flush_out(NKSocks5 *s, NKTransport *t)
{
    while (s->sent < s->out_len) {
        long n = t->write(t->ctx, s->buf + s->sent, (long)(s->out_len - s->sent));
        if (n == NK_WOULDBLOCK)
            return NK_WOULDBLOCK;
        if (n <= 0)
            return n == 0 ? NK_EOF : (int)n;
        s->sent += (size_t)n;
    }
    return NK_OK;
}

/* NK_OK when s->want bytes are in s->buf */
static int fill_in(NKSocks5 *s, NKTransport *t)
{
    while (s->have < s->want) {
        long n = t->read(t->ctx, s->buf + s->have, (long)(s->want - s->have));
        if (n == NK_WOULDBLOCK)
            return NK_WOULDBLOCK;
        if (n <= 0)
            return n == 0 ? NK_EOF : (int)n;
        s->have += (size_t)n;
    }
    return NK_OK;
}

static void expect(NKSocks5 *s, size_t n)
{
    s->have = 0;
    s->want = n;
}

int nk_socks5_step(NKSocks5 *s, NKTransport *t)
{
    int r;
    for (;;) {
        switch (s->state) {
        case S_GREET:
            s->buf[0] = 5;
            if (s->user[0]) {
                s->buf[1] = 2; s->buf[2] = 0; s->buf[3] = 2;
                put_out(s, 4);
            } else {
                s->buf[1] = 1; s->buf[2] = 0;
                put_out(s, 3);
            }
            s->state = S_METHOD;
            expect(s, 0);
            /* fall through */
        case S_METHOD:
            if ((r = flush_out(s, t)) != NK_OK)
                return r;
            if (!s->want)
                expect(s, 2);
            if ((r = fill_in(s, t)) != NK_OK)
                return r;
            if (s->buf[0] != 5)
                return NK_EPROTO;
            if (s->buf[1] == 0xff)
                return NK_EREFUSED;
            if (s->buf[1] == 2 && s->user[0]) {
                size_t ul = strlen(s->user), pl = strlen(s->pass);
                s->buf[0] = 1;
                s->buf[1] = (uint8_t)ul;
                memcpy(s->buf + 2, s->user, ul);
                s->buf[2 + ul] = (uint8_t)pl;
                memcpy(s->buf + 3 + ul, s->pass, pl);
                put_out(s, 3 + ul + pl);
                expect(s, 0);
                s->state = S_AUTH;
            } else if (s->buf[1] == 0) {
                s->state = S_CONNECT;
                s->out_len = 0;
            } else
                return NK_EPROTO;
            break;
        case S_AUTH:
        case S_AUTHREPLY:
            if ((r = flush_out(s, t)) != NK_OK)
                return r;
            if (!s->want)
                expect(s, 2);
            if ((r = fill_in(s, t)) != NK_OK)
                return r;
            if (s->buf[1] != 0)
                return NK_EREFUSED;
            s->state = S_CONNECT;
            s->out_len = 0;
            break;
        case S_CONNECT:
            if (!s->out_len) {
                size_t hl = strlen(s->host);
                s->buf[0] = 5; s->buf[1] = 1; s->buf[2] = 0; s->buf[3] = 3;
                s->buf[4] = (uint8_t)hl;
                memcpy(s->buf + 5, s->host, hl);
                s->buf[5 + hl] = (uint8_t)(s->port >> 8);
                s->buf[6 + hl] = (uint8_t)s->port;
                put_out(s, 7 + hl);
                expect(s, 0);
            }
            if ((r = flush_out(s, t)) != NK_OK)
                return r;
            s->state = S_REPLY;
            expect(s, 5);   /* VER REP RSV ATYP + first address byte */
            /* fall through */
        case S_REPLY:
            if ((r = fill_in(s, t)) != NK_OK)
                return r;
            if (s->buf[0] != 5 || s->buf[2] != 0)
                return NK_EPROTO;
            if (s->buf[1] != 0)
                return NK_EREFUSED;
            switch (s->buf[3]) {
            case 1:  s->want = 4 + 4 + 2; break;
            case 3:  s->want = 4 + 1 + s->buf[4] + 2; break;
            case 4:  s->want = 4 + 16 + 2; break;
            default: return NK_EPROTO;
            }
            s->state = S_REPLYADDR;
            /* fall through */
        case S_REPLYADDR:
            if ((r = fill_in(s, t)) != NK_OK)
                return r;
            s->state = S_DONE;
            /* wipe the password from memory once used */
            memset(s->pass, 0, sizeof(s->pass));
            memset(s->buf, 0, sizeof(s->buf));
            /* fall through */
        case S_DONE:
            return 1;
        default:
            return NK_EARG;
        }
    }
}

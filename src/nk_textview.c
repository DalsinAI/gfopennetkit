/* Text view model: scrollback ring, IRC formatting, news quotes, links,
 * wrapping, search and copy. MIT licence, (c) 2026 AmiCradle Team. */
#include <stdlib.h>
#include <string.h>
#include "gfopennetkit/textview.h"

typedef struct TvLine {
    NKTvLine pub;
    size_t   bytes;        /* what this line costs against max_bytes */
} TvLine;

struct NKTextView {
    TvLine **ring;
    uint32_t cap, head, count;   /* head = index of the oldest */
    size_t   bytes, max_bytes;
    uint32_t next_seq, marker;
    int      in_sig;
};

NKTextView *nk_tv_new(uint32_t max_lines, size_t max_bytes)
{
    NKTextView *tv;
    if (!max_lines)
        return 0;
    tv = (NKTextView *)calloc(1, sizeof(*tv));
    if (!tv)
        return 0;
    tv->ring = (TvLine **)calloc(max_lines, sizeof(*tv->ring));
    if (!tv->ring) {
        free(tv);
        return 0;
    }
    tv->cap = max_lines;
    tv->max_bytes = max_bytes ? max_bytes : (size_t)-1;
    tv->next_seq = 1;
    return tv;
}

static void drop_oldest(NKTextView *tv)
{
    TvLine *l = tv->ring[tv->head];
    tv->bytes -= l->bytes;
    free(l);
    tv->ring[tv->head] = 0;
    tv->head = (tv->head + 1) % tv->cap;
    tv->count--;
}

void nk_tv_clear(NKTextView *tv)
{
    while (tv->count)
        drop_oldest(tv);
    tv->head = 0;
    tv->in_sig = 0;
    tv->marker = 0;
}

void nk_tv_free(NKTextView *tv)
{
    if (!tv)
        return;
    nk_tv_clear(tv);
    free(tv->ring);
    free(tv);
}

/* ---- parsing ----------------------------------------------------------- */
typedef struct {
    NKTvRun *runs;
    uint32_t n, cap;
} RunList;

static int push_run(RunList *rl, uint32_t start, uint32_t len, int32_t fg, int32_t bg, uint8_t flags)
{
    if (!len)
        return 1;
    if (rl->n && rl->runs[rl->n - 1].start + rl->runs[rl->n - 1].len == start &&
        rl->runs[rl->n - 1].fg == fg && rl->runs[rl->n - 1].bg == bg &&
        rl->runs[rl->n - 1].flags == flags) {
        rl->runs[rl->n - 1].len += len;
        return 1;
    }
    if (rl->n == rl->cap) {
        uint32_t nc = rl->cap ? rl->cap * 2 : 8;
        NKTvRun *nr = (NKTvRun *)realloc(rl->runs, nc * sizeof(*nr));
        if (!nr)
            return 0;
        rl->runs = nr;
        rl->cap = nc;
    }
    rl->runs[rl->n].start = start;
    rl->runs[rl->n].len = len;
    rl->runs[rl->n].fg = fg;
    rl->runs[rl->n].bg = bg;
    rl->runs[rl->n].flags = flags;
    rl->n++;
    return 1;
}

static int isdig(int c) { return c >= '0' && c <= '9'; }
static int hexv(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int hex6(const char *s, size_t n, int32_t *rgb)
{
    int32_t v = 0;
    size_t i;
    if (n < 6)
        return 0;
    for (i = 0; i < 6; i++) {
        int h = hexv((unsigned char)s[i]);
        if (h < 0)
            return 0;
        v = v << 4 | h;
    }
    *rgb = 0x1000000 | v;
    return 1;
}

/* Takes formatting codes out of s into plain; style runs into rl. */
static int parse_mirc(const char *s, size_t n, char *plain, uint32_t *plen, RunList *rl)
{
    int32_t fg = NK_TV_DEFAULT, bg = NK_TV_DEFAULT;
    uint8_t fl = 0;
    uint32_t out = 0, seg = 0;
    size_t i = 0;
#define FLUSH() do { if (!push_run(rl, seg, out - seg, fg, bg, fl)) return 0; seg = out; } while (0)
    while (i < n) {
        unsigned char c = (unsigned char)s[i];
        switch (c) {
        case 0x02: FLUSH(); fl ^= NK_TV_BOLD; i++; break;
        case 0x1d: FLUSH(); fl ^= NK_TV_ITALIC; i++; break;
        case 0x1f: FLUSH(); fl ^= NK_TV_UNDERLINE; i++; break;
        case 0x1e: FLUSH(); fl ^= NK_TV_STRIKE; i++; break;
        case 0x11: FLUSH(); fl ^= NK_TV_MONO; i++; break;
        case 0x16: FLUSH(); fl ^= NK_TV_REVERSE; i++; break;
        case 0x0f: FLUSH(); fl = 0; fg = bg = NK_TV_DEFAULT; i++; break;
        case 0x03:
            FLUSH();
            i++;
            if (i < n && isdig((unsigned char)s[i])) {
                int v = s[i++] - '0';
                if (i < n && isdig((unsigned char)s[i]))
                    v = v * 10 + (s[i++] - '0');
                fg = v == 99 ? NK_TV_DEFAULT : v;
                if (i + 1 < n && s[i] == ',' && isdig((unsigned char)s[i + 1])) {
                    i++;
                    v = s[i++] - '0';
                    if (i < n && isdig((unsigned char)s[i]))
                        v = v * 10 + (s[i++] - '0');
                    bg = v == 99 ? NK_TV_DEFAULT : v;
                }
            } else
                fg = bg = NK_TV_DEFAULT;
            break;
        case 0x04: {
            int32_t rgb;
            FLUSH();
            i++;
            if (hex6(s + i, n - i, &rgb)) {
                fg = rgb;
                i += 6;
                if (i < n && s[i] == ',' && hex6(s + i + 1, n - i - 1, &rgb)) {
                    bg = rgb;
                    i += 7;
                }
            } else
                fg = bg = NK_TV_DEFAULT;
            break;
        }
        default:
            plain[out++] = (char)c;
            i++;
        }
    }
    FLUSH();
#undef FLUSH
    *plen = out;
    return 1;
}

/* ---- links --------------------------------------------------------------- */
static int lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }

static int prefix_at(const char *t, size_t len, size_t i, const char *p)
{
    size_t k;
    for (k = 0; p[k]; k++)
        if (i + k >= len || lower((unsigned char)t[i + k]) != p[k])
            return 0;
    return (int)k;
}

static int url_char(int c)
{
    return c > ' ' && c != '<' && c != '>' && c != '"' && c != 0x7f && c != '`';
}

int nk_tv_find_url(const char *text, size_t len, size_t from, size_t *start, size_t *ulen)
{
    static const char *const schemes[] = {
        "https://", "http://", "ftp://", "nntp://", "ircs://", "irc://",
        "news:", "mailto:", "www.", 0
    };
    size_t i, e;
    int k, p;
    for (i = from; i < len; i++) {
        /* a URL starts a word, or follows an opening bracket or quote */
        if (i > 0) {
            int b = (unsigned char)text[i - 1];
            if (b != ' ' && b != '\t' && b != '(' && b != '<' && b != '[' &&
                b != '"' && b != '\'')
                continue;
        }
        for (k = 0; schemes[k]; k++)
            if ((p = prefix_at(text, len, i, schemes[k])) != 0)
                break;
        if (!schemes[k])
            continue;
        e = i + (size_t)p;
        while (e < len && url_char((unsigned char)text[e]))
            e++;
        /* leave out trailing punctuation and an unmatched ')' */
        for (;;) {
            int c;
            if (e <= i + (size_t)p)
                break;
            c = (unsigned char)text[e - 1];
            if (c == '.' || c == ',' || c == ';' || c == ':' || c == '!' ||
                c == '?' || c == '\'' || c == ']') {
                e--;
                continue;
            }
            if (c == ')') {
                size_t q;
                int open = 0, close = 0;
                for (q = i; q < e; q++) {
                    if (text[q] == '(') open++;
                    if (text[q] == ')') close++;
                }
                if (close > open) {
                    e--;
                    continue;
                }
            }
            break;
        }
        if (e == i + (size_t)p)
            continue;           /* a scheme with nothing after it */
        *start = i;
        *ulen = e - i;
        return 1;
    }
    return 0;
}

/* Splits the style runs at link edges and marks the link parts. */
static int mark_links(const char *t, uint32_t len, RunList *rl)
{
    size_t s, ul, from = 0;
    RunList out = { 0, 0, 0 };
    uint32_t r;
    int any = 0;
    /* link spans first */
    uint32_t ls[32], le[32], nl = 0;
    while (nl < 32 && nk_tv_find_url(t, len, from, &s, &ul)) {
        ls[nl] = (uint32_t)s;
        le[nl] = (uint32_t)(s + ul);
        nl++;
        from = s + ul;
        any = 1;
    }
    if (!any)
        return 1;
    for (r = 0; r < rl->n; r++) {
        NKTvRun x = rl->runs[r];
        uint32_t pos = x.start, end = x.start + x.len, k;
        while (pos < end) {
            uint32_t cut = end;
            uint8_t fl = x.flags;
            for (k = 0; k < nl; k++) {
                if (pos >= ls[k] && pos < le[k]) {
                    fl |= NK_TV_LINK;
                    if (le[k] < cut) cut = le[k];
                    break;
                }
                if (ls[k] > pos && ls[k] < cut)
                    cut = ls[k];
            }
            if (!push_run(&out, pos, cut - pos, x.fg, x.bg, fl)) {
                free(out.runs);
                return 0;
            }
            pos = cut;
        }
    }
    free(rl->runs);
    *rl = out;
    return 1;
}

static uint8_t quote_level(const char *t, uint32_t len)
{
    uint32_t i = 0;
    unsigned q = 0;
    while (i < len && (t[i] == '>' || (q && t[i] == ' '))) {
        if (t[i] == '>')
            q++;
        i++;
    }
    return (uint8_t)(q > 255 ? 255 : q);
}

uint32_t nk_tv_add(NKTextView *tv, const char *text, size_t len, int mode, uint8_t kind)
{
    char *plain;
    uint32_t plen = 0, i;
    RunList rl = { 0, 0, 0 };
    TvLine *l;
    size_t sz;
    uint8_t dim = 0, quote = 0;

    while (len && (text[len - 1] == '\n' || text[len - 1] == '\r'))
        len--;
    if (len > 0xfffffff0u)
        return 0;
    plain = (char *)malloc(len + 1);
    if (!plain)
        return 0;
    if (mode == NK_TV_MIRC) {
        if (!parse_mirc(text, len, plain, &plen, &rl))
            goto oom;
    } else {
        memcpy(plain, text, len);
        plen = (uint32_t)len;
        if (mode == NK_TV_NEWS) {
            if ((plen == 3 && memcmp(plain, "-- ", 3) == 0) ||
                (plen == 2 && memcmp(plain, "--", 2) == 0))
                tv->in_sig = 1;
            dim = tv->in_sig ? NK_TV_DIM : 0;
            quote = quote_level(plain, plen);
        }
        if (!push_run(&rl, 0, plen, NK_TV_DEFAULT, NK_TV_DEFAULT, dim))
            goto oom;
    }
    plain[plen] = 0;
    if (!mark_links(plain, plen, &rl))
        goto oom;

    sz = sizeof(TvLine) + rl.n * sizeof(NKTvRun) + plen + 1;
    l = (TvLine *)malloc(sz);
    if (!l)
        goto oom;
    {
        NKTvRun *runs = (NKTvRun *)(l + 1);
        char *t = (char *)(runs + rl.n);
        if (rl.n)
            memcpy(runs, rl.runs, rl.n * sizeof(NKTvRun));
        memcpy(t, plain, plen + 1);
        l->pub.runs = runs;
        l->pub.text = t;
    }
    l->pub.seq = tv->next_seq++;
    if (!tv->next_seq)
        tv->next_seq = 1;       /* 0 means "none" */
    l->pub.len = plen;
    l->pub.nruns = (uint16_t)(rl.n > 0xffff ? 0xffff : rl.n);
    l->pub.quote = quote;
    l->pub.kind = kind;
    l->bytes = sz;
    free(rl.runs);
    free(plain);

    if (tv->count == tv->cap)
        drop_oldest(tv);
    i = (tv->head + tv->count) % tv->cap;
    tv->ring[i] = l;
    tv->count++;
    tv->bytes += sz;
    while (tv->bytes > tv->max_bytes && tv->count > 1)
        drop_oldest(tv);
    return l->pub.seq;
oom:
    free(rl.runs);
    free(plain);
    return 0;
}

uint32_t nk_tv_count(const NKTextView *tv)
{
    return tv->count;
}

const NKTvLine *nk_tv_line(const NKTextView *tv, uint32_t index)
{
    if (index >= tv->count)
        return 0;
    return &tv->ring[(tv->head + index) % tv->cap]->pub;
}

long nk_tv_index_of(const NKTextView *tv, uint32_t seq)
{
    uint32_t first;
    if (!tv->count || !seq)
        return -1;
    first = nk_tv_line(tv, 0)->seq;
    /* seqs are consecutive inside the ring (unsigned wrap is fine) */
    if ((uint32_t)(seq - first) >= tv->count)
        return -1;
    {
        long idx = (long)(uint32_t)(seq - first);
        return nk_tv_line(tv, (uint32_t)idx)->seq == seq ? idx : -1;
    }
}

void nk_tv_set_marker(NKTextView *tv, uint32_t seq) { tv->marker = seq; }
uint32_t nk_tv_marker(const NKTextView *tv) { return tv->marker; }

/* ---- wrapping -------------------------------------------------------------- */
static uint32_t next_char(const char *t, uint32_t len, uint32_t i)
{
    i++;
    while (i < len && ((unsigned char)t[i] & 0xc0) == 0x80)
        i++;        /* stay on UTF-8 character boundaries */
    return i;
}

uint32_t nk_tv_wrap(const NKTvLine *line, uint32_t width, NKTvMeasure measure,
                    void *user, uint32_t *breaks, uint32_t max)
{
    const char *t = line->text;
    uint32_t len = line->len, row = 0, rows = 1, i, last_space;
    if (!width || !len)
        return 1;
    while (row < len) {
        uint32_t brk = 0;
        last_space = 0;
        for (i = row; i < len;) {
            uint32_t n = next_char(t, len, i);
            if (measure(user, t + row, n - row) > width) {
                if (t[i] == ' ') {
                    /* a space overflowing: break after the spaces */
                    while (n < len && t[n] == ' ')
                        n++;
                    brk = n;
                } else if (last_space > row)
                    brk = last_space;
                else
                    brk = i > row ? i : n;    /* at least one char per row */
                break;
            }
            if (t[i] == ' ')
                last_space = n;
            i = n;
        }
        if (!brk || brk >= len)
            break;
        if (rows - 1 < max)
            breaks[rows - 1] = brk;
        rows++;
        row = brk;
    }
    return rows;
}

/* ---- search and copy -------------------------------------------------------- */
static int match_at(const char *t, uint32_t len, uint32_t i, const char *nd, size_t nl, int nocase)
{
    size_t k;
    if (i + nl > len)
        return 0;
    for (k = 0; k < nl; k++) {
        int a = (unsigned char)t[i + k], b = (unsigned char)nd[k];
        if (nocase) {
            a = lower(a);
            b = lower(b);
        }
        if (a != b)
            return 0;
    }
    return 1;
}

int nk_tv_find(const NKTextView *tv, const char *needle, int nocase, int dir,
               uint32_t *index, uint32_t *offset)
{
    size_t nl = strlen(needle);
    long li = (long)*index;
    long off = (long)*offset;
    if (!nl || !tv->count)
        return 0;
    if (li >= (long)tv->count) {
        li = (long)tv->count - 1;
        off = (long)nk_tv_line(tv, (uint32_t)li)->len;
    }
    while (li >= 0 && li < (long)tv->count) {
        const NKTvLine *l = nk_tv_line(tv, (uint32_t)li);
        long p;
        if (dir >= 0) {
            for (p = off; p + (long)nl <= (long)l->len; p++)
                if (match_at(l->text, l->len, (uint32_t)p, needle, nl, nocase))
                    goto found;
            li++;
            off = 0;
        } else {
            for (p = off - 1; p >= 0; p--)
                if (match_at(l->text, l->len, (uint32_t)p, needle, nl, nocase))
                    goto found;
            li--;
            if (li >= 0)
                off = (long)nk_tv_line(tv, (uint32_t)li)->len;
        }
        continue;
found:
        *index = (uint32_t)li;
        *offset = (uint32_t)p;
        return 1;
    }
    return 0;
}

size_t nk_tv_copy(const NKTextView *tv, uint32_t ia, uint32_t oa,
                  uint32_t ib, uint32_t ob, char *out, size_t cap)
{
    size_t need = 0, w = 0;
    uint32_t i;
    if (ia > ib || (ia == ib && oa > ob)) {
        uint32_t t = ia; ia = ib; ib = t;
        t = oa; oa = ob; ob = t;
    }
    if (ib >= tv->count) {
        ib = tv->count ? tv->count - 1 : 0;
        ob = tv->count ? nk_tv_line(tv, ib)->len : 0;
    }
    for (i = ia; i <= ib && i < tv->count; i++) {
        const NKTvLine *l = nk_tv_line(tv, i);
        uint32_t a = i == ia ? oa : 0, b = i == ib ? ob : l->len, k;
        if (a > l->len) a = l->len;
        if (b > l->len) b = l->len;
        if (i > ia) {
            if (cap && w + 1 < cap) out[w++] = '\n';
            need++;
        }
        for (k = a; k < b; k++) {
            if (cap && w + 1 < cap) out[w++] = l->text[k];
            need++;
        }
    }
    if (cap)
        out[w] = 0;
    return need;
}

int nk_tv_link_at(const NKTvLine *line, uint32_t offset, uint32_t *start, uint32_t *len)
{
    uint16_t r;
    for (r = 0; r < line->nruns; r++) {
        const NKTvRun *x = &line->runs[r];
        if ((x->flags & NK_TV_LINK) && offset >= x->start && offset < x->start + x->len) {
            /* a link may cover several style runs: join neighbours */
            uint32_t s = x->start, e = x->start + x->len;
            uint16_t q = r;
            while (q > 0 && (line->runs[q - 1].flags & NK_TV_LINK) &&
                   line->runs[q - 1].start + line->runs[q - 1].len == s) {
                q--;
                s = line->runs[q].start;
            }
            q = r;
            while (q + 1 < line->nruns && (line->runs[q + 1].flags & NK_TV_LINK) &&
                   line->runs[q + 1].start == e) {
                q++;
                e = line->runs[q].start + line->runs[q].len;
            }
            *start = s;
            *len = e - s;
            return 1;
        }
    }
    return 0;
}

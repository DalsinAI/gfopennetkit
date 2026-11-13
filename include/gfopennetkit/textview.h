/* gfopennetkit text view model: the scrollback behind GFOpenNews' article
 * view and GFOpenIRC's channel windows. No drawing here: the GFOpenLayout
 * element asks this model for lines, runs, links and wrap points.
 * MIT licence, (c) 2026 AmiCradle Team. */
#ifndef GFOPENNETKIT_TEXTVIEW_H
#define GFOPENNETKIT_TEXTVIEW_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* run flags */
#define NK_TV_BOLD      0x01
#define NK_TV_ITALIC    0x02
#define NK_TV_UNDERLINE 0x04
#define NK_TV_STRIKE    0x08
#define NK_TV_MONO      0x10
#define NK_TV_REVERSE   0x20
#define NK_TV_LINK      0x40
#define NK_TV_DIM       0x80   /* news signature */

#define NK_TV_DEFAULT   (-1)   /* fg/bg: the view's own colour */
/* colours 0-98 are mIRC's palette numbers; 0x1000000 | RGB is an \x04 hex colour */
#define NK_TV_RGB(r, g, b) (0x1000000 | ((r) << 16) | ((g) << 8) | (b))

typedef struct NKTvRun {
    uint32_t start, len;   /* byte offsets into the line's plain text */
    int32_t  fg, bg;
    uint8_t  flags;
} NKTvRun;

typedef struct NKTvLine {
    uint32_t seq;          /* grows by one per line added, never reused */
    uint32_t len;          /* plain text bytes (formatting codes taken out) */
    uint16_t nruns;
    uint8_t  quote;        /* news: quote level (leading '>' count) */
    uint8_t  kind;         /* app's own tag: message, notice, join, ... */
    const char *text;      /* NUL-terminated */
    const NKTvRun *runs;
} NKTvLine;

typedef struct NKTextView NKTextView;

/* parse modes for nk_tv_add */
#define NK_TV_PLAIN  0     /* text as given, links only */
#define NK_TV_MIRC   1     /* IRC formatting codes (^B ^C ^D ^] ^_ ^^ ^Q ^V ^O) */
#define NK_TV_NEWS   2     /* quote levels, "-- " signature dimmed after it */

/* max_lines and max_bytes bound the ring; the oldest lines go first. */
NKTextView *nk_tv_new(uint32_t max_lines, size_t max_bytes);
void        nk_tv_free(NKTextView *tv);
void        nk_tv_clear(NKTextView *tv);   /* also ends a news signature */
/* Adds one line (no LF inside; a CR LF or LF at the end is dropped).
 * Answers the line's seq, or 0 when out of memory. */
uint32_t    nk_tv_add(NKTextView *tv, const char *text, size_t len, int mode, uint8_t kind);
uint32_t    nk_tv_count(const NKTextView *tv);
/* index 0 is the oldest line held; NULL when out of range */
const NKTvLine *nk_tv_line(const NKTextView *tv, uint32_t index);
/* index of a seq, or -1 when it has gone out of the ring */
long        nk_tv_index_of(const NKTextView *tv, uint32_t seq);

/* The "last read" marker (IRC): a seq, or 0 for none. */
void        nk_tv_set_marker(NKTextView *tv, uint32_t seq);
uint32_t    nk_tv_marker(const NKTextView *tv);

/* Wrap points. `measure` answers the width of text[0..len) in the caller's
 * units (pixels on screen, characters in tests). Breaks go after spaces;
 * a word wider than `width` is cut where it no longer fits. Writes the
 * start offset of each display row after the first into `breaks` and
 * answers the number of rows (>= 1), even when `max` is too small. */
typedef uint32_t (*NKTvMeasure)(void *user, const char *text, size_t len);
uint32_t nk_tv_wrap(const NKTvLine *line, uint32_t width, NKTvMeasure measure,
                    void *user, uint32_t *breaks, uint32_t max);

/* Search from (index, offset) forwards (dir > 0) or backwards (dir < 0),
 * ASCII case-insensitive when nocase. Answers 1 and sets *index, *offset. */
int nk_tv_find(const NKTextView *tv, const char *needle, int nocase, int dir,
               uint32_t *index, uint32_t *offset);

/* Copies the plain text from (ia, oa) up to (ib, ob), lines joined with LF.
 * Answers the length the whole copy needs; writes at most cap-1 bytes and a
 * NUL when cap > 0. */
size_t nk_tv_copy(const NKTextView *tv, uint32_t ia, uint32_t oa,
                  uint32_t ib, uint32_t ob, char *out, size_t cap);

/* Link at a byte offset of a line: answers 1 and the span, else 0. */
int nk_tv_link_at(const NKTvLine *line, uint32_t offset, uint32_t *start, uint32_t *len);

/* Lower level, also used by the apps: finds the next URL in text[from..len).
 * http://, https://, ftp://, news:, nntp://, irc://, ircs://, mailto: and
 * "www." Trailing punctuation and an unmatched ')' are left out. */
int nk_tv_find_url(const char *text, size_t len, size_t from, size_t *start, size_t *ulen);

#ifdef __cplusplus
}
#endif
#endif

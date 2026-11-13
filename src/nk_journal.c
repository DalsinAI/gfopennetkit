/* Crash-safe append-only journal of text records.
 * MIT licence, (c) 2026 AmiCradle Team.
 *
 * Line format: "<crc32 of record, 8 lower-case hex> <record>\n".
 * Swap on compaction (AmigaDOS Rename() never replaces a file):
 *   1. write <path>.new and close it
 *   2. rename <path> -> <path>.old
 *   3. rename <path>.new -> <path>
 *   4. delete <path>.old
 * Open repairs a crash between any two steps:
 *   - <path> missing, <path>.new present: the crash was after step 2; finish 3.
 *   - <path> present, <path>.new present: before step 2 (or step 1 torn);
 *     <path> is whole, drop <path>.new.
 *   - <path>.old present with <path>: after step 3; drop <path>.old.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gfopennetkit/netkit.h"
#if defined(__unix__) || defined(__APPLE__)
#include <unistd.h>
#define NK_HAVE_FSYNC 1
#endif

struct NKJournal {
    FILE *f;
    char *path, *path_new, *path_old;
    unsigned long records;
};

static char *cat2(const char *a, const char *b)
{
    size_t la = strlen(a), lb = strlen(b);
    char *p = (char *)malloc(la + lb + 1);
    if (p) {
        memcpy(p, a, la);
        memcpy(p + la, b, lb + 1);
    }
    return p;
}

static int exists(const char *p)
{
    FILE *f = fopen(p, "rb");
    if (!f)
        return 0;
    fclose(f);
    return 1;
}

static int file_sync(FILE *f)
{
    if (fflush(f) != 0)
        return NK_EIO;
#ifdef NK_HAVE_FSYNC
    fsync(fileno(f));
#endif
    return NK_OK;
}

static int swap_in(NKJournal *j)
{
    remove(j->path_old);
    if (exists(j->path) && rename(j->path, j->path_old) != 0)
        return NK_EIO;
    if (rename(j->path_new, j->path) != 0) {
        rename(j->path_old, j->path);   /* put the old one back */
        return NK_EIO;
    }
    remove(j->path_old);
    return NK_OK;
}

static void recover(NKJournal *j)
{
    int have = exists(j->path), have_new = exists(j->path_new);
    if (!have && have_new)
        rename(j->path_new, j->path);
    else if (have && have_new)
        remove(j->path_new);
    if (exists(j->path))
        remove(j->path_old);
    else if (exists(j->path_old))
        rename(j->path_old, j->path);   /* crash inside step 2 on a FS without atomic rename */
}

static int hexval(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

/* 1 = good record, 0 = damaged */
static int parse_line(const char *s, size_t n, const char **rec, size_t *rlen)
{
    uint32_t want = 0;
    size_t i;
    if (n < 9 || s[8] != ' ')
        return 0;
    for (i = 0; i < 8; i++) {
        int v = hexval((unsigned char)s[i]);
        if (v < 0)
            return 0;
        want = want << 4 | (uint32_t)v;
    }
    *rec = s + 9;
    *rlen = n - 9;
    return nk_crc32_update(0, *rec, *rlen) == want;
}

/* Reads a whole line (any length). Answers its length, or -1 at EOF.
 * *nl says whether it ended with LF. */
static long read_line(FILE *f, char **buf, size_t *cap, int *nl)
{
    size_t n = 0;
    int c;
    *nl = 0;
    while ((c = getc(f)) != EOF) {
        if (n + 1 >= *cap) {
            size_t nc = *cap ? *cap * 2 : 256;
            char *nb = (char *)realloc(*buf, nc);
            if (!nb)
                return -2;
            *buf = nb;
            *cap = nc;
        }
        if (c == '\n') {
            *nl = 1;
            break;
        }
        (*buf)[n++] = (char)c;
    }
    if (c == EOF && n == 0)
        return -1;
    return (long)n;
}

static int write_rec(FILE *f, const char *rec, size_t len)
{
    if (fprintf(f, "%08lx ", (unsigned long)nk_crc32_update(0, rec, len)) != 9)
        return NK_EIO;
    if (len && fwrite(rec, 1, len, f) != len)
        return NK_EIO;
    return putc('\n', f) == EOF ? NK_EIO : NK_OK;
}

void nk_journal_close(NKJournal *j)
{
    if (!j)
        return;
    if (j->f) {
        file_sync(j->f);
        fclose(j->f);
    }
    free(j->path);
    free(j->path_new);
    free(j->path_old);
    free(j);
}

NKJournal *nk_journal_open(const char *path, NKJournalReplay fn, void *user,
                           NKJournalStats *stats, int *err)
{
    NKJournal *j = (NKJournal *)calloc(1, sizeof(*j));
    NKJournalStats st = { 0, 0, 0 };
    FILE *f;
    char *line = 0;
    size_t cap = 0;
    long good_end = 0, pos = 0, total = 0;
    int e = NK_ENOMEM;

    if (!j)
        goto fail;
    j->path = cat2(path, "");
    j->path_new = cat2(path, ".new");
    j->path_old = cat2(path, ".old");
    if (!j->path || !j->path_new || !j->path_old)
        goto fail;
    recover(j);

    if ((f = fopen(j->path, "rb")) != 0) {
        unsigned long pending_bad = 0;
        for (;;) {
            int nl;
            const char *rec;
            size_t rlen;
            long n = read_line(f, &line, &cap, &nl);
            if (n == -2) {
                fclose(f);
                goto fail;
            }
            if (n < 0)
                break;
            pos += n + nl;
            if (nl && parse_line(line, (size_t)n, &rec, &rlen)) {
                st.skipped += pending_bad;
                pending_bad = 0;
                st.records++;
                good_end = pos;
                if (fn && fn(user, rec, rlen) != 0) {
                    fclose(f);
                    e = NK_EARG;
                    goto fail;
                }
            } else
                pending_bad++;
        }
        total = pos;
        fclose(f);
        if (good_end < total) {
            /* torn or damaged tail: keep the first good_end bytes */
            FILE *in = fopen(j->path, "rb"), *out = fopen(j->path_new, "wb");
            char cp[4096];
            long left = good_end;
            e = NK_EIO;
            if (!in || !out) {
                if (in) fclose(in);
                if (out) fclose(out);
                goto fail;
            }
            while (left > 0) {
                size_t want = left > (long)sizeof(cp) ? sizeof(cp) : (size_t)left;
                size_t got = fread(cp, 1, want, in);
                if (!got || fwrite(cp, 1, got, out) != got)
                    break;
                left -= (long)got;
            }
            fclose(in);
            if (left || file_sync(out) != NK_OK) {
                fclose(out);
                goto fail;
            }
            fclose(out);
            if (swap_in(j) != NK_OK)
                goto fail;
            st.truncated_bytes = (unsigned long)(total - good_end);
        }
    }
    free(line);
    line = 0;
    j->records = st.records;
    j->f = fopen(j->path, "ab");
    if (!j->f) {
        e = NK_EIO;
        goto fail;
    }
    if (stats)
        *stats = st;
    if (err)
        *err = NK_OK;
    return j;
fail:
    free(line);
    nk_journal_close(j);
    if (err)
        *err = e;
    return 0;
}

int nk_journal_append(NKJournal *j, const char *rec, size_t len)
{
    int r;
    if (memchr(rec, '\n', len))
        return NK_EARG;
    r = write_rec(j->f, rec, len);
    if (r == NK_OK)
        j->records++;
    return r;
}

int nk_journal_sync(NKJournal *j)
{
    return file_sync(j->f);
}

int nk_journal_compact(NKJournal *j, NKJournalNext next, void *user)
{
    FILE *out = fopen(j->path_new, "wb");
    const char *rec;
    size_t len;
    unsigned long n = 0;
    int r = NK_OK, k;
    if (!out)
        return NK_EIO;
    while ((k = next(user, &rec, &len)) == 1) {
        if (memchr(rec, '\n', len)) {
            r = NK_EARG;
            break;
        }
        if ((r = write_rec(out, rec, len)) != NK_OK)
            break;
        n++;
    }
    if (r == NK_OK && k < 0)
        r = k;
    if (r == NK_OK)
        r = file_sync(out);
    fclose(out);
    if (r != NK_OK) {
        remove(j->path_new);
        return r;
    }
    file_sync(j->f);
    fclose(j->f);
    j->f = 0;
    r = swap_in(j);
    j->f = fopen(j->path, "ab");
    if (!j->f)
        return NK_EIO;
    if (r == NK_OK)
        j->records = n;
    return r;
}

unsigned long nk_journal_records(const NKJournal *j)
{
    return j->records;
}

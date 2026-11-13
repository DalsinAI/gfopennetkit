/* Text view model tests. MIT licence, (c) 2026 AmiCradle Team. */
#include "gfopennetkit/textview.h"
#include "nk_test.h"

#define S(x) x, sizeof(x) - 1

/* one unit per character (UTF-8 aware) */
static uint32_t chars(void *u, const char *t, size_t n)
{
    uint32_t c = 0;
    size_t i;
    (void)u;
    for (i = 0; i < n; i++)
        if (((unsigned char)t[i] & 0xc0) != 0x80)
            c++;
    return c;
}

static const NKTvRun *run_at(const NKTvLine *l, uint32_t off)
{
    uint16_t r;
    for (r = 0; r < l->nruns; r++)
        if (off >= l->runs[r].start && off < l->runs[r].start + l->runs[r].len)
            return &l->runs[r];
    return 0;
}

static int covers(const NKTvLine *l)
{
    uint32_t pos = 0;
    uint16_t r;
    for (r = 0; r < l->nruns; r++) {
        if (l->runs[r].start != pos) return 0;
        pos += l->runs[r].len;
    }
    return pos == l->len;
}

int main(void)
{
    NKTextView *tv = nk_tv_new(4, 0);
    const NKTvLine *l;
    const NKTvRun *r;
    uint32_t s1, i, off, st, ln, br[16];
    size_t us, ul;
    char buf[128];

    /* mIRC codes come out of the text and into runs */
    s1 = nk_tv_add(tv, S("\x02" "bold\x02 \x03" "4,2red\x0f plain\x1d it\x1d\r\n"), NK_TV_MIRC, 1);
    CHECK_EQ(s1, 1);
    l = nk_tv_line(tv, 0);
    CHECK(strcmp(l->text, "bold red plain it") == 0);
    CHECK(covers(l));
    r = run_at(l, 0);  CHECK(r && r->flags == NK_TV_BOLD && r->fg == NK_TV_DEFAULT);
    r = run_at(l, 5);  CHECK(r && r->fg == 4 && r->bg == 2 && r->flags == 0);
    r = run_at(l, 9);  CHECK(r && r->fg == NK_TV_DEFAULT && r->bg == NK_TV_DEFAULT);
    r = run_at(l, 15); CHECK(r && r->flags == NK_TV_ITALIC);
    CHECK_EQ(l->kind, 1);
    /* ^C alone resets colours; ^C99 is default; 3 digits keep the third */
    nk_tv_add(tv, S("\x03" "12a\x03" "b\x03" "99c\x03" "123d"), NK_TV_MIRC, 0);
    l = nk_tv_line(tv, 1);
    CHECK(strcmp(l->text, "abc3d") == 0);
    CHECK_EQ(run_at(l, 0)->fg, 12);
    CHECK_EQ(run_at(l, 1)->fg, NK_TV_DEFAULT);
    CHECK_EQ(run_at(l, 3)->fg, 12);
    /* ^C followed by a comma with no digit leaves the comma as text */
    nk_tv_add(tv, S("\x03" "5,x"), NK_TV_MIRC, 0);
    l = nk_tv_line(tv, 2);
    CHECK(strcmp(l->text, ",x") == 0);
    CHECK_EQ(run_at(l, 0)->fg, 5);
    /* ^D hex colours */
    nk_tv_add(tv, S("\x04" "FF8000,000000hex\x04" "zz"), NK_TV_MIRC, 0);
    l = nk_tv_line(tv, 3);
    CHECK(strcmp(l->text, "hexzz") == 0);
    CHECK_EQ(run_at(l, 0)->fg, NK_TV_RGB(0xff, 0x80, 0));
    CHECK_EQ(run_at(l, 0)->bg, NK_TV_RGB(0, 0, 0));
    CHECK_EQ(run_at(l, 3)->fg, NK_TV_DEFAULT);

    /* the ring: a fifth line pushes out the first; seqs carry on */
    CHECK_EQ(nk_tv_add(tv, S("five"), NK_TV_PLAIN, 0), 5);
    CHECK_EQ(nk_tv_count(tv), 4);
    CHECK_EQ(nk_tv_line(tv, 0)->seq, 2);
    CHECK_EQ(nk_tv_index_of(tv, 1), -1);
    CHECK_EQ(nk_tv_index_of(tv, 5), 3);
    CHECK_EQ(nk_tv_index_of(tv, 0), -1);
    nk_tv_set_marker(tv, 4);
    CHECK_EQ(nk_tv_marker(tv), 4);
    nk_tv_free(tv);

    /* byte limit */
    tv = nk_tv_new(1000, 400);
    for (i = 0; i < 50; i++)
        nk_tv_add(tv, S("0123456789012345678901234567890123456789"), NK_TV_PLAIN, 0);
    CHECK(nk_tv_count(tv) < 10 && nk_tv_count(tv) >= 1);
    CHECK_EQ(nk_tv_line(tv, nk_tv_count(tv) - 1)->seq, 50);
    nk_tv_free(tv);

    /* links */
    CHECK(nk_tv_find_url("see https://amicradle.com/x.", 28, 0, &us, &ul) && us == 4 && ul == 23);
    CHECK(nk_tv_find_url("(https://en.wikipedia.org/wiki/Amiga_(computer))", 48, 0, &us, &ul)
          && us == 1 && ul == 46);
    CHECK(nk_tv_find_url("go www.aminet.net!", 18, 0, &us, &ul) && us == 3 && ul == 14);
    CHECK(nk_tv_find_url("<news:comp.sys.amiga.misc>", 26, 0, &us, &ul) && us == 1 && ul == 24);
    CHECK(nk_tv_find_url("mail mailto:dale@example.org, ok", 32, 0, &us, &ul) && us == 5 && ul == 23);
    CHECK(!nk_tv_find_url("xhttp://no and http:// alone", 28, 0, &us, &ul));
    CHECK(nk_tv_find_url("ircs://irc.libera.chat:6697/#amiga", 34, 0, &us, &ul) && ul == 34);
    tv = nk_tv_new(10, 0);
    nk_tv_add(tv, S("\x02" "bold https://a.example/\x02" "x/y end"), NK_TV_MIRC, 0);
    l = nk_tv_line(tv, 0);
    CHECK(strcmp(l->text, "bold https://a.example/x/y end") == 0);
    CHECK(covers(l));
    CHECK(run_at(l, 5)->flags == (NK_TV_BOLD | NK_TV_LINK));
    CHECK(run_at(l, 24)->flags == NK_TV_LINK);
    CHECK(run_at(l, 27)->flags == 0);
    CHECK(nk_tv_link_at(l, 24, &st, &ln) && st == 5 && ln == 21);   /* spans two style runs */
    CHECK(!nk_tv_link_at(l, 2, &st, &ln));

    /* news: quotes and signature */
    nk_tv_clear(tv);
    nk_tv_add(tv, S("> > quoted twice"), NK_TV_NEWS, 0);
    nk_tv_add(tv, S(">>> three"), NK_TV_NEWS, 0);
    nk_tv_add(tv, S("body"), NK_TV_NEWS, 0);
    nk_tv_add(tv, S("-- "), NK_TV_NEWS, 0);
    nk_tv_add(tv, S("Dale, A1200"), NK_TV_NEWS, 0);
    CHECK_EQ(nk_tv_line(tv, 0)->quote, 2);
    CHECK_EQ(nk_tv_line(tv, 1)->quote, 3);
    CHECK_EQ(nk_tv_line(tv, 2)->quote, 0);
    CHECK_EQ(nk_tv_line(tv, 2)->runs[0].flags, 0);
    CHECK_EQ(nk_tv_line(tv, 3)->runs[0].flags, NK_TV_DIM);
    CHECK_EQ(nk_tv_line(tv, 4)->runs[0].flags, NK_TV_DIM);
    nk_tv_clear(tv);                               /* next article: no sig */
    nk_tv_add(tv, S("fresh"), NK_TV_NEWS, 0);
    CHECK_EQ(nk_tv_line(tv, 0)->runs[0].flags, 0);
    nk_tv_add(tv, S(""), NK_TV_NEWS, 0);
    CHECK_EQ(nk_tv_line(tv, 1)->nruns, 0);
    CHECK_EQ(nk_tv_line(tv, 1)->len, 0);

    /* wrapping */
    nk_tv_clear(tv);
    nk_tv_add(tv, S("the quick brown fox jumps"), NK_TV_PLAIN, 0);
    l = nk_tv_line(tv, 0);
    CHECK_EQ(nk_tv_wrap(l, 100, chars, 0, br, 16), 1);
    CHECK_EQ(nk_tv_wrap(l, 10, chars, 0, br, 16), 3);
    CHECK_EQ(br[0], 10);  /* "the quick " */
    CHECK_EQ(br[1], 20);  /* "brown fox " */
    CHECK_EQ(nk_tv_wrap(l, 9, chars, 0, br, 16), 3);   /* "the quick" fits; the space goes to the end of row 1 */
    CHECK_EQ(br[0], 10);
    nk_tv_add(tv, S("abcdefghijkl"), NK_TV_PLAIN, 0);
    l = nk_tv_line(tv, 1);
    CHECK_EQ(nk_tv_wrap(l, 5, chars, 0, br, 16), 3);   /* long word is cut */
    CHECK(br[0] == 5 && br[1] == 10);
    CHECK_EQ(nk_tv_wrap(l, 5, chars, 0, br, 1), 3);    /* count even when breaks is short */
    CHECK_EQ(nk_tv_wrap(l, 0, chars, 0, br, 16), 1);
    nk_tv_add(tv, S("\xc3\xa9\xc3\xa9\xc3\xa9"), NK_TV_PLAIN, 0); /* three e-acute */
    l = nk_tv_line(tv, 2);
    CHECK_EQ(nk_tv_wrap(l, 2, chars, 0, br, 16), 2);
    CHECK_EQ(br[0], 4);                               /* never inside a character */
    CHECK_EQ(nk_tv_wrap(l, 1, chars, 0, br, 16), 3);
    CHECK(br[0] == 2 && br[1] == 4);

    /* search */
    i = 0; off = 0;
    CHECK(nk_tv_find(tv, "FOX", 1, 1, &i, &off) && i == 0 && off == 16);
    i = 0; off = 0;
    CHECK(!nk_tv_find(tv, "FOX", 0, 1, &i, &off));
    i = 0; off = 17;
    CHECK(nk_tv_find(tv, "ghi", 0, 1, &i, &off) && i == 1 && off == 6);
    i = 2; off = 0;
    CHECK(nk_tv_find(tv, "quick", 0, -1, &i, &off) && i == 0 && off == 4);
    i = 99; off = 0;                                  /* from the end */
    CHECK(nk_tv_find(tv, "the", 0, -1, &i, &off) && i == 0 && off == 0);
    CHECK(!nk_tv_find(tv, "", 0, 1, &i, &off));

    /* copy */
    CHECK_EQ(nk_tv_copy(tv, 0, 16, 1, 3, buf, sizeof(buf)), 13);
    CHECK(strcmp(buf, "fox jumps\nabc") == 0);
    CHECK_EQ(nk_tv_copy(tv, 1, 3, 0, 16, buf, sizeof(buf)), 13);   /* reversed ends */
    CHECK(strcmp(buf, "fox jumps\nabc") == 0);
    CHECK_EQ(nk_tv_copy(tv, 0, 16, 1, 3, buf, 5), 13);              /* truncated, NUL kept */
    CHECK(strcmp(buf, "fox ") == 0);
    nk_tv_free(tv);
    CHECK(nk_tv_new(0, 0) == 0);
    TEST_DONE();
}

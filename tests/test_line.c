/* Line reader tests. MIT licence, (c) 2026 AmiCradle Team. */
#include "gfopennetkit/netkit.h"
#include "nk_test.h"

static void feed(NKLineReader *lr, const char *s)
{
    size_t room, n = strlen(s);
    char *p = nk_lr_space(lr, &room);
    if (room < n) { fprintf(stderr, "no room\n"); exit(2); }
    memcpy(p, s, n);
    nk_lr_commit(lr, n);
}

int main(void)
{
    char buf[64];
    NKLineReader lr;
    const char *l;
    size_t n;

    nk_lr_init(&lr, buf, sizeof(buf), 20);
    CHECK_EQ(nk_lr_next(&lr, &l, &n), 0);
    feed(&lr, "200 hello\r\nPING :x\nhalf");
    CHECK_EQ(nk_lr_next(&lr, &l, &n), 1); CHECK_STRN(l, n, "200 hello");
    CHECK_EQ(nk_lr_next(&lr, &l, &n), 1); CHECK_STRN(l, n, "PING :x");
    CHECK_EQ(nk_lr_next(&lr, &l, &n), 0);
    feed(&lr, " line\r");
    CHECK_EQ(nk_lr_next(&lr, &l, &n), 0);       /* CR without LF yet */
    feed(&lr, "\n\r\n");
    CHECK_EQ(nk_lr_next(&lr, &l, &n), 1); CHECK_STRN(l, n, "half line");
    CHECK_EQ(nk_lr_next(&lr, &l, &n), 1); CHECK_EQ(n, 0);   /* empty line */
    CHECK_EQ(nk_lr_next(&lr, &l, &n), 0);

    /* exactly max_line is fine, one more is dropped, the next line survives */
    feed(&lr, "aaaaaaaaaaaaaaaaaaaa\r\n");
    CHECK_EQ(nk_lr_next(&lr, &l, &n), 1); CHECK_EQ(n, 20);
    feed(&lr, "bbbbbbbbbbbbbbbbbbbbb\r\nok\r\n");
    CHECK_EQ(nk_lr_next(&lr, &l, &n), NK_ETOOLONG);
    CHECK_EQ(nk_lr_next(&lr, &l, &n), 1); CHECK_STRN(l, n, "ok");

    /* an endless line with no LF: one ETOOLONG, then skip, then recover */
    feed(&lr, "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxx");
    CHECK_EQ(nk_lr_next(&lr, &l, &n), NK_ETOOLONG);
    CHECK_EQ(nk_lr_next(&lr, &l, &n), 0);
    feed(&lr, "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx");
    CHECK_EQ(nk_lr_next(&lr, &l, &n), 0);
    feed(&lr, "xxx\nafter\n");
    CHECK_EQ(nk_lr_next(&lr, &l, &n), 1); CHECK_STRN(l, n, "after");

    /* raw bytes after a header line (binary body) */
    feed(&lr, "HDR\r\n\x01\x02\x03");
    CHECK_EQ(nk_lr_next(&lr, &l, &n), 1); CHECK_STRN(l, n, "HDR");
    CHECK_EQ(nk_lr_buffered(&lr), 3);
    {
        unsigned char raw[8];
        CHECK_EQ(nk_lr_take(&lr, raw, sizeof(raw)), 3);
        CHECK(raw[0] == 1 && raw[2] == 3);
        CHECK_EQ(nk_lr_buffered(&lr), 0);
    }

    /* many lines one byte at a time through a small buffer (compaction) */
    {
        const char *s = "one\r\ntwo\r\nthree\r\n";
        int i, k, got = 0;
        for (k = 0; k < 50; k++)
            for (i = 0; s[i]; i++) {
                char c[2] = { s[i], 0 };
                feed(&lr, c);
                while (nk_lr_next(&lr, &l, &n) == 1)
                    got++;
            }
        CHECK_EQ(got, 150);
    }

    /* a max_line too big for the buffer is clamped */
    nk_lr_init(&lr, buf, 16, 1000);
    CHECK_EQ(lr.max_line, 14);
    TEST_DONE();
}

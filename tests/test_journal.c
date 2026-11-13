/* Journal tests: replay, torn tail, damage, compaction, crash recovery.
 * MIT licence, (c) 2026 AmiCradle Team. */
#include "gfopennetkit/netkit.h"
#include "nk_test.h"

static char got[32][64];
static int ngot;

static int collect(void *u, const char *rec, size_t len)
{
    (void)u;
    if (ngot < 32 && len < 64) {
        memcpy(got[ngot], rec, len);
        got[ngot][len] = 0;
    }
    ngot++;
    return 0;
}

static NKJournal *reopen(const char *p, NKJournalStats *st)
{
    int e;
    NKJournal *j;
    ngot = 0;
    j = nk_journal_open(p, collect, 0, st, &e);
    CHECK(j != 0);
    CHECK_EQ(e, NK_OK);
    return j;
}

static void write_file(const char *p, const char *s)
{
    FILE *f = fopen(p, "wb");
    fputs(s, f);
    fclose(f);
}

static long file_size(const char *p)
{
    FILE *f = fopen(p, "rb");
    long n;
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fclose(f);
    return n;
}

static const char *recs[] = { "add 1 big.iso", "seg 1 1", "seg 1 2", "done 1" };
static int ri;
static int next_live(void *u, const char **rec, size_t *len)
{
    (void)u;
    if (ri >= 2) return 0;
    *rec = ri == 0 ? "add 1 big.iso" : "done 1";
    *len = strlen(*rec);
    ri++;
    return 1;
}

int main(int argc, char **argv)
{
    char path[512], pnew[520], pold[520];
    NKJournal *j;
    NKJournalStats st;
    int i;

    snprintf(path, sizeof(path), "%s/nkj-test.journal", argc > 1 ? argv[1] : ".");
    snprintf(pnew, sizeof(pnew), "%s.new", path);
    snprintf(pold, sizeof(pold), "%s.old", path);
    remove(path); remove(pnew); remove(pold);

    /* empty start, append, replay in order */
    j = reopen(path, &st);
    CHECK_EQ(st.records, 0);
    for (i = 0; i < 4; i++)
        CHECK_EQ(nk_journal_append(j, recs[i], strlen(recs[i])), NK_OK);
    CHECK_EQ(nk_journal_append(j, "a\nb", 3), NK_EARG);
    CHECK_EQ(nk_journal_append(j, "", 0), NK_OK);      /* empty record is fine */
    CHECK_EQ(nk_journal_sync(j), NK_OK);
    CHECK_EQ(nk_journal_records(j), 5);
    nk_journal_close(j);
    j = reopen(path, &st);
    CHECK_EQ(st.records, 5);
    CHECK_EQ(ngot, 5);
    CHECK(strcmp(got[0], "add 1 big.iso") == 0);
    CHECK(strcmp(got[3], "done 1") == 0);
    CHECK(got[4][0] == 0);
    nk_journal_close(j);

    /* torn tail (power cut mid-append): cut off, file shrinks, appends go on */
    {
        long before = file_size(path);
        FILE *f = fopen(path, "ab");
        fputs("1234abcd seg 1 3 (no newl", f);
        fclose(f);
        j = reopen(path, &st);
        CHECK_EQ(st.records, 5);
        CHECK_EQ(st.truncated_bytes, 25);
        CHECK_EQ(file_size(path), before);
        CHECK_EQ(nk_journal_append(j, "seg 1 3", 7), NK_OK);
        nk_journal_close(j);
        j = reopen(path, &st);
        CHECK_EQ(st.records, 6);
        CHECK(strcmp(got[5], "seg 1 3") == 0);
        nk_journal_close(j);
    }

    /* a damaged record in the middle is skipped and counted, not fatal */
    {
        char ok1[64], ok2[64], buf[256];
        snprintf(ok1, sizeof(ok1), "%08lx one\n", (unsigned long)nk_crc32_update(0, "one", 3));
        snprintf(ok2, sizeof(ok2), "%08lx three\n", (unsigned long)nk_crc32_update(0, "three", 5));
        snprintf(buf, sizeof(buf), "%s%s%s%s", ok1, "deadbeef two\n", "garbage\n", ok2);
        write_file(path, buf);
        j = reopen(path, &st);
        CHECK_EQ(st.records, 2);
        CHECK_EQ(st.skipped, 2);
        CHECK_EQ(st.truncated_bytes, 0);
        CHECK(strcmp(got[1], "three") == 0);
        nk_journal_close(j);
        /* damage only at the end is a torn tail, cut away */
        snprintf(buf, sizeof(buf), "%s%s", ok1, "deadbeef two\n");
        write_file(path, buf);
        j = reopen(path, &st);
        CHECK_EQ(st.records, 1);
        CHECK_EQ(st.truncated_bytes, 13);
        nk_journal_close(j);
    }

    /* compaction keeps only the live records */
    remove(path);
    j = reopen(path, &st);
    for (i = 0; i < 4; i++)
        nk_journal_append(j, recs[i], strlen(recs[i]));
    ri = 0;
    CHECK_EQ(nk_journal_compact(j, next_live, 0), NK_OK);
    CHECK_EQ(nk_journal_records(j), 2);
    CHECK_EQ(nk_journal_append(j, "seg 2 1", 7), NK_OK);  /* still appendable */
    nk_journal_close(j);
    CHECK_EQ(file_size(pnew), -1);
    CHECK_EQ(file_size(pold), -1);
    j = reopen(path, &st);
    CHECK_EQ(ngot, 3);
    CHECK(strcmp(got[1], "done 1") == 0 && strcmp(got[2], "seg 2 1") == 0);
    nk_journal_close(j);

    /* crash recovery, one case per gap in the swap */
    {
        char a[64], b[64];
        snprintf(a, sizeof(a), "%08lx old\n", (unsigned long)nk_crc32_update(0, "old", 3));
        snprintf(b, sizeof(b), "%08lx new\n", (unsigned long)nk_crc32_update(0, "new", 3));
        /* .new being written when it stopped: the main file wins */
        write_file(path, a); write_file(pnew, "0000"); remove(pold);
        j = reopen(path, &st);
        CHECK(ngot == 1 && strcmp(got[0], "old") == 0);
        CHECK_EQ(file_size(pnew), -1);
        nk_journal_close(j);
        /* stopped after the main file went to .old: .new is finished */
        remove(path); write_file(pold, a); write_file(pnew, b);
        j = reopen(path, &st);
        CHECK(ngot == 1 && strcmp(got[0], "new") == 0);
        CHECK_EQ(file_size(pold), -1);
        CHECK_EQ(file_size(pnew), -1);
        nk_journal_close(j);
        /* stopped before deleting .old */
        write_file(path, b); write_file(pold, a); remove(pnew);
        j = reopen(path, &st);
        CHECK(ngot == 1 && strcmp(got[0], "new") == 0);
        CHECK_EQ(file_size(pold), -1);
        nk_journal_close(j);
        /* only .old left (rename without replace failed half way) */
        remove(path); write_file(pold, a); remove(pnew);
        j = reopen(path, &st);
        CHECK(ngot == 1 && strcmp(got[0], "old") == 0);
        nk_journal_close(j);
    }

    /* the replay callback can stop the open */
    {
        int e;
        j = nk_journal_open(path, 0, 0, 0, &e);
        CHECK(j != 0);
        nk_journal_close(j);
    }
    remove(path);
    TEST_DONE();
}

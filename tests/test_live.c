/* End-to-end over real sockets to the fake servers (tests/fakes/fakeserv.py),
 * directly and through the fake SOCKS5 proxy. Local ports only.
 * MIT licence, (c) 2026 AmiCradle Team. */
#include <poll.h>
#include <stdio.h>
#include "gfopennetkit/netkit.h"
#include "nk_test.h"

/* Waits (poll, at most ~10 s of 50 ms turns) for the next line. The time
 * passed to the connection is a counter, not the clock. */
static int wait_line(NKConn *c, char *out, size_t cap)
{
    const char *l;
    size_t n;
    int i, r;
    for (i = 0; i < 200; i++) {
        r = nk_conn_line(c, &l, &n);
        if (r == 1) {
            if (n >= cap) n = cap - 1;
            memcpy(out, l, n);
            out[n] = 0;
            return 1;
        }
        {
            struct pollfd p;
            p.fd = nk_conn_fd(c);
            p.events = POLLIN | (nk_conn_outq(c) ? POLLOUT : 0);
            poll(&p, 1, 50);
        }
        if ((r = nk_conn_pump(c, (uint32_t)i)) != NK_OK)
            return r;
    }
    return NK_ETIMEDOUT;
}

static int port_of(const char *name)
{
    const char *v = getenv(name);
    return v ? atoi(v) : 0;
}

static void nntp_session(NKConn *c)
{
    char l[512];
    int body = 0;
    CHECK_EQ(wait_line(c, l, sizeof(l)), 1);
    CHECK(strncmp(l, "200 ", 4) == 0);
    nk_conn_sendline(c, "GROUP comp.sys.amiga.misc");
    CHECK_EQ(wait_line(c, l, sizeof(l)), 1);
    CHECK(strncmp(l, "480", 3) == 0);        /* not logged in yet */
    nk_conn_sendline(c, "AUTHINFO USER dale");
    CHECK_EQ(wait_line(c, l, sizeof(l)), 1);
    CHECK(strncmp(l, "381", 3) == 0);
    nk_conn_sendline(c, "AUTHINFO PASS s3cret");
    CHECK_EQ(wait_line(c, l, sizeof(l)), 1);
    CHECK(strncmp(l, "281", 3) == 0);
    nk_conn_sendline(c, "GROUP comp.sys.amiga.misc");
    CHECK_EQ(wait_line(c, l, sizeof(l)), 1);
    CHECK(strcmp(l, "211 3 1 3 comp.sys.amiga.misc") == 0);
    nk_conn_sendline(c, "BODY 3");
    CHECK_EQ(wait_line(c, l, sizeof(l)), 1);
    CHECK(strncmp(l, "222 3 <3@fake>", 14) == 0);
    for (;;) {
        CHECK_EQ(wait_line(c, l, sizeof(l)), 1);
        if (strcmp(l, ".") == 0) break;
        if (body == 0) CHECK(strcmp(l, "..leading dot line") == 0); /* still stuffed on the wire */
        body++;
    }
    CHECK_EQ(body, 3);
    /* POST: 340, the article dot-stuffed, then 240; the fake undoes the
     * dots and (with NK_NNTP_POSTED) writes it out */
    nk_conn_sendline(c, "POST");
    CHECK_EQ(wait_line(c, l, sizeof(l)), 1);
    CHECK(strncmp(l, "340", 3) == 0);
    nk_conn_sendline(c, "From: k@example.invalid");
    nk_conn_sendline(c, "Newsgroups: comp.sys.amiga.misc");
    nk_conn_sendline(c, "Subject: test post");
    nk_conn_sendline(c, "");
    nk_conn_sendline(c, "..a line with a dot");
    nk_conn_sendline(c, ".");
    CHECK_EQ(wait_line(c, l, sizeof(l)), 1);
    CHECK(strncmp(l, "240 article posted <post", 24) == 0);
    {
        const char *d = getenv("NK_NNTP_POSTED");
        char path[1024], got[256];
        FILE *f;
        size_t n;
        if (d) {
            snprintf(path, sizeof(path), "%s/%d.art", d, atoi(l + 24));
            f = fopen(path, "rb");
            CHECK(f != 0);
            if (f) {
                n = fread(got, 1, sizeof(got) - 1, f);
                got[n] = 0;
                fclose(f);
                CHECK(strcmp(got, "From: k@example.invalid\nNewsgroups: comp.sys.amiga.misc\n"
                                  "Subject: test post\n\n.a line with a dot\n") == 0);
            }
        }
    }
    /* an article without Newsgroups is refused */
    nk_conn_sendline(c, "POST");
    CHECK_EQ(wait_line(c, l, sizeof(l)), 1);
    CHECK(strncmp(l, "340", 3) == 0);
    nk_conn_sendline(c, "From: k@example.invalid");
    nk_conn_sendline(c, "Subject: no groups");
    nk_conn_sendline(c, "");
    nk_conn_sendline(c, "text");
    nk_conn_sendline(c, ".");
    CHECK_EQ(wait_line(c, l, sizeof(l)), 1);
    CHECK(strcmp(l, "441 posting failed: no Newsgroups header") == 0);
    nk_conn_sendline(c, "QUIT");
    CHECK_EQ(wait_line(c, l, sizeof(l)), 1);
    CHECK(strncmp(l, "205", 3) == 0);
    CHECK_EQ(wait_line(c, l, sizeof(l)), NK_EOF);
}

int main(void)
{
    int nntp = port_of("NK_NNTP_PORT"), irc = port_of("NK_IRC_PORT"),
        socks = port_of("NK_SOCKS5_PORT");
    NKTransport t;
    NKConn *c;
    char l[512];

    if (!nntp || !irc || !socks) {
        fprintf(stderr, "run through fakeserv.py --run nntp,irc,socks5 -- test_live\n");
        return 2;
    }
    /* straight to NNTP */
    CHECK_EQ(nk_posix_tcp("127.0.0.1", (uint16_t)nntp, &t), NK_OK);
    c = nk_conn_new(0, t, 0);
    nntp_session(c);
    nk_conn_free(c);

    /* NNTP through SOCKS5 with a user and password */
    {
        NKSocks5 s;
        int r, i;
        CHECK_EQ(nk_posix_tcp("127.0.0.1", (uint16_t)socks, &t), NK_OK);
        nk_socks5_init(&s, "localhost", (uint16_t)nntp, "proxyuser", "proxypass");
        for (i = 0; i < 400 && (r = nk_socks5_step(&s, &t)) == NK_WOULDBLOCK; i++) {
            struct pollfd p;
            p.fd = t.fd(t.ctx);
            p.events = POLLIN;  /* the requests are small: they always go at once */
            poll(&p, 1, 25);
        }
        CHECK_EQ(r, 1);
        c = nk_conn_new(0, t, 0);
        nntp_session(c);
        nk_conn_free(c);
        /* a wrong proxy password is refused */
        CHECK_EQ(nk_posix_tcp("127.0.0.1", (uint16_t)socks, &t), NK_OK);
        nk_socks5_init(&s, "localhost", (uint16_t)nntp, "proxyuser", "nope");
        for (i = 0; i < 400 && (r = nk_socks5_step(&s, &t)) == NK_WOULDBLOCK; i++) {
            struct pollfd p;
            p.fd = t.fd(t.ctx);
            p.events = POLLIN;
            poll(&p, 1, 25);
        }
        CHECK_EQ(r, NK_EREFUSED);
        nk_transport_close(&t);
    }

    /* IRC: registration, PING/PONG, JOIN */
    CHECK_EQ(nk_posix_tcp("127.0.0.1", (uint16_t)irc, &t), NK_OK);
    c = nk_conn_new(0, t, 0);
    nk_conn_sendline(c, "NICK krissie");
    nk_conn_sendline(c, "USER krissie 0 * :Krissie");
    CHECK_EQ(wait_line(c, l, sizeof(l)), 1);
    CHECK(strstr(l, " 001 krissie ") != 0);
    CHECK_EQ(wait_line(c, l, sizeof(l)), 1);
    CHECK(strstr(l, " 005 ") != 0);
    CHECK_EQ(wait_line(c, l, sizeof(l)), 1);
    CHECK(strcmp(l, ":fake.irc PING :tok123") == 0);
    nk_conn_sendline(c, "PONG :tok123");
    nk_conn_sendline(c, "JOIN #amiga");
    CHECK_EQ(wait_line(c, l, sizeof(l)), 1);
    CHECK(strcmp(l, ":krissie!u@h JOIN #amiga") == 0);
    nk_conn_sendline(c, "QUIT :bye");
    while (wait_line(c, l, sizeof(l)) == 1)
        ;
    nk_conn_free(c);
    TEST_DONE();
}

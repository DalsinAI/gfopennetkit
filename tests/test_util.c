/* nk_parse_ipv4 and nk_strerror tests. MIT licence, (c) 2026 AmiCradle Team. */
#include "gfopennetkit/netkit.h"
#include "nk_test.h"

static int bad(const char *s)
{
    uint32_t a = 0x12345678u;
    return nk_parse_ipv4(s, &a) == 0 && a == 0x12345678u;
}

int main(void)
{
    uint32_t a = 0;

    CHECK_EQ(nk_parse_ipv4("192.168.0.195", &a), 1);
    CHECK_EQ(a, 0xC0A800C3u);
    CHECK_EQ(nk_parse_ipv4("127.0.0.1", &a), 1);
    CHECK_EQ(a, 0x7F000001u);
    CHECK_EQ(nk_parse_ipv4("0.0.0.0", &a), 1);
    CHECK_EQ(a, 0);
    CHECK_EQ(nk_parse_ipv4("255.255.255.255", &a), 1);
    CHECK_EQ(a, 0xFFFFFFFFu);
    CHECK_EQ(nk_parse_ipv4("010.001.000.009", &a), 1); /* decimal, not octal */
    CHECK_EQ(a, 0x0A010009u);

    /* names and anything not exactly four parts go to the resolver */
    CHECK(bad("irc.libera.chat"));
    CHECK(bad("localhost"));
    CHECK(bad(""));
    CHECK(bad("1.2.3"));
    CHECK(bad("1.2.3.4.5"));
    CHECK(bad("1.2.3.4."));
    CHECK(bad(".1.2.3"));
    CHECK(bad("1..2.3"));
    CHECK(bad("256.0.0.1"));
    CHECK(bad("1.2.3.0256"));
    CHECK(bad("1.2.3.4 "));
    CHECK(bad(" 1.2.3.4"));
    CHECK(bad("+1.2.3.4"));
    CHECK(bad("1.2.3.-4"));
    CHECK(bad("1.2.3.4x"));
    CHECK(bad("192.168.0.195:6667"));
    CHECK(bad("0x7f.0.0.1"));
    CHECK(bad("99999999999.1.1.1"));
    CHECK_EQ(nk_parse_ipv4(0, &a), 0);
    CHECK_EQ(nk_parse_ipv4("1.2.3.4", 0), 0);

    /* every connect step reads differently */
    CHECK(strcmp(nk_strerror(NK_ENOTFOUND), "host not found") == 0);
    CHECK(strstr(nk_strerror(NK_ESOCKET), "socket") != 0);
    CHECK(strcmp(nk_strerror(NK_EREFUSED), "refused") == 0);
    CHECK(strcmp(nk_strerror(NK_ETIMEDOUT), "timed out") == 0);
    CHECK(strcmp(nk_strerror(-99), "unknown error") == 0);

    TEST_DONE();
}

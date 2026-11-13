/* Error texts, dotted IPv4 addresses and connect errors.
 * MIT licence, (c) 2026 AmiCradle Team. */
#define _POSIX_C_SOURCE 200809L
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include "gfopennetkit/netkit.h"
#include "nk_test.h"

static int ip_is(const char *s, int a, int b, int c, int d)
{
    uint8_t o[4];
    return nk_parse_ipv4(s, o) && o[0] == a && o[1] == b && o[2] == c && o[3] == d;
}

int main(void)
{
    uint8_t o[4];
    int e;
    CHECK(ip_is("192.168.0.195", 192, 168, 0, 195));
    CHECK(ip_is("127.0.0.1", 127, 0, 0, 1));
    CHECK(ip_is("0.0.0.0", 0, 0, 0, 0));
    CHECK(ip_is("255.255.255.255", 255, 255, 255, 255));
    CHECK(ip_is("010.1.1.1", 10, 1, 1, 1));
    CHECK(!nk_parse_ipv4("256.1.1.1", o));
    CHECK(!nk_parse_ipv4("1.2.3", o));
    CHECK(!nk_parse_ipv4("1.2.3.4.5", o));
    CHECK(!nk_parse_ipv4("1.2.3.4 ", o));
    CHECK(!nk_parse_ipv4(" 1.2.3.4", o));
    CHECK(!nk_parse_ipv4("1..2.3", o));
    CHECK(!nk_parse_ipv4("1.2.3.", o));
    CHECK(!nk_parse_ipv4("1.2.3.0004", o));
    CHECK(!nk_parse_ipv4("irc.libera.chat", o));
    CHECK(!nk_parse_ipv4("1e3.1.1.1", o));
    CHECK(!nk_parse_ipv4("", o));
    /* every code has its own text */
    for (e = NK_OK; e >= NK_ESOCKET; e--)
        CHECK(strcmp(nk_strerror(e), "unknown error") != 0);
    CHECK(!strcmp(nk_strerror(NK_ERESOLVE), "host name not found"));
    CHECK(!strcmp(nk_strerror(-99), "unknown error"));
    /* connect: a name that can't resolve (.invalid never does), and a
     * port we just held and let go, so nothing listens there */
    {
        NKTransport t;
        struct sockaddr_in sa;
        socklen_t sl = sizeof(sa);
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        CHECK_EQ(nk_posix_tcp("no-such-host.invalid", 1, &t), NK_ERESOLVE);
        memset(&sa, 0, sizeof(sa));
        sa.sin_family = AF_INET;
        sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        CHECK(fd >= 0 && bind(fd, (struct sockaddr *)&sa, sizeof(sa)) == 0 &&
              getsockname(fd, (struct sockaddr *)&sa, &sl) == 0);
        close(fd);
        CHECK_EQ(nk_posix_tcp("127.0.0.1", ntohs(sa.sin_port), &t), NK_EREFUSED);
    }
    TEST_DONE();
}

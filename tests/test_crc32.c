/* CRC-32 tests. MIT licence, (c) 2026 AmiCradle Team. */
#include "gfopennetkit/netkit.h"
#include "nk_test.h"

static uint32_t bitwise(const uint8_t *p, size_t n)
{
    uint32_t c = 0xffffffffu;
    size_t i;
    int k;
    for (i = 0; i < n; i++) {
        c ^= p[i];
        for (k = 0; k < 8; k++)
            c = (c & 1) ? (c >> 1) ^ 0xEDB88320u : c >> 1;
    }
    return ~c;
}

int main(void)
{
    static uint8_t buf[5000];
    uint32_t seed = 12345;
    size_t i, a;

    CHECK_EQ(nk_crc32_update(0, "", 0), 0);
    CHECK_EQ(nk_crc32_update(0, "123456789", 9), 0xCBF43926u);
    CHECK_EQ(nk_crc32_update(0, "The quick brown fox jumps over the lazy dog", 43), 0x414FA339u);

    for (i = 0; i < sizeof(buf); i++) {
        seed = seed * 1103515245u + 12345u;
        buf[i] = (uint8_t)(seed >> 16);
    }
    /* every length and alignment against the plain bitwise form */
    for (i = 0; i < 70; i++)
        for (a = 0; a < 9; a++)
            CHECK_EQ(nk_crc32_update(0, buf + a, i), bitwise(buf + a, i));
    CHECK_EQ(nk_crc32_update(0, buf, sizeof(buf)), bitwise(buf, sizeof(buf)));
    /* streaming in pieces gives the same answer */
    {
        uint32_t c = 0;
        for (i = 0; i < sizeof(buf); i += 333)
            c = nk_crc32_update(c, buf + i, sizeof(buf) - i < 333 ? sizeof(buf) - i : 333);
        CHECK_EQ(c, bitwise(buf, sizeof(buf)));
    }
    /* combine: crc(A||B) from crc(A), crc(B), len(B) */
    {
        static const size_t cuts[] = { 0, 1, 7, 8, 100, 2500, 4999, 5000 };
        for (i = 0; i < sizeof(cuts) / sizeof(cuts[0]); i++) {
            size_t c = cuts[i];
            uint32_t ca = nk_crc32_update(0, buf, c);
            uint32_t cb = nk_crc32_update(0, buf + c, sizeof(buf) - c);
            CHECK_EQ(nk_crc32_combine(ca, cb, sizeof(buf) - c), bitwise(buf, sizeof(buf)));
        }
    }
    TEST_DONE();
}

/* CRC-32 (IEEE 802.3, reflected polynomial 0xEDB88320). Our own code.
 * MIT licence, (c) 2026 AmiCradle Team. */
#include "gfopennetkit/netkit.h"

#define NK_POLY 0xEDB88320u

static uint32_t nk_table[8][256];
static int nk_table_ready;

static void nk_crc_init(void)
{
    uint32_t i, k, c;
    for (i = 0; i < 256; i++) {
        c = i;
        for (k = 0; k < 8; k++)
            c = (c & 1) ? (c >> 1) ^ NK_POLY : c >> 1;
        nk_table[0][i] = c;
    }
    /* slicing-by-8: table[k][i] is the CRC of byte i followed by k zeros */
    for (i = 0; i < 256; i++)
        for (k = 1; k < 8; k++)
            nk_table[k][i] = (nk_table[k - 1][i] >> 8) ^ nk_table[0][nk_table[k - 1][i] & 0xff];
    nk_table_ready = 1;
}

uint32_t nk_crc32_update(uint32_t crc, const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t c;
    if (!nk_table_ready)
        nk_crc_init();
    c = ~crc;
    while (len >= 8) {
        /* byte order independent: build the words from bytes */
        uint32_t lo = c ^ ((uint32_t)p[0] | (uint32_t)p[1] << 8 |
                           (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24);
        c = nk_table[7][lo & 0xff] ^ nk_table[6][(lo >> 8) & 0xff] ^
            nk_table[5][(lo >> 16) & 0xff] ^ nk_table[4][lo >> 24] ^
            nk_table[3][p[4]] ^ nk_table[2][p[5]] ^
            nk_table[1][p[6]] ^ nk_table[0][p[7]];
        p += 8;
        len -= 8;
    }
    while (len--)
        c = (c >> 8) ^ nk_table[0][(c ^ *p++) & 0xff];
    return ~c;
}

/* a * b modulo the polynomial, in reflected bit order (x^0 is bit 31) */
static uint32_t nk_mulmod(uint32_t a, uint32_t b)
{
    uint32_t m = 0x80000000u, p = 0;
    if (!a)
        return 0;
    for (;;) {
        if (a & m) {
            p ^= b;
            if ((a & (m - 1)) == 0)
                break;
        }
        m >>= 1;
        b = (b & 1) ? (b >> 1) ^ NK_POLY : b >> 1;
    }
    return p;
}

/* x^(8n) modulo the polynomial */
static uint32_t nk_x8n(uint64_t n)
{
    uint32_t r = 0x80000000u, sq = 0x40000000u; /* x^0, x^1 */
    uint64_t e = n * 8;
    while (e) {
        if (e & 1)
            r = nk_mulmod(sq, r);
        sq = nk_mulmod(sq, sq);
        e >>= 1;
    }
    return r;
}

uint32_t nk_crc32_combine(uint32_t crc_a, uint32_t crc_b, uint64_t len_b)
{
    /* with init and final xor both ~0 their effects cancel, so the CRC of
     * A||B is crc(A) moved on by len(B) zero bytes, xor crc(B) */
    return nk_mulmod(nk_x8n(len_b), crc_a) ^ crc_b;
}

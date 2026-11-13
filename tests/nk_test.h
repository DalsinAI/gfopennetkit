/* Minimal test helpers. MIT licence, (c) 2026 AmiCradle Team. */
#ifndef NK_TEST_H
#define NK_TEST_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int nk_t_fail, nk_t_run;
#define CHECK(c) do { nk_t_run++; if (!(c)) { nk_t_fail++; \
    fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c); } } while (0)
#define CHECK_EQ(a, b) do { long long _a = (long long)(a), _b = (long long)(b); nk_t_run++; \
    if (_a != _b) { nk_t_fail++; fprintf(stderr, "%s:%d: %s == %lld, want %lld\n", \
    __FILE__, __LINE__, #a, _a, _b); } } while (0)
#define CHECK_STRN(p, n, s) do { nk_t_run++; if ((n) != strlen(s) || memcmp((p), (s), (n))) { \
    nk_t_fail++; fprintf(stderr, "%s:%d: got \"%.*s\", want \"%s\"\n", __FILE__, __LINE__, \
    (int)(n), (const char *)(p), (s)); } } while (0)
#define TEST_DONE() do { printf("%d checks, %d failed\n", nk_t_run, nk_t_fail); \
    return nk_t_fail ? 1 : 0; } while (0)
#endif

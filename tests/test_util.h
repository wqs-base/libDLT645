/*
 * test_util.h - Minimal assertion helpers for the unit tests.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef DLT645_TEST_UTIL_H
#define DLT645_TEST_UTIL_H

#include <stdio.h>
#include <string.h>

static int g_tests = 0;
static int g_failed = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        g_tests++;                                                         \
        if (!(cond)) {                                                     \
            g_failed++;                                                    \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);       \
        }                                                                  \
    } while (0)

#define CHECK_EQ_INT(a, b)                                                 \
    do {                                                                   \
        long long _a = (long long)(a);                                     \
        long long _b = (long long)(b);                                     \
        g_tests++;                                                         \
        if (_a != _b) {                                                    \
            g_failed++;                                                    \
            printf("  FAIL %s:%d: %s=%lld != %s=%lld\n", __FILE__,         \
                   __LINE__, #a, _a, #b, _b);                              \
        }                                                                  \
    } while (0)

#define CHECK_EQ_MEM(a, b, n)                                              \
    do {                                                                   \
        g_tests++;                                                         \
        if (memcmp((a), (b), (n)) != 0) {                                  \
            g_failed++;                                                    \
            printf("  FAIL %s:%d: memory differs over %d bytes\n",         \
                   __FILE__, __LINE__, (int)(n));                          \
        }                                                                  \
    } while (0)

#define TEST_REPORT(name)                                                  \
    do {                                                                   \
        printf("%s: %d checks, %d failed\n", (name), g_tests, g_failed);   \
        return g_failed == 0 ? 0 : 1;                                      \
    } while (0)

#endif /* DLT645_TEST_UTIL_H */

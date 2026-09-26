// SPDX-License-Identifier: AGPL-3.0-only
#ifndef CLEANROOM_TEST_H
#define CLEANROOM_TEST_H
#include <stdio.h>

static int g_fail;
static int g_check;

#define CHECK(cond, ...)                                                   \
    do {                                                                   \
        g_check++;                                                         \
        if (!(cond)) {                                                     \
            g_fail++;                                                      \
            fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__);           \
            fprintf(stderr, __VA_ARGS__);                                  \
            fprintf(stderr, "\n");                                         \
        }                                                                  \
    } while (0)

#define TEST_END(name)                                                     \
    do {                                                                   \
        if (g_fail) {                                                      \
            printf("%s: %d/%d checks FAILED\n", name, g_fail, g_check);    \
            return 1;                                                      \
        }                                                                  \
        printf("%s: OK (%d checks)\n", name, g_check);                     \
        return 0;                                                          \
    } while (0)

#endif

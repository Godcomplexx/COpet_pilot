#ifndef COPET_TEST_UTIL_H
#define COPET_TEST_UTIL_H

#include <stdio.h>
#include <string.h>

static int g_test_failures = 0;
static int g_test_checks = 0;

#define CHECK(condition)                                                      \
    do {                                                                      \
        ++g_test_checks;                                                      \
        if (!(condition)) {                                                   \
            ++g_test_failures;                                                \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);     \
        }                                                                     \
    } while (0)

#define CHECK_STR(actual, expected)                                          \
    do {                                                                      \
        ++g_test_checks;                                                      \
        if (strcmp((actual), (expected)) != 0) {                              \
            ++g_test_failures;                                                \
            printf("  FAIL %s:%d: got \"%s\", expected \"%s\"\n",             \
                   __FILE__, __LINE__, (actual), (expected));                 \
        }                                                                     \
    } while (0)

#define TEST_REPORT(suite_name)                                              \
    do {                                                                      \
        printf("%s: %d checks, %d failures\n", (suite_name),                  \
               g_test_checks, g_test_failures);                               \
        return g_test_failures == 0 ? 0 : 1;                                  \
    } while (0)

#endif

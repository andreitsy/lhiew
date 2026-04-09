#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int _tests_run    = 0;
static int _tests_passed = 0;
static int _tests_failed = 0;

#define ASSERT(cond)                                                         \
    do {                                                                     \
        if (!(cond)) {                                                       \
            fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);\
            _tests_failed++;                                                 \
            return;                                                          \
        }                                                                    \
    } while (0)

#define ASSERT_EQ(a, b)                                                      \
    do {                                                                     \
        if ((a) != (b)) {                                                    \
            fprintf(stderr, "  FAIL %s:%d: %s != %s\n",                     \
                    __FILE__, __LINE__, #a, #b);                             \
            _tests_failed++;                                                 \
            return;                                                          \
        }                                                                    \
    } while (0)

#define ASSERT_STR_EQ(a, b)                                                  \
    do {                                                                     \
        if (strcmp((a), (b)) != 0) {                                         \
            fprintf(stderr, "  FAIL %s:%d: \"%s\" != \"%s\"\n",             \
                    __FILE__, __LINE__, (a), (b));                           \
            _tests_failed++;                                                 \
            return;                                                          \
        }                                                                    \
    } while (0)

#define ASSERT_NE(a, b)                                                      \
    do {                                                                     \
        if ((a) == (b)) {                                                    \
            fprintf(stderr, "  FAIL %s:%d: %s == %s\n",                     \
                    __FILE__, __LINE__, #a, #b);                             \
            _tests_failed++;                                                 \
            return;                                                          \
        }                                                                    \
    } while (0)

#define ASSERT_GT(a, b)                                                      \
    do {                                                                     \
        if (!((a) > (b))) {                                                  \
            fprintf(stderr, "  FAIL %s:%d: %s not > %s\n",                  \
                    __FILE__, __LINE__, #a, #b);                             \
            _tests_failed++;                                                 \
            return;                                                          \
        }                                                                    \
    } while (0)

#define RUN_TEST(fn)                                                         \
    do {                                                                     \
        int _prev = _tests_failed;                                           \
        _tests_run++;                                                        \
        fn();                                                                \
        if (_tests_failed == _prev) {                                        \
            _tests_passed++;                                                 \
            printf("  PASS %s\n", #fn);                                     \
        }                                                                    \
    } while (0)

#define TEST_REPORT()                                                        \
    do {                                                                     \
        printf("\n%d/%d passed", _tests_passed, _tests_run);                 \
        if (_tests_failed) printf(", %d FAILED", _tests_failed);             \
        printf("\n");                                                        \
        return _tests_failed ? EXIT_FAILURE : EXIT_SUCCESS;                  \
    } while (0)

/* Zero out global_cfg for a clean test */
#define RESET_GLOBAL_CFG()                                                   \
    memset(&global_cfg, 0, sizeof(global_cfg))

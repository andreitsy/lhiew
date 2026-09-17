#include "lhiew/types.h"
#include "lhiew/search.h"

#include "test_harness.h"

#include <string.h>

static const uint8_t sample[] = {
    0x00, 0x0d, 0x0a, 0x50, 0x4b, 0x03, 0x04, 0x0d,
    0x0a, 0xff, 0x50, 0x4b, 0x0d, 0x0a, 0x00, 0x00,
};

static void compile_hex(const char *text, const uint8_t *expected, size_t count) {
    uint8_t pattern[SEARCH_PATTERN_MAX];
    size_t length = 0;
    ASSERT(search_compile(text, strlen(text), 0, pattern, &length));
    ASSERT_EQ(count, length);
    ASSERT(!memcmp(pattern, expected, count));
}

static void test_compile_accepts_pairs_and_separators(void) {
    const uint8_t expected[] = {0x0d, 0x0a};
    compile_hex("0d0a", expected, 2);
    compile_hex("0D 0A", expected, 2);
    compile_hex(" 0d\t0a ", expected, 2);
}

static void test_compile_rejects_incomplete_bytes(void) {
    uint8_t pattern[SEARCH_PATTERN_MAX];
    size_t length = 1;
    /* A lone nibble is an unfinished byte rather than a short pattern. */
    ASSERT(!search_compile("0d0", 3, 0, pattern, &length));
    ASSERT(!search_compile("0d 0", 4, 0, pattern, &length));
    ASSERT(!search_compile("0 d", 3, 0, pattern, &length));
    ASSERT(!search_compile("zz", 2, 0, pattern, &length));
}

static void test_compile_empty_input_has_no_pattern(void) {
    uint8_t pattern[SEARCH_PATTERN_MAX];
    size_t length = 7;
    ASSERT(search_compile("", 0, 0, pattern, &length));
    ASSERT_EQ((size_t)0, length);
    ASSERT(search_compile("   ", 3, 0, pattern, &length));
    ASSERT_EQ((size_t)0, length);
}

static void test_compile_text_is_literal(void) {
    uint8_t pattern[SEARCH_PATTERN_MAX];
    size_t length = 0;
    ASSERT(search_compile("PK 0d", 5, 1, pattern, &length));
    ASSERT_EQ((size_t)5, length);
    ASSERT(!memcmp(pattern, "PK 0d", 5));
}

static void test_compile_enforces_pattern_limit(void) {
    char text[2 * SEARCH_PATTERN_MAX + 4];
    uint8_t pattern[SEARCH_PATTERN_MAX];
    size_t length = 0;
    memset(text, 'a', sizeof(text));
    ASSERT(search_compile(text, 2 * SEARCH_PATTERN_MAX, 0, pattern, &length));
    ASSERT_EQ((size_t)SEARCH_PATTERN_MAX, length);
    ASSERT(!search_compile(text, 2 * SEARCH_PATTERN_MAX + 2, 0, pattern, &length));
    ASSERT(!search_compile(text, SEARCH_PATTERN_MAX + 1, 1, pattern, &length));
}

static void test_find_forward_starts_at_offset(void) {
    const uint8_t needle[] = {0x0d, 0x0a};
    size_t found = SIZE_MAX;
    ASSERT(search_find(sample, sizeof(sample), needle, 2, 0, 0, &found));
    ASSERT_EQ((size_t)1, found);
    /* Starting on a match reports it in place; the caller advances to repeat. */
    ASSERT(search_find(sample, sizeof(sample), needle, 2, 1, 0, &found));
    ASSERT_EQ((size_t)1, found);
    ASSERT(search_find(sample, sizeof(sample), needle, 2, 2, 0, &found));
    ASSERT_EQ((size_t)7, found);
    ASSERT(search_find(sample, sizeof(sample), needle, 2, 8, 0, &found));
    ASSERT_EQ((size_t)12, found);
    ASSERT(!search_find(sample, sizeof(sample), needle, 2, 13, 0, &found));
}

static void test_find_backward_scans_towards_zero(void) {
    const uint8_t needle[] = {0x0d, 0x0a};
    size_t found = SIZE_MAX;
    ASSERT(search_find(sample, sizeof(sample), needle, 2, sizeof(sample), 1, &found));
    ASSERT_EQ((size_t)12, found);
    ASSERT(search_find(sample, sizeof(sample), needle, 2, 11, 1, &found));
    ASSERT_EQ((size_t)7, found);
    ASSERT(search_find(sample, sizeof(sample), needle, 2, 6, 1, &found));
    ASSERT_EQ((size_t)1, found);
    ASSERT(!search_find(sample, sizeof(sample), needle, 2, 0, 1, &found));
}

static void test_find_rejects_impossible_spans(void) {
    const uint8_t needle[] = {0x50, 0x4b};
    size_t found = SIZE_MAX;
    /* A pattern longer than the file can never match at any offset. */
    ASSERT(!search_find(sample, 1, needle, 2, 0, 0, &found));
    ASSERT(!search_find(sample, sizeof(sample), needle, 0, 0, 0, &found));
    ASSERT(!search_find(NULL, sizeof(sample), needle, 2, 0, 0, &found));
    /* A forward start past the last candidate offset is not a match. */
    ASSERT(!search_find(sample, sizeof(sample), needle, 2, sizeof(sample), 0, &found));
}

static void test_find_locates_single_byte_at_boundaries(void) {
    const uint8_t zero[] = {0x00};
    size_t found = SIZE_MAX;
    ASSERT(search_find(sample, sizeof(sample), zero, 1, 0, 0, &found));
    ASSERT_EQ((size_t)0, found);
    ASSERT(search_find(sample, sizeof(sample), zero, 1, sizeof(sample) - 1, 0, &found));
    ASSERT_EQ(sizeof(sample) - 1, found);
    ASSERT(search_find(sample, sizeof(sample), zero, 1, sizeof(sample) - 1, 1, &found));
    ASSERT_EQ(sizeof(sample) - 1, found);
}

int main(void) {
    RUN_TEST(test_compile_accepts_pairs_and_separators);
    RUN_TEST(test_compile_rejects_incomplete_bytes);
    RUN_TEST(test_compile_empty_input_has_no_pattern);
    RUN_TEST(test_compile_text_is_literal);
    RUN_TEST(test_compile_enforces_pattern_limit);
    RUN_TEST(test_find_forward_starts_at_offset);
    RUN_TEST(test_find_backward_scans_towards_zero);
    RUN_TEST(test_find_rejects_impossible_spans);
    RUN_TEST(test_find_locates_single_byte_at_boundaries);
    TEST_REPORT();
}

#include "lhiew/types.h"
#include "lhiew/search.h"
#include "lhiew/input.h"

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

static void test_compile_failure_never_exposes_partial_length(void) {
    uint8_t pattern[SEARCH_PATTERN_MAX];
    size_t length = 42;
    ASSERT(!search_compile("001", 3, 0, pattern, &length));
    ASSERT_EQ(length, (size_t)0);
    ASSERT(!search_compile("00zz", 4, 0, pattern, &length));
    ASSERT_EQ(length, (size_t)0);
    ASSERT(!search_compile(NULL, 0, 0, pattern, &length));
    ASSERT(!search_compile("00", 2, 0, NULL, &length));
    ASSERT(!search_compile("00", 2, 0, pattern, NULL));
}

static void test_find_overlapping_matches_and_extreme_starts(void) {
    const uint8_t data[] = "ababa";
    const uint8_t pattern[] = "aba";
    size_t found = SIZE_MAX;
    ASSERT(search_find(data, 5, pattern, 3, 1, 0, &found));
    ASSERT_EQ(found, (size_t)2);
    ASSERT(search_find(data, 5, pattern, 3, 1, 1, &found));
    ASSERT_EQ(found, (size_t)0);
    ASSERT(search_find(data, 5, pattern, 3, SIZE_MAX, 1, &found));
    ASSERT_EQ(found, (size_t)2);
    ASSERT(!search_find(data, 5, pattern, 3, SIZE_MAX, 0, &found));
    ASSERT_EQ(found, (size_t)2);
    ASSERT(!search_find(data, 5, NULL, 3, 0, 0, &found));
    ASSERT(!search_find(data, 5, pattern, 3, 0, 0, NULL));
}

static void test_failed_repeat_retains_selected_direction(void) {
    uint8_t data[] = "ababa";
    RESET_GLOBAL_CFG();
    global_cfg.file = data;
    global_cfg.num_bytes = sizeof(data) - 1;
    global_cfg.screencols = 24;
    memcpy(global_cfg.search_pattern, "aba", 3);
    global_cfg.search_pattern_length = 3;
    ASSERT(!search_repeat(1));
    ASSERT_EQ(global_cfg.search_backward, 1);
    ASSERT_EQ(global_cfg.cur_byte, (size_t)0);
    ASSERT(search_repeat(0));
    ASSERT_EQ(global_cfg.search_backward, 0);
    ASSERT_EQ(global_cfg.cur_byte, (size_t)2);
    ASSERT(!search_repeat(0));
    ASSERT_EQ(global_cfg.cur_byte, (size_t)2);
    ASSERT(search_repeat(1));
    ASSERT_EQ(global_cfg.cur_byte, (size_t)0);
}

static void test_text_prompt_retains_and_corrects_long_hex_input(void) {
    uint8_t data[SEARCH_PATTERN_MAX];
    memset(data, 'a', sizeof(data));
    RESET_GLOBAL_CFG();
    global_cfg.file = data;
    global_cfg.num_bytes = sizeof(data);
    global_cfg.screencols = 24;
    search_open_prompt();
    for (size_t i = 0; i < 2 * SEARCH_PATTERN_MAX; ++i)
        search_keypress('a');
    search_keypress('\t');
    ASSERT_EQ(global_cfg.search_ascii, 1);
    ASSERT_EQ(global_cfg.search_input_length, (size_t)2 * SEARCH_PATTERN_MAX);
    search_keypress('b');
    ASSERT_EQ(global_cfg.search_input_length, (size_t)2 * SEARCH_PATTERN_MAX);
    ASSERT_EQ(global_cfg.search_input[2 * SEARCH_PATTERN_MAX - 1], 'a');
    search_keypress('\r');
    ASSERT_EQ(global_cfg.search_prompt, 1);
    ASSERT_EQ(global_cfg.search_pattern_length, (size_t)0);

    /* Backspace can correct retained input even while above the text limit. */
    for (size_t i = 0; i < SEARCH_PATTERN_MAX; ++i)
        search_keypress(127);
    ASSERT_EQ(global_cfg.search_input_length, (size_t)SEARCH_PATTERN_MAX);
    ASSERT_EQ(global_cfg.search_input[SEARCH_PATTERN_MAX], '\0');
    search_keypress('b');
    ASSERT_EQ(global_cfg.search_input_length, (size_t)SEARCH_PATTERN_MAX);
    search_keypress('\r');
    ASSERT_EQ(global_cfg.search_prompt, 0);
    ASSERT_EQ(global_cfg.search_pattern_length, sizeof(data));
    ASSERT_EQ(memcmp(global_cfg.search_pattern, data, sizeof(data)), 0);

    search_open_prompt();
    search_keypress(CTRL_KEY('u'));
    search_keypress(CTRL_KEY('h'));
    ASSERT_EQ(global_cfg.search_input_length, (size_t)0);
    ASSERT_EQ(global_cfg.search_input[0], '\0');
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
    RUN_TEST(test_compile_failure_never_exposes_partial_length);
    RUN_TEST(test_find_overlapping_matches_and_extreme_starts);
    RUN_TEST(test_failed_repeat_retains_selected_direction);
    RUN_TEST(test_text_prompt_retains_and_corrects_long_hex_input);
    TEST_REPORT();
}

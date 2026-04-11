#include "test_harness.h"
#include "lhiew/append_buffer.h"

static void test_init(void) {
    append_buffer ab = ABUF_INIT;
    ASSERT_EQ(ab.buffer, NULL);
    ASSERT_EQ(ab.len, (size_t)0);
}

static void test_append_single(void) {
    append_buffer ab = ABUF_INIT;
    append_to_buffer(&ab, "hello", 5);
    ASSERT_EQ(ab.len, (size_t)5);
    ASSERT(memcmp(ab.buffer, "hello", 5) == 0);
    free_append_buffer(&ab);
}

static void test_append_multiple(void) {
    append_buffer ab = ABUF_INIT;
    append_to_buffer(&ab, "foo", 3);
    append_to_buffer(&ab, "bar", 3);
    ASSERT_EQ(ab.len, (size_t)6);
    ASSERT(memcmp(ab.buffer, "foobar", 6) == 0);
    free_append_buffer(&ab);
}

static void test_append_empty(void) {
    append_buffer ab = ABUF_INIT;
    append_to_buffer(&ab, "", 0);
    ASSERT_EQ(ab.len, (size_t)0);
    free_append_buffer(&ab);
}

static void test_append_binary(void) {
    append_buffer ab = ABUF_INIT;
    const char data[] = "\x00\x01\x02\xff";
    append_to_buffer(&ab, data, 4);
    ASSERT_EQ(ab.len, (size_t)4);
    ASSERT_EQ((unsigned char)ab.buffer[0], 0x00);
    ASSERT_EQ((unsigned char)ab.buffer[3], 0xff);
    free_append_buffer(&ab);
}

static void test_free_resets(void) {
    append_buffer ab = ABUF_INIT;
    append_to_buffer(&ab, "test", 4);
    free_append_buffer(&ab);
    ASSERT_EQ(ab.buffer, NULL);
    ASSERT_EQ(ab.len, (size_t)0);
}

static void test_append_large(void) {
    append_buffer ab = ABUF_INIT;
    for (int i = 0; i < 1000; i++) {
        append_to_buffer(&ab, "x", 1);
    }
    ASSERT_EQ(ab.len, (size_t)1000);
    free_append_buffer(&ab);
}

int main(void) {
    printf("test_append_buffer:\n");
    RUN_TEST(test_init);
    RUN_TEST(test_append_single);
    RUN_TEST(test_append_multiple);
    RUN_TEST(test_append_empty);
    RUN_TEST(test_append_binary);
    RUN_TEST(test_free_resets);
    RUN_TEST(test_append_large);
    TEST_REPORT();
}

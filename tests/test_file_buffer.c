#include "lhiew/types.h"
#include "test_harness.h"
#include "lhiew/file_buffer.h"

#include <unistd.h>

static const char *TMP_FILE = "/tmp/lhiew_test_file.bin";

static void create_test_file(const void *data, size_t len) {
    FILE *f = fopen(TMP_FILE, "wb");
    fwrite(data, 1, len, f);
    fclose(f);
}

static void cleanup_test_file(void) {
    unlink(TMP_FILE);
}

static void test_open_file(void) {
    RESET_GLOBAL_CFG();
    global_cfg.cur_screencols = 80;
    const uint8_t data[] = {0xDE, 0xAD, 0xBE, 0xEF, 0x41, 0x42, 0x43, 0x44};
    create_test_file(data, sizeof(data));

    open_file_to_view((char *)TMP_FILE);

    ASSERT_EQ(global_cfg.num_bytes, sizeof(data));
    ASSERT_NE(global_cfg.file, NULL);
    ASSERT_EQ(global_cfg.file[0], 0xDE);
    ASSERT_EQ(global_cfg.file[1], 0xAD);
    ASSERT_EQ(global_cfg.file[4], 0x41);
    ASSERT_EQ(global_cfg.file[7], 0x44);
    ASSERT_STR_EQ(global_cfg.filename, TMP_FILE);
    ASSERT_NE(global_cfg.fp, NULL);

    fclose(global_cfg.fp);
    global_cfg.fp = NULL;
    free(global_cfg.filename);
    global_cfg.filename = NULL;
    cleanup_test_file();
}

static void test_open_file_sets_numrows(void) {
    RESET_GLOBAL_CFG();
    global_cfg.cur_screencols = 4;
    const uint8_t data[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    create_test_file(data, sizeof(data));

    open_file_to_view((char *)TMP_FILE);

    /* 10 bytes / 4 cols + 1 = 3 */
    ASSERT_EQ(global_cfg.numrows, (size_t)3);

    fclose(global_cfg.fp);
    global_cfg.fp = NULL;
    free(global_cfg.filename);
    global_cfg.filename = NULL;
    cleanup_test_file();
}

static void test_mmap_readonly(void) {
    RESET_GLOBAL_CFG();
    global_cfg.cur_screencols = 80;
    const char data[] = "Hello, LHiew!";
    create_test_file(data, sizeof(data) - 1); /* exclude null terminator */

    open_file_to_view((char *)TMP_FILE);

    ASSERT_EQ(global_cfg.num_bytes, sizeof(data) - 1);
    ASSERT(memcmp(global_cfg.file, "Hello, LHiew!", 13) == 0);

    fclose(global_cfg.fp);
    global_cfg.fp = NULL;
    free(global_cfg.filename);
    global_cfg.filename = NULL;
    cleanup_test_file();
}

int main(void) {
    printf("test_file_buffer:\n");
    RUN_TEST(test_open_file);
    RUN_TEST(test_open_file_sets_numrows);
    RUN_TEST(test_mmap_readonly);
    TEST_REPORT();
}

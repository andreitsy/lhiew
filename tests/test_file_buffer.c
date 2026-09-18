#include "lhiew/types.h"
#include "test_harness.h"
#include "lhiew/file_buffer.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

static char test_path[64];
static int test_fd = -1;

static void cleanup(void) {
    if (global_cfg.file)
        munmap(global_cfg.file, global_cfg.num_bytes);
    if (global_cfg.fp)
        fclose(global_cfg.fp);
    free(global_cfg.filename);
    if (test_fd >= 0)
        close(test_fd);
    if (test_path[0])
        unlink(test_path);
    test_fd = -1;
    test_path[0] = '\0';
    RESET_GLOBAL_CFG();
}

static int open_bytes(const void *data, size_t length, size_t width) {
    cleanup();
    strcpy(test_path, "/tmp/lhiew-file-buffer-XXXXXX");
    test_fd = mkstemp(test_path);
    if (test_fd < 0 || (length && write(test_fd, data, length) != (ssize_t)length))
        return 0;
    global_cfg.cur_screencols = width;
    open_file_to_view(test_path);
    return 1;
}

static void test_open_file(void) {
    const uint8_t bytes[] = {0xde, 0xad, 0xbe, 0xef, 0x41, 0x42, 0x43, 0x44};
    ASSERT(open_bytes(bytes, sizeof(bytes), 80));
    ASSERT_EQ(global_cfg.num_bytes, sizeof(bytes));
    ASSERT_NE(global_cfg.file, NULL);
    ASSERT_EQ(memcmp(global_cfg.file, bytes, sizeof(bytes)), 0);
    ASSERT_STR_EQ(global_cfg.filename, test_path);
    ASSERT_NE(global_cfg.fp, NULL);
}

static void test_open_file_sets_numrows(void) {
    ASSERT(open_bytes("0123456789", 10, 4));
    ASSERT_EQ(global_cfg.numrows, (size_t)3);
    /* The one-past-EOF viewing cursor occupies a row for exact multiples. */
    ASSERT(open_bytes("01234567", 8, 4));
    ASSERT_EQ(global_cfg.numrows, (size_t)3);
    ASSERT(open_bytes("012", 3, 0));
    ASSERT_EQ(global_cfg.numrows, (size_t)4);
}

static void test_open_empty_file(void) {
    ASSERT(open_bytes(NULL, 0, 24));
    ASSERT_EQ(global_cfg.num_bytes, (size_t)0);
    ASSERT_EQ(global_cfg.numrows, (size_t)0);
    ASSERT_EQ(global_cfg.file, NULL);
    ASSERT_NE(global_cfg.fp, NULL);
}

static void test_reload_grows_and_empties_mapping(void) {
    ASSERT(open_bytes("abc", 3, 24));
    ASSERT_EQ(pwrite(test_fd, "def", 3, 3), 3);
    ASSERT(reload_file_in_editor());
    ASSERT_EQ(global_cfg.num_bytes, (size_t)6);
    ASSERT_EQ(memcmp(global_cfg.file, "abcdef", 6), 0);
    ASSERT_EQ(ftruncate(test_fd, 0), 0);
    ASSERT(reload_file_in_editor());
    ASSERT_EQ(global_cfg.num_bytes, (size_t)0);
    ASSERT_EQ(global_cfg.file, NULL);
    ASSERT_EQ(pwrite(test_fd, "x", 1, 0), 1);
    ASSERT(reload_file_in_editor());
    ASSERT_EQ(global_cfg.num_bytes, (size_t)1);
    ASSERT_EQ(global_cfg.file[0], 'x');
}

static void test_failed_reload_preserves_mapping(void) {
    ASSERT(open_bytes("abc", 3, 24));
    FILE *original = global_cfg.fp;
    uint8_t *mapping = global_cfg.file;
    global_cfg.fp = NULL;
    ASSERT(!reload_file_in_editor());
    ASSERT_EQ(errno, EBADF);
    ASSERT_EQ(global_cfg.file, mapping);
    ASSERT_EQ(global_cfg.num_bytes, (size_t)3);
    global_cfg.fp = fopen("/dev/null", "rb");
    ASSERT_NE(global_cfg.fp, NULL);
    ASSERT(!reload_file_in_editor());
    ASSERT_EQ(errno, EINVAL);
    ASSERT_EQ(global_cfg.file, mapping);
    ASSERT_EQ(global_cfg.num_bytes, (size_t)3);
    ASSERT_EQ(memcmp(global_cfg.file, "abc", 3), 0);
    fclose(global_cfg.fp);
    global_cfg.fp = original;
}

static void test_reopen_owned_filename(void) {
    ASSERT(open_bytes("abc", 3, 24));
    open_file_to_view(global_cfg.filename);
    ASSERT_STR_EQ(global_cfg.filename, test_path);
    ASSERT_EQ(global_cfg.num_bytes, (size_t)3);
    ASSERT_EQ(memcmp(global_cfg.file, "abc", 3), 0);
}

int main(void) {
    printf("test_file_buffer:\n");
    RUN_TEST(test_open_file);
    RUN_TEST(test_open_file_sets_numrows);
    RUN_TEST(test_open_empty_file);
    RUN_TEST(test_reload_grows_and_empties_mapping);
    RUN_TEST(test_failed_reload_preserves_mapping);
    RUN_TEST(test_reopen_owned_filename);
    cleanup();
    TEST_REPORT();
}

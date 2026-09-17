#include "lhiew/types.h"
#include "test_harness.h"
#include "lhiew/architecture.h"
#include "lhiew/editor.h"
#include "lhiew/file_buffer.h"
#include "lhiew/hex_edit.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

static int write_calls, sync_calls;
static int fail_write_call, fail_sync, fail_allocation;
static int interrupt_write, interrupt_read, interrupt_sync, short_read, zero_write;
static char test_path[64];
static int test_fd = -1;

ssize_t hex_test_pwrite(int fd, const void *buffer, size_t size, off_t offset) {
    write_calls++;
    if (interrupt_write) {
        interrupt_write = 0;
        errno = EINTR;
        return -1;
    }
    if (write_calls == fail_write_call) {
        errno = ENOSPC;
        return -1;
    }
    if (zero_write) {
        zero_write = 0;
        return 0;
    }
    return pwrite(fd, buffer, size, offset);
}

ssize_t hex_test_pread(int fd, void *buffer, size_t size, off_t offset) {
    if (interrupt_read) {
        interrupt_read = 0;
        errno = EINTR;
        return -1;
    }
    if (short_read) {
        short_read = 0;
        return 0;
    }
    return pread(fd, buffer, size, offset);
}

int hex_test_fsync(int fd) {
    sync_calls++;
    if (interrupt_sync) {
        interrupt_sync = 0;
        errno = EINTR;
        return -1;
    }
    if (fail_sync) {
        errno = EIO;
        return -1;
    }
    return fsync(fd);
}

void *hex_test_realloc(void *pointer, size_t size) {
    if (fail_allocation) {
        errno = ENOMEM;
        return NULL;
    }
    return realloc(pointer, size);
}

static void cleanup(void) {
    if (global_cfg.editing)
        hex_edit_cancel();
    if (global_cfg.file && global_cfg.num_bytes)
        munmap(global_cfg.file, global_cfg.num_bytes);
    if (global_cfg.fp)
        fclose(global_cfg.fp);
    free(global_cfg.filename);
    free(global_cfg.disassembler_buffer);
    if (test_fd >= 0)
        close(test_fd);
    if (test_path[0]) {
        char backup[96];
        snprintf(backup, sizeof(backup), "%s.backup", test_path);
        unlink(backup);
        unlink(test_path);
    }
    test_fd = -1;
    test_path[0] = '\0';
    RESET_GLOBAL_CFG();
    write_calls = sync_calls = fail_write_call = fail_sync = fail_allocation = 0;
    interrupt_write = interrupt_read = interrupt_sync = short_read = zero_write = 0;
}

static int open_bytes(const uint8_t *data, size_t size) {
    cleanup();
    strcpy(test_path, "/tmp/lhiew-hex-edit-XXXXXX");
    test_fd = mkstemp(test_path);
    if (test_fd < 0 || write(test_fd, data, size) != (ssize_t)size)
        return 0;
    global_cfg.cur_screencols = 16;
    global_cfg.screencols = 80;
    global_cfg.screenrows = 10;
    global_cfg.mode = HEX_MODE;
    global_cfg.disassembler_mode = MODE_LONG_COMPAT_32;
    open_file_to_view(test_path);
    return 1;
}

static int disk_byte(size_t offset) {
    uint8_t value;
    return pread(test_fd, &value, 1, (off_t)offset) == 1 ? value : -1;
}

static void test_private_edits_save_only_changed_bytes(void) {
    const uint8_t bytes[] = {0x10, 0x20, 0x30, 0x40};
    ASSERT(open_bytes(bytes, sizeof(bytes)));
    ASSERT(hex_edit_begin());
    ASSERT(global_cfg.editing);
    ASSERT(hex_edit_set_byte(2, 0xab));
    ASSERT_EQ(global_cfg.file[2], 0xab);
    ASSERT_EQ(disk_byte(2), 0x30);
    ASSERT_EQ(hex_edit_dirty_count(), 1u);
    ASSERT(hex_edit_save());
    ASSERT_EQ(disk_byte(2), 0xab);
    ASSERT_EQ(disk_byte(1), 0x20);
    ASSERT_EQ(hex_edit_dirty_count(), 0u);
    ASSERT_EQ(write_calls, 1);
    ASSERT_EQ(sync_calls, 1);
    ASSERT(global_cfg.editing);
    struct stat st;
    ASSERT_EQ(fstat(test_fd, &st), 0);
    ASSERT_EQ(st.st_size, (off_t)sizeof(bytes));
    ASSERT(hex_edit_set_byte(2, 0xcd));
    hex_edit_cancel();
    ASSERT(!global_cfg.editing);
    ASSERT_EQ(global_cfg.file[2], 0xab);
    ASSERT_EQ(disk_byte(2), 0xab);
}

static void test_cancel_never_writes_unsaved_edits(void) {
    const uint8_t bytes[] = {1, 2, 3};
    ASSERT(open_bytes(bytes, sizeof(bytes)));
    ASSERT(hex_edit_begin());
    ASSERT(hex_edit_set_byte(0, 9));
    ASSERT(hex_edit_set_byte(2, 7));
    hex_edit_cancel();
    ASSERT(!global_cfg.editing);
    ASSERT_EQ(hex_edit_dirty_count(), 0u);
    ASSERT(memcmp(global_cfg.file, bytes, sizeof(bytes)) == 0);
    ASSERT_EQ(disk_byte(0), 1);
    ASSERT_EQ(write_calls, 0);
    ASSERT_EQ(sync_calls, 0);
}

static void test_noop_and_reverting_edits_remove_pending_records(void) {
    const uint8_t bytes[] = {1, 2, 3, 4};
    ASSERT(open_bytes(bytes, sizeof(bytes)));
    ASSERT(hex_edit_begin());
    ASSERT(hex_edit_set_byte(3, 4));
    ASSERT_EQ(hex_edit_dirty_count(), 0u);
    ASSERT(hex_edit_set_byte(3, 0xff));
    ASSERT(hex_edit_set_byte(0, 0x10));
    ASSERT(hex_edit_set_byte(2, 0x11));
    ASSERT(hex_edit_set_byte(2, 0x12));
    ASSERT_EQ(hex_edit_dirty_count(), 3u);
    ASSERT(hex_edit_set_byte(2, 3));
    ASSERT(hex_edit_set_byte(0, 1));
    ASSERT(hex_edit_set_byte(3, 4));
    ASSERT_EQ(hex_edit_dirty_count(), 0u);
    ASSERT(hex_edit_save());
    ASSERT_EQ(write_calls, 0);
    ASSERT_EQ(sync_calls, 0);
}

static void test_empty_missing_readonly_and_eof_rejected(void) {
    cleanup();
    ASSERT(!hex_edit_begin());
    ASSERT(!hex_edit_set_byte(0, 1));
    ASSERT(!hex_edit_save());
    ASSERT(open_bytes(NULL, 0));
    ASSERT(!hex_edit_begin());
    const uint8_t bytes[] = {1};
    ASSERT(open_bytes(bytes, sizeof(bytes)));
    ASSERT_EQ(fchmod(test_fd, 0444), 0);
    ASSERT(!hex_edit_begin());
    ASSERT_EQ(fchmod(test_fd, 0600), 0);
    ASSERT(hex_edit_begin());
    ASSERT(!hex_edit_set_byte(1, 2));
    ASSERT(!hex_edit_set_byte(SIZE_MAX, 3));
    ASSERT_EQ(hex_edit_dirty_count(), 0u);
    hex_edit_cancel();
    ASSERT_EQ(unlink(test_path), 0);
    ASSERT(!hex_edit_begin());
}

static void test_allocation_failure_keeps_original_byte(void) {
    const uint8_t bytes[] = {1, 2};
    ASSERT(open_bytes(bytes, sizeof(bytes)));
    ASSERT(hex_edit_begin());
    fail_allocation = 1;
    ASSERT(!hex_edit_set_byte(0, 0xff));
    ASSERT_EQ(global_cfg.file[0], 1);
    ASSERT_EQ(disk_byte(0), 1);
    ASSERT_EQ(hex_edit_dirty_count(), 0u);
    fail_allocation = 0;
    ASSERT(hex_edit_set_byte(0, 0xff));
    ASSERT_EQ(hex_edit_dirty_count(), 1u);
}

static void test_external_write_refuses_save_and_reload_sees_change(void) {
    const uint8_t bytes[] = {1, 2, 3, 4};
    ASSERT(open_bytes(bytes, sizeof(bytes)));
    ASSERT(hex_edit_begin());
    ASSERT(hex_edit_set_byte(0, 9));
    const uint8_t external = 7;
    ASSERT_EQ(pwrite(test_fd, &external, 1, 3), 1);
    /* Force a different timestamp even on filesystems with coarse clocks. */
    const struct timespec times[] = {{123456789, 0}, {123456789, 0}};
    ASSERT_EQ(futimens(test_fd, times), 0);
    ASSERT(!hex_edit_save());
    ASSERT_EQ(write_calls, 0);
    ASSERT_EQ(hex_edit_dirty_count(), 1u);
    ASSERT_EQ(disk_byte(0), 1);
    ASSERT(!hex_edit_set_byte(1, 8));
    hex_edit_cancel();
    ASSERT_EQ(global_cfg.file[0], 1);
    ASSERT_EQ(global_cfg.file[3], 7);
}

static void test_replaced_path_refuses_begin_and_save(void) {
    const uint8_t bytes[] = {1, 2};
    ASSERT(open_bytes(bytes, sizeof(bytes)));
    ASSERT(hex_edit_begin());
    ASSERT(hex_edit_set_byte(0, 9));
    ASSERT_EQ(unlink(test_path), 0);
    int replacement = open(test_path, O_CREAT | O_EXCL | O_WRONLY, 0600);
    ASSERT(replacement >= 0);
    ASSERT_EQ(write(replacement, bytes, sizeof(bytes)), (ssize_t)sizeof(bytes));
    close(replacement);
    ASSERT(!hex_edit_save());
    ASSERT_EQ(write_calls, 0);
    hex_edit_cancel();
    ASSERT(!hex_edit_begin());
}

static void test_truncated_file_refuses_save_and_cancel_reloads_size(void) {
    const uint8_t bytes[] = {1, 2, 3, 4};
    ASSERT(open_bytes(bytes, sizeof(bytes)));
    ASSERT(hex_edit_begin());
    ASSERT(hex_edit_set_byte(3, 9));
    global_cfg.cur_byte = 3;
    ASSERT_EQ(ftruncate(test_fd, 0), 0);
    ASSERT(!hex_edit_save());
    ASSERT(!hex_edit_set_byte(0, 7));
    ASSERT_EQ(write_calls, 0);
    hex_edit_cancel();
    ASSERT(!global_cfg.editing);
    ASSERT_EQ(global_cfg.num_bytes, 0u);
    ASSERT_EQ(global_cfg.cur_byte, 0u);
    ASSERT_EQ(global_cfg.file, NULL);
}

static void test_partial_save_failure_can_retry(void) {
    const uint8_t bytes[] = {1, 2, 3, 4};
    ASSERT(open_bytes(bytes, sizeof(bytes)));
    ASSERT(hex_edit_begin());
    ASSERT(hex_edit_set_byte(3, 9));
    ASSERT(hex_edit_set_byte(0, 8));
    fail_write_call = 2;
    ASSERT(!hex_edit_save());
    ASSERT_EQ(disk_byte(0), 8);
    ASSERT_EQ(disk_byte(3), 4);
    ASSERT_EQ(hex_edit_dirty_count(), 2u);
    ASSERT(strstr(global_cfg.statusmsg, "partly written"));
    fail_write_call = 0;
    ASSERT(hex_edit_save());
    ASSERT_EQ(write_calls, 3);
    ASSERT_EQ(sync_calls, 1);
    ASSERT_EQ(disk_byte(3), 9);
    ASSERT_EQ(hex_edit_dirty_count(), 0u);
}

static void test_cancel_after_partial_save_reloads_actual_disk(void) {
    const uint8_t bytes[] = {1, 2, 3, 4};
    ASSERT(open_bytes(bytes, sizeof(bytes)));
    ASSERT(hex_edit_begin());
    ASSERT(hex_edit_set_byte(0, 8));
    ASSERT(hex_edit_set_byte(3, 9));
    fail_write_call = 2;
    ASSERT(!hex_edit_save());
    hex_edit_cancel();
    ASSERT_EQ(global_cfg.file[0], 8);
    ASSERT_EQ(global_cfg.file[3], 4);
    ASSERT_EQ(hex_edit_dirty_count(), 0u);
    ASSERT_EQ(write_calls, 2);
}

static void test_fsync_failure_preserves_pending_and_retry_syncs(void) {
    const uint8_t bytes[] = {1, 2};
    ASSERT(open_bytes(bytes, sizeof(bytes)));
    ASSERT(hex_edit_begin());
    ASSERT(hex_edit_set_byte(0, 9));
    fail_sync = 1;
    ASSERT(!hex_edit_save());
    ASSERT_EQ(disk_byte(0), 9);
    ASSERT_EQ(hex_edit_dirty_count(), 1u);
    fail_sync = 0;
    ASSERT(hex_edit_set_byte(0, 9));
    ASSERT_EQ(hex_edit_dirty_count(), 1u);
    ASSERT(hex_edit_save());
    ASSERT_EQ(write_calls, 1);
    ASSERT_EQ(sync_calls, 2);
    ASSERT_EQ(hex_edit_dirty_count(), 0u);
}

static void test_interrupted_io_retries_and_short_io_fails(void) {
    const uint8_t bytes[] = {1, 2};
    ASSERT(open_bytes(bytes, sizeof(bytes)));
    ASSERT(hex_edit_begin());
    ASSERT(hex_edit_set_byte(1, 9));
    short_read = 1;
    ASSERT(!hex_edit_save());
    ASSERT_EQ(write_calls, 0);
    zero_write = 1;
    ASSERT(!hex_edit_save());
    ASSERT_EQ(disk_byte(1), 2);
    interrupt_read = interrupt_write = interrupt_sync = 1;
    ASSERT(hex_edit_save());
    ASSERT_EQ(disk_byte(1), 9);
    ASSERT_EQ(hex_edit_dirty_count(), 0u);
    ASSERT_EQ(write_calls, 3);
    ASSERT_EQ(sync_calls, 2);
}

static void test_save_refreshes_detection_and_preserves_manual_profile(void) {
    uint8_t mz[64] = {0};
    mz[0] = 'M'; mz[1] = 'Z';
    mz[2] = 64; mz[4] = 1; mz[8] = 2;
    ASSERT(open_bytes(mz, sizeof(mz)));
    ASSERT_EQ(architecture_binary_info()->format, BINARY_FORMAT_MZ);
    architecture_select(9);
    ASSERT_EQ(global_cfg.architecture, ARCH_AARCH64);
    ASSERT(hex_edit_begin());
    ASSERT(hex_edit_set_byte(0, 0));
    ASSERT(hex_edit_save());
    ASSERT_EQ(architecture_binary_info()->format, BINARY_FORMAT_RAW);
    ASSERT_EQ(global_cfg.architecture, ARCH_AARCH64);
    ASSERT(global_cfg.architecture_manual);
    ASSERT(hex_edit_set_byte(0, 'M'));
    hex_edit_cancel();
    ASSERT_EQ(architecture_binary_info()->format, BINARY_FORMAT_RAW);
    ASSERT_EQ(global_cfg.architecture, ARCH_AARCH64);
    ASSERT(global_cfg.architecture_manual);
}

static void test_sparse_file_edit_beyond_four_gib(void) {
    if (SIZE_MAX <= UINT32_MAX) {
        printf("  SKIP sparse >4GiB mapping on a 32-bit address space\n");
        return;
    }
    const uint8_t first = 0x11;
    ASSERT(open_bytes(&first, 1));
    const uint64_t large_offset = UINT64_C(0x100000000) + 123;
    ASSERT_EQ(ftruncate(test_fd, (off_t)(large_offset + 1)), 0);
    ASSERT(reload_file_in_editor());
    ASSERT_EQ(global_cfg.num_bytes, (size_t)(large_offset + 1));
    ASSERT(hex_edit_begin());
    ASSERT(hex_edit_set_byte((size_t)large_offset, 0xa5));
    ASSERT_EQ(global_cfg.file[(size_t)large_offset], 0xa5);
    ASSERT_EQ(disk_byte((size_t)large_offset), 0);
    ASSERT(hex_edit_save());
    ASSERT_EQ(disk_byte((size_t)large_offset), 0xa5);
    ASSERT_EQ(disk_byte(0), 0x11);
    ASSERT_EQ(write_calls, 1);
    struct stat st;
    ASSERT_EQ(fstat(test_fd, &st), 0);
    ASSERT_EQ(st.st_size, (off_t)(large_offset + 1));
    ASSERT((uint64_t)st.st_blocks * 512 < UINT64_C(16) * 1024 * 1024);
}

static void test_backup_precedes_edits_and_survives_save(void) {
    const uint8_t bytes[] = {0x11, 0x22, 0x33};
    ASSERT(open_bytes(bytes, sizeof(bytes)));
    ASSERT(hex_edit_begin());
    char backup[96];
    snprintf(backup, sizeof(backup), "%s.backup", test_path);
    int fd = open(backup, O_RDONLY);
    ASSERT(fd >= 0);
    uint8_t saved[sizeof(bytes)];
    ASSERT_EQ(read(fd, saved, sizeof(saved)), (ssize_t)sizeof(saved));
    ASSERT_EQ(memcmp(bytes, saved, sizeof(bytes)), 0);
    ASSERT(hex_edit_set_byte(1, 0xaa));
    ASSERT(hex_edit_save());
    ASSERT_EQ(pread(fd, saved, sizeof(saved), 0), (ssize_t)sizeof(saved));
    ASSERT_EQ(memcmp(bytes, saved, sizeof(bytes)), 0);
    close(fd);
}

static void test_failed_backup_refuses_editing_without_mutation(void) {
    const uint8_t bytes[] = {0x11, 0x22};
    ASSERT(open_bytes(bytes, sizeof(bytes)));
    char backup[96];
    snprintf(backup, sizeof(backup), "%s.backup", test_path);
    ASSERT_EQ(mkdir(backup, 0700), 0);
    ASSERT(!hex_edit_begin());
    ASSERT(!global_cfg.editing);
    ASSERT_EQ(hex_edit_dirty_count(), 0u);
    ASSERT_EQ(memcmp(global_cfg.file, bytes, sizeof(bytes)), 0);
    ASSERT_EQ(disk_byte(0), 0x11);
    ASSERT_EQ(write_calls, 0);
    ASSERT_EQ(rmdir(backup), 0);
}

static void test_multi_field_patch_is_atomic_on_invalid_span_and_allocation_failure(void) {
    const uint8_t bytes[] = {1, 2, 3, 4};
    const uint8_t replacements[] = {8, 9};
    ASSERT(open_bytes(bytes, sizeof(bytes)));
    ASSERT(hex_edit_begin());
    hexPatch patches[] = {{0, replacements, 2}, {3, replacements, 2}};
    ASSERT(!hex_edit_patch(patches, 2));
    ASSERT_EQ(memcmp(global_cfg.file, bytes, sizeof(bytes)), 0);
    ASSERT_EQ(hex_edit_dirty_count(), 0u);
    patches[1].offset = 2;
    fail_allocation = 1;
    ASSERT(!hex_edit_patch(patches, 2));
    ASSERT_EQ(memcmp(global_cfg.file, bytes, sizeof(bytes)), 0);
    ASSERT_EQ(hex_edit_dirty_count(), 0u);
    fail_allocation = 0;
    ASSERT(hex_edit_patch(patches, 2));
    ASSERT_EQ(global_cfg.file[0], 8);
    ASSERT_EQ(global_cfg.file[3], 9);
    ASSERT_EQ(hex_edit_dirty_count(), 4u);
    ASSERT_EQ(disk_byte(0), 1);
}

int main(void) {
    printf("test_hex_edit:\n");
    RUN_TEST(test_private_edits_save_only_changed_bytes);
    RUN_TEST(test_cancel_never_writes_unsaved_edits);
    RUN_TEST(test_noop_and_reverting_edits_remove_pending_records);
    RUN_TEST(test_empty_missing_readonly_and_eof_rejected);
    RUN_TEST(test_allocation_failure_keeps_original_byte);
    RUN_TEST(test_external_write_refuses_save_and_reload_sees_change);
    RUN_TEST(test_replaced_path_refuses_begin_and_save);
    RUN_TEST(test_truncated_file_refuses_save_and_cancel_reloads_size);
    RUN_TEST(test_partial_save_failure_can_retry);
    RUN_TEST(test_cancel_after_partial_save_reloads_actual_disk);
    RUN_TEST(test_fsync_failure_preserves_pending_and_retry_syncs);
    RUN_TEST(test_interrupted_io_retries_and_short_io_fails);
    RUN_TEST(test_save_refreshes_detection_and_preserves_manual_profile);
    RUN_TEST(test_sparse_file_edit_beyond_four_gib);
    RUN_TEST(test_backup_precedes_edits_and_survives_save);
    RUN_TEST(test_failed_backup_refuses_editing_without_mutation);
    RUN_TEST(test_multi_field_patch_is_atomic_on_invalid_span_and_allocation_failure);
    cleanup();
    TEST_REPORT();
}

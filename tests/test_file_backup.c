#include "lhiew/types.h"
#include "lhiew/file_backup.h"
#include "test_harness.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

static char directory[64], source_path[128], backup_path[160], moved_path[160];
static int source_fd = -1;

static void cleanup(void) {
    if (source_fd >= 0) close(source_fd);
    source_fd = -1;
    if (directory[0]) {
        DIR *dir = opendir(directory);
        if (dir) {
            struct dirent *entry;
            while ((entry = readdir(dir))) {
                if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
                if (unlinkat(dirfd(dir), entry->d_name, 0) != 0)
                    unlinkat(dirfd(dir), entry->d_name, AT_REMOVEDIR);
            }
            closedir(dir);
        }
        rmdir(directory);
    }
    directory[0] = '\0';
}

static int setup(void) {
    cleanup();
    strcpy(directory, "/tmp/lhiew-backup-XXXXXX");
    if (!mkdtemp(directory)) return 0;
    snprintf(source_path, sizeof(source_path), "%s/original.bin", directory);
    snprintf(backup_path, sizeof(backup_path), "%s.backup", source_path);
    snprintf(moved_path, sizeof(moved_path), "%s/renamed.bin", directory);
    source_fd = open(source_path, O_RDWR | O_CREAT | O_EXCL, 0600);
    return source_fd >= 0;
}

static int temporary_files(void) {
    int count = 0;
    DIR *dir = opendir(directory);
    if (!dir) return -1;
    struct dirent *entry;
    while ((entry = readdir(dir)))
        if (strstr(entry->d_name, ".backup.tmp.")) ++count;
    closedir(dir);
    return count;
}

static void test_backup_exact_bytes_and_file_position(void) {
    ASSERT(setup());
    uint8_t original[131111], copied[sizeof(original)];
    for (size_t i = 0; i < sizeof(original); ++i) original[i] = (uint8_t)(i * 19 + 3);
    ASSERT_EQ(write(source_fd, original, sizeof(original)), (ssize_t)sizeof(original));
    ASSERT_EQ(lseek(source_fd, 123, SEEK_SET), (off_t)123);
    ASSERT(file_backup_ensure(source_path, source_fd));
    ASSERT_EQ(lseek(source_fd, 0, SEEK_CUR), (off_t)123);
    int backup = open(backup_path, O_RDONLY);
    ASSERT(backup >= 0);
    struct stat source, copy;
    ASSERT_EQ(fstat(source_fd, &source), 0);
    ASSERT_EQ(fstat(backup, &copy), 0);
    ASSERT(source.st_dev != copy.st_dev || source.st_ino != copy.st_ino);
    ASSERT_EQ(copy.st_size, source.st_size);
    ASSERT_EQ(copy.st_mode & 0777, 0600);
    ASSERT_EQ(read(backup, copied, sizeof(copied)), (ssize_t)sizeof(copied));
    ASSERT_EQ(memcmp(copied, original, sizeof(original)), 0);
    close(backup);
    ASSERT_EQ(temporary_files(), 0);
}

static void test_backup_keeps_first_original_across_sessions(void) {
    ASSERT(setup());
    ASSERT_EQ(write(source_fd, "original", 8), 8);
    ASSERT(file_backup_ensure(source_path, source_fd));
    ASSERT_EQ(pwrite(source_fd, "modified", 8, 0), 8);
    ASSERT(file_backup_ensure(source_path, source_fd));
    close(source_fd);
    source_fd = open(source_path, O_RDWR);
    ASSERT(source_fd >= 0);
    ASSERT(file_backup_ensure(source_path, source_fd));
    int backup = open(backup_path, O_RDONLY);
    ASSERT(backup >= 0);
    char bytes[8];
    ASSERT_EQ(read(backup, bytes, sizeof(bytes)), 8);
    ASSERT_EQ(memcmp(bytes, "original", 8), 0);
    close(backup);
    ASSERT_EQ(temporary_files(), 0);
}

static void test_backup_rejects_aliases_and_nonregular_destinations(void) {
    ASSERT(setup());
    ASSERT_EQ(write(source_fd, "source", 6), 6);
    ASSERT_EQ(symlink(source_path, backup_path), 0);
    ASSERT(!file_backup_ensure(source_path, source_fd));
    ASSERT_EQ(unlink(backup_path), 0);
    ASSERT_EQ(link(source_path, backup_path), 0);
    ASSERT(!file_backup_ensure(source_path, source_fd));
    ASSERT_EQ(unlink(backup_path), 0);
    ASSERT_EQ(mkfifo(backup_path, 0600), 0);
    ASSERT(!file_backup_ensure(source_path, source_fd));
    ASSERT_EQ(unlink(backup_path), 0);
    ASSERT_EQ(mkdir(backup_path, 0700), 0);
    ASSERT(!file_backup_ensure(source_path, source_fd));
    ASSERT_EQ(rmdir(backup_path), 0);
    char bytes[6];
    ASSERT_EQ(pread(source_fd, bytes, sizeof(bytes), 0), 6);
    ASSERT_EQ(memcmp(bytes, "source", 6), 0);
    ASSERT_EQ(temporary_files(), 0);
}

static void test_backup_read_failure_never_publishes_partial_copy(void) {
    ASSERT(setup());
    ASSERT_EQ(write(source_fd, "source", 6), 6);
    int write_only = open(source_path, O_WRONLY);
    ASSERT(write_only >= 0);
    ASSERT(!file_backup_ensure(source_path, write_only));
    ASSERT_EQ(errno, EBADF);
    close(write_only);
    ASSERT_EQ(access(backup_path, F_OK), -1);
    ASSERT_EQ(temporary_files(), 0);
}

static void test_backup_refuses_replaced_source_path(void) {
    ASSERT(setup());
    ASSERT_EQ(write(source_fd, "first", 5), 5);
    ASSERT_EQ(rename(source_path, moved_path), 0);
    int other = open(source_path, O_RDWR | O_CREAT | O_EXCL, 0600);
    ASSERT(other >= 0);
    ASSERT_EQ(write(other, "other", 5), 5);
    close(other);
    ASSERT(!file_backup_ensure(source_path, source_fd));
    ASSERT_EQ(errno, EBUSY);
    ASSERT_EQ(access(backup_path, F_OK), -1);
    ASSERT_EQ(temporary_files(), 0);
}

static void test_backup_sparse_file_beyond_four_gib(void) {
    if (sizeof(off_t) < 8) {
        printf("  SKIP sparse backup with 32-bit file offsets\n");
        return;
    }
    ASSERT(setup());
    off_t end = (off_t)(UINT64_C(0x100000000) + 123);
    ASSERT_EQ(ftruncate(source_fd, end + 1), 0);
    ASSERT_EQ(pwrite(source_fd, "HEAD", 4, 0), 4);
    ASSERT_EQ(pwrite(source_fd, "Z", 1, end), 1);
    ASSERT(file_backup_ensure(source_path, source_fd));
    int backup = open(backup_path, O_RDONLY);
    ASSERT(backup >= 0);
    struct stat st;
    ASSERT_EQ(fstat(backup, &st), 0);
    ASSERT_EQ(st.st_size, end + 1);
    ASSERT((uint64_t)st.st_blocks * 512 < UINT64_C(16) * 1024 * 1024);
    char bytes[4];
    ASSERT_EQ(pread(backup, bytes, sizeof(bytes), 0), 4);
    ASSERT_EQ(memcmp(bytes, "HEAD", 4), 0);
    ASSERT_EQ(pread(backup, bytes, 1, end), 1);
    ASSERT_EQ(bytes[0], 'Z');
    ASSERT_EQ(pread(backup, bytes, 1, end / 2), 1);
    ASSERT_EQ(bytes[0], 0);
    close(backup);
    ASSERT_EQ(temporary_files(), 0);
}

static void test_backup_empty_source_and_invalid_arguments(void) {
    ASSERT(setup());
    ASSERT(file_backup_ensure(source_path, source_fd));
    struct stat st;
    ASSERT_EQ(stat(backup_path, &st), 0);
    ASSERT_EQ(st.st_size, (off_t)0);
    ASSERT(!file_backup_ensure(NULL, source_fd));
    ASSERT(!file_backup_ensure(source_path, -1));
}

static void test_backup_rejects_nonregular_source_and_dangling_destination(void) {
    ASSERT(setup());
    int device = open("/dev/null", O_RDONLY);
    ASSERT(device >= 0);
    ASSERT(!file_backup_ensure("/dev/null", device));
    ASSERT_EQ(errno, EINVAL);
    close(device);
    ASSERT_EQ(symlink(moved_path, backup_path), 0);
    ASSERT(!file_backup_ensure(source_path, source_fd));
    ASSERT_EQ(errno, EINVAL);
    struct stat st;
    ASSERT_EQ(lstat(backup_path, &st), 0);
    ASSERT(S_ISLNK(st.st_mode));
    ASSERT_EQ(temporary_files(), 0);
}

static void test_backup_preserves_all_zero_file_size(void) {
    ASSERT(setup());
    off_t size = 64 * 1024 + 7;
    ASSERT_EQ(ftruncate(source_fd, size), 0);
    ASSERT(file_backup_ensure(source_path, source_fd));
    int backup = open(backup_path, O_RDONLY);
    ASSERT(backup >= 0);
    struct stat st;
    ASSERT_EQ(fstat(backup, &st), 0);
    ASSERT_EQ(st.st_size, size);
    unsigned char last = 1;
    ASSERT_EQ(pread(backup, &last, 1, size - 1), 1);
    ASSERT_EQ(last, 0);
    ASSERT_EQ(pread(backup, &last, 1, size), 0);
    close(backup);
    ASSERT_EQ(temporary_files(), 0);
}

int main(void) {
    RUN_TEST(test_backup_exact_bytes_and_file_position);
    RUN_TEST(test_backup_keeps_first_original_across_sessions);
    RUN_TEST(test_backup_rejects_aliases_and_nonregular_destinations);
    RUN_TEST(test_backup_read_failure_never_publishes_partial_copy);
    RUN_TEST(test_backup_refuses_replaced_source_path);
    RUN_TEST(test_backup_sparse_file_beyond_four_gib);
    RUN_TEST(test_backup_empty_source_and_invalid_arguments);
    RUN_TEST(test_backup_rejects_nonregular_source_and_dangling_destination);
    RUN_TEST(test_backup_preserves_all_zero_file_size);
    cleanup();
    TEST_REPORT();
}

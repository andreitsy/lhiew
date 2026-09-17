#include "lhiew/types.h"
#include "lhiew/file_backup.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int backup_same_identity(const struct stat *a, const struct stat *b) {
    return a->st_dev == b->st_dev && a->st_ino == b->st_ino;
}

static int backup_same_version(const struct stat *a, const struct stat *b) {
    if (!backup_same_identity(a, b) || a->st_size != b->st_size || !S_ISREG(b->st_mode))
        return 0;
#ifdef __APPLE__
    return a->st_mtimespec.tv_sec == b->st_mtimespec.tv_sec &&
           a->st_mtimespec.tv_nsec == b->st_mtimespec.tv_nsec &&
           a->st_ctimespec.tv_sec == b->st_ctimespec.tv_sec &&
           a->st_ctimespec.tv_nsec == b->st_ctimespec.tv_nsec;
#else
    return a->st_mtim.tv_sec == b->st_mtim.tv_sec &&
           a->st_mtim.tv_nsec == b->st_mtim.tv_nsec &&
           a->st_ctim.tv_sec == b->st_ctim.tv_sec &&
           a->st_ctim.tv_nsec == b->st_ctim.tv_nsec;
#endif
}

static int backup_source_unchanged(const char *filename, int fd, const struct stat *before) {
    struct stat current, named;
    if (fstat(fd, &current) != 0 || stat(filename, &named) != 0) return 0;
    if (!backup_same_version(before, &current) || !backup_same_version(before, &named)) {
        errno = EBUSY;
        return 0;
    }
    return 1;
}

static int backup_sync_parent(const char *path) {
    char *directory = strdup(path);
    if (!directory) return 0;
    char *slash = strrchr(directory, '/');
    if (!slash) strcpy(directory, ".");
    else if (slash == directory) slash[1] = '\0';
    else *slash = '\0';
    int fd = open(directory, O_RDONLY | O_CLOEXEC | O_DIRECTORY);
    int error = errno;
    free(directory);
    if (fd < 0) { errno = error; return 0; }
    int result;
    do { result = fsync(fd); } while (result < 0 && errno == EINTR);
    error = errno;
    close(fd);
    errno = error;
    return result == 0;
}

/* Return 1 for a safe existing backup, 0 if absent, -1 if unsafe/inaccessible. */
static int backup_existing(const char *path, const struct stat *source) {
    struct stat named, opened;
    if (lstat(path, &named) != 0) return errno == ENOENT ? 0 : -1;
    if (!S_ISREG(named.st_mode) || backup_same_identity(source, &named)) {
        errno = EINVAL;
        return -1;
    }
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0) return -1;
    int success = fstat(fd, &opened) == 0;
    if (success && (!S_ISREG(opened.st_mode) || !backup_same_identity(&named, &opened) ||
                    backup_same_identity(source, &opened))) {
        errno = EINVAL;
        success = 0;
    }
    int error = errno;
    close(fd);
    errno = error;
    return success ? 1 : -1;
}

static int backup_write(int fd, const uint8_t *buffer, size_t length, off_t offset) {
    while (length) {
        ssize_t written = pwrite(fd, buffer, length, offset);
        if (written < 0 && errno == EINTR) continue;
        if (written <= 0) {
            if (!written) errno = EIO;
            return 0;
        }
        buffer += written;
        length -= (size_t)written;
        offset += written;
    }
    return 1;
}

static int backup_copy_range(int source, int target, off_t start, off_t end) {
    uint8_t buffer[64 * 1024];
    while (start < end) {
        size_t request = (uintmax_t)(end - start) > sizeof(buffer) ? sizeof(buffer) : (size_t)(end - start);
        ssize_t count = pread(source, buffer, request, start);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) {
            if (!count) errno = EIO;
            return 0;
        }
        /* The fallback also preserves large runs of zero-filled holes. */
        size_t i = 0;
        while (i < (size_t)count && !buffer[i]) ++i;
        if (i < (size_t)count && !backup_write(target, buffer, (size_t)count, start)) return 0;
        start += count;
    }
    return 1;
}

static int backup_copy(int source, int target, off_t size) {
#if defined(SEEK_DATA) && defined(SEEK_HOLE)
    off_t cursor = 0;
    while (cursor < size) {
        off_t data = lseek(source, cursor, SEEK_DATA);
        if (data < 0) {
            if (errno == ENXIO) return 1;
            if (errno == EINVAL || errno == ENOTSUP || errno == ENOSYS) break;
            return 0;
        }
        if (data >= size) return 1;
        if (data < cursor) { errno = EIO; return 0; }
        off_t hole = lseek(source, data, SEEK_HOLE);
        if (hole < 0) {
            if (errno == EINVAL || errno == ENOTSUP || errno == ENOSYS) break;
            return 0;
        }
        if (hole <= data) { errno = EIO; return 0; }
        if (hole > size) hole = size;
        if (!backup_copy_range(source, target, data, hole)) return 0;
        cursor = hole;
    }
    if (cursor == size) return 1;
#endif
    return backup_copy_range(source, target, 0, size);
}

int file_backup_ensure(const char *filename, int source_fd) {
    struct stat source;
    if (!filename || source_fd < 0) { errno = EINVAL; return 0; }
    if (fstat(source_fd, &source) != 0) return 0;
    if (!S_ISREG(source.st_mode) || source.st_size < 0) { errno = EINVAL; return 0; }
    if (!backup_source_unchanged(filename, source_fd, &source)) return 0;
    size_t length = strlen(filename);
    if (length > SIZE_MAX - 32) { errno = ENAMETOOLONG; return 0; }
    char *path = malloc(length + sizeof(".backup"));
    char *temporary = malloc(length + sizeof(".backup.tmp.XXXXXX"));
    if (!path || !temporary) {
        free(path);
        free(temporary);
        errno = ENOMEM;
        return 0;
    }
    memcpy(path, filename, length);
    memcpy(path + length, ".backup", sizeof(".backup"));
    memcpy(temporary, filename, length);
    memcpy(temporary + length, ".backup.tmp.XXXXXX", sizeof(".backup.tmp.XXXXXX"));
    int existing = backup_existing(path, &source);
    if (existing) {
        int success = existing > 0 && backup_sync_parent(path);
        int error = errno;
        free(path);
        free(temporary);
        errno = error;
        return success;
    }
    int target = mkstemp(temporary);
    if (target < 0) {
        int error = errno;
        free(path);
        free(temporary);
        errno = error;
        return 0;
    }
    (void)fcntl(target, F_SETFD, FD_CLOEXEC);
    off_t original_position = lseek(source_fd, 0, SEEK_CUR);
    int success = ftruncate(target, source.st_size) == 0 &&
                  backup_copy(source_fd, target, source.st_size);
    if (success) {
        int result;
        do { result = fsync(target); } while (result < 0 && errno == EINTR);
        success = result == 0 && backup_source_unchanged(filename, source_fd, &source);
    }
    if (success && link(temporary, path) != 0) {
        success = errno == EEXIST && backup_existing(path, &source) > 0;
    }
    int error = errno;
    if (original_position >= 0) (void)lseek(source_fd, original_position, SEEK_SET);
    close(target);
    unlink(temporary);
    if (success && !backup_sync_parent(path)) {
        success = 0;
        error = errno;
    }
    free(path);
    free(temporary);
    errno = error;
    return success;
}

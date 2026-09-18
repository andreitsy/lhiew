#include "lhiew/types.h"
#include "file_state.h"
#include "lhiew/hex_edit.h"
#include "lhiew/architecture.h"
#include "lhiew/editor.h"
#include "lhiew/file_buffer.h"
#include "lhiew/file_backup.h"
#include "lhiew/render.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

/* Test builds substitute calls after system declarations, preserving libc's
   _FILE_OFFSET_BITS redirection of the real pread/pwrite entry points. */
#ifndef HEX_PWRITE
#define HEX_PWRITE pwrite
#else
ssize_t HEX_PWRITE(int fd, const void *buffer, size_t size, off_t offset);
#endif
#ifndef HEX_PREAD
#define HEX_PREAD pread
#else
ssize_t HEX_PREAD(int fd, void *buffer, size_t size, off_t offset);
#endif
#ifndef HEX_FSYNC
#define HEX_FSYNC fsync
#else
int HEX_FSYNC(int fd);
#endif
#ifndef HEX_REALLOC
#define HEX_REALLOC realloc
#else
void *HEX_REALLOC(void *pointer, size_t size);
#endif

typedef struct byteChange {
    size_t offset;
    uint8_t before;
    uint8_t after;
    int written;
} byteChange;

static byteChange *changes;
static size_t change_count;
static size_t change_capacity;
static int edit_fd = -1;
static struct stat baseline;

static int validate_file(void) {
    struct stat current, named;
    if (edit_fd < 0 || !global_cfg.filename ||
        fstat(edit_fd, &current) != 0 || stat(global_cfg.filename, &named) != 0) {
        editor_set_status_message("Cannot access edited file: %s", strerror(errno));
        return 0;
    }
    if (!file_same_version(&baseline, &current) || !file_same_extent(&current, &named)) {
        editor_set_status_message("File changed externally; discard edits and reopen");
        return 0;
    }
    return 1;
}

int hex_edit_check_file(void) {
    return !global_cfg.editing || validate_file();
}

static void refresh_metadata(void) {
    int manual = global_cfg.architecture_manual;
    architectureSpec spec = {global_cfg.architecture, global_cfg.disassembler_mode,
                             global_cfg.big_endian};
    architecture_detect_file();
    if (manual) {
        global_cfg.architecture = spec.id;
        global_cfg.disassembler_mode = spec.x86_mode;
        global_cfg.big_endian = spec.big_endian;
        global_cfg.architecture_manual = 1;
    }
    switch_mode();
}

int hex_edit_begin(void) {
    if (global_cfg.editing)
        return 1;
    if (!global_cfg.fp || !global_cfg.filename || !global_cfg.file ||
        !global_cfg.num_bytes) {
        editor_set_status_message("Open a non-empty regular file to edit");
        return 0;
    }
    struct stat viewed, opened;
    if (fstat(fileno(global_cfg.fp), &viewed) != 0) {
        editor_set_status_message("Cannot inspect file: %s", strerror(errno));
        return 0;
    }
    if (!S_ISREG(viewed.st_mode) || viewed.st_size < 0 ||
        (uintmax_t)viewed.st_size != global_cfg.num_bytes) {
        editor_set_status_message("File size changed; reopen before editing");
        return 0;
    }
    /* Respect an explicitly read-only file even when run with elevated rights. */
    if (!(viewed.st_mode & (S_IWUSR | S_IWGRP | S_IWOTH))) {
        editor_set_status_message("File is read-only");
        return 0;
    }
    int fd = open(global_cfg.filename, O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        editor_set_status_message("Cannot edit file: %s", strerror(errno));
        return 0;
    }
    if (fstat(fd, &opened) != 0 || !file_same_version(&viewed, &opened)) {
        close(fd);
        editor_set_status_message("File changed; reopen before editing");
        return 0;
    }
    if (!file_backup_ensure(global_cfg.filename, fd)) {
        int saved_errno = errno;
        close(fd);
        editor_set_status_message("Cannot create safe .backup: %s; editing refused", strerror(saved_errno));
        return 0;
    }
    struct stat after_backup, named;
    if (fstat(fd, &after_backup) != 0 || stat(global_cfg.filename, &named) != 0 ||
        !file_same_version(&opened, &after_backup) || !file_same_extent(&after_backup, &named)) {
        close(fd);
        editor_set_status_message("File changed during backup; reopen before editing");
        return 0;
    }
    if (mprotect(global_cfg.file, global_cfg.num_bytes, PROT_READ | PROT_WRITE) != 0) {
        int saved_errno = errno;
        close(fd);
        editor_set_status_message("Cannot create edit view: %s", strerror(saved_errno));
        return 0;
    }
    edit_fd = fd;
    baseline = opened;
    global_cfg.editing = 1;
    global_cfg.edit_ascii = 0;
    global_cfg.edit_nibble = 0;
    editor_set_status_message("Edit: Tab hex/text, F9 save, Esc exit");
    return 1;
}

static size_t find_change(size_t offset) {
    size_t first = 0, last = change_count;
    while (first < last) {
        size_t middle = first + (last - first) / 2;
        if (changes[middle].offset < offset)
            first = middle + 1;
        else
            last = middle;
    }
    return first;
}

static int reserve_changes(size_t needed) {
    if (needed <= change_capacity)
        return 1;
    size_t limit = SIZE_MAX / sizeof(*changes);
    if (needed > limit) {
        editor_set_status_message("Too many pending edits");
        return 0;
    }
    size_t capacity = change_capacity ? change_capacity : 64;
    while (capacity < needed)
        capacity = capacity > limit / 2 ? limit : capacity * 2;
    byteChange *replacement = HEX_REALLOC(changes, capacity * sizeof(*changes));
    if (!replacement) {
        editor_set_status_message("Not enough memory for pending edits");
        return 0;
    }
    changes = replacement;
    change_capacity = capacity;
    return 1;
}

static int stage_byte(size_t offset, uint8_t value) {
    size_t index = find_change(offset);
    if (index < change_count && changes[index].offset == offset) {
        changes[index].after = value;
        global_cfg.file[offset] = value;
        if (value == changes[index].before && !changes[index].written) {
            memmove(changes + index, changes + index + 1,
                    (change_count - index - 1) * sizeof(*changes));
            change_count--;
        }
        return 1;
    }
    uint8_t before = global_cfg.file[offset];
    if (before == value)
        return 1;
    if (!reserve_changes(change_count + 1))
        return 0;
    memmove(changes + index + 1, changes + index,
            (change_count - index) * sizeof(*changes));
    changes[index] = (byteChange){offset, before, value, 0};
    change_count++;
    global_cfg.file[offset] = value;
    return 1;
}

int hex_edit_set_byte(size_t offset, uint8_t value) {
    if (!global_cfg.editing || edit_fd < 0) {
        editor_set_status_message("Enter hex editing first");
        return 0;
    }
    if (offset >= global_cfg.num_bytes) {
        editor_set_status_message("End of file: overwrite existing bytes only");
        return 0;
    }
    if (!validate_file()) return 0;
    return stage_byte(offset, value);
}

int hex_edit_patch(const hexPatch *patches, size_t count) {
    if (!global_cfg.editing || edit_fd < 0 || !validate_file()) return 0;
    if (count && !patches) {
        editor_set_status_message("Missing edit spans");
        return 0;
    }
    size_t reserve = change_count;
    for (size_t i = 0; i < count; ++i) {
        const hexPatch *patch = &patches[i];
        if ((!patch->bytes && patch->length) || patch->offset > global_cfg.num_bytes ||
            patch->length > global_cfg.num_bytes - patch->offset ||
            patch->length > SIZE_MAX - reserve) {
            editor_set_status_message("Invalid edit span");
            return 0;
        }
        reserve += patch->length;
    }
    if (!reserve_changes(reserve))
        return 0;
    /* All bounds and allocations precede mutation. stage_byte cannot allocate
       or fail once the worst-case number of records has been reserved. */
    for (size_t i = 0; i < count; ++i) {
        for (size_t j = 0; j < patches[i].length; ++j)
            stage_byte(patches[i].offset + j, patches[i].bytes[j]);
    }
    return 1;
}

static int save_error(int error) {
    /* Keep each record, including successful writes, until fsync succeeds. Its
       before value now records the bytes our writes actually put on disk, so a
       retry can validate them. Reloading cannot roll back these disk writes. */
    struct stat current;
    if (fstat(edit_fd, &current) == 0 && file_same_extent(&baseline, &current))
        baseline = current;
    editor_set_status_message("Save failed: %s; disk may be partly written", strerror(error));
    return 0;
}

int hex_edit_save(void) {
    if (!global_cfg.editing || edit_fd < 0) {
        editor_set_status_message("Enter hex editing first");
        return 0;
    }
    if (!validate_file())
        return 0;
    if (!change_count) {
        editor_set_status_message("No pending changes");
        return 1;
    }
    /* Check all expected bytes before the first write. The timestamp check also
       detects unrelated changes; this is conflict detection, not file locking. */
    for (size_t i = 0; i < change_count; ++i) {
        uint8_t actual;
        ssize_t count;
        do {
            count = HEX_PREAD(edit_fd, &actual, 1, (off_t)changes[i].offset);
        } while (count < 0 && errno == EINTR);
        if (count != 1) {
            editor_set_status_message("Cannot verify edited bytes: %s",
                                      strerror(count < 0 ? errno : EIO));
            return 0;
        }
        if (actual != changes[i].before) {
            editor_set_status_message("File changed externally; discard edits and reopen");
            return 0;
        }
    }
    if (!validate_file())
        return 0;
    for (size_t i = 0; i < change_count; ++i) {
        byteChange *change = &changes[i];
        if (change->before == change->after)
            continue;
        ssize_t count;
        do {
            count = HEX_PWRITE(edit_fd, &change->after, 1, (off_t)change->offset);
        } while (count < 0 && errno == EINTR);
        if (count != 1)
            return save_error(count < 0 ? errno : EIO);
        change->before = change->after;
        change->written = 1;
    }
    int result;
    do {
        result = HEX_FSYNC(edit_fd);
    } while (result < 0 && errno == EINTR);
    if (result < 0)
        return save_error(errno);
    struct stat current;
    if (fstat(edit_fd, &current) != 0)
        return save_error(errno);
    if (!file_same_extent(&baseline, &current)) {
        editor_set_status_message("File changed during save; pending edits retained");
        return 0;
    }
    size_t saved_count = change_count;
    baseline = current;
    change_count = 0;
    refresh_metadata();
    editor_set_status_message("Saved %zu byte%s", saved_count, saved_count == 1 ? "" : "s");
    return 1;
}

void hex_edit_cancel(void) {
    if (!global_cfg.editing)
        return;
    if (!reload_file_in_editor()) {
        editor_set_status_message("Cannot reload file: %s", strerror(errno));
        return;
    }
    close(edit_fd);
    edit_fd = -1;
    free(changes);
    changes = NULL;
    change_count = 0;
    change_capacity = 0;
    global_cfg.editing = 0;
    global_cfg.edit_ascii = 0;
    global_cfg.edit_nibble = 0;
    refresh_metadata();
    editor_set_status_message("Edit closed; showing current file contents");
}

size_t hex_edit_dirty_count(void) {
    return change_count;
}

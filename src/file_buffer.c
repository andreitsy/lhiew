#include "lhiew/types.h"
#include "lhiew/file_buffer.h"
#include "lhiew/architecture.h"
#include "lhiew/terminal.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>

static int file_size(const struct stat *st, size_t *size) {
    if (!S_ISREG(st->st_mode)) {
        errno = EINVAL;
        return 0;
    }
    if (st->st_size < 0 || (uintmax_t)st->st_size > SIZE_MAX) {
        errno = EOVERFLOW;
        return 0;
    }
    *size = (size_t)st->st_size;
    return 1;
}

int reload_file_in_editor(void) {
    struct stat st;
    size_t size;
    if (!global_cfg.fp) {
        errno = EBADF;
        return 0;
    }
    int fd = fileno(global_cfg.fp);
    if (fstat(fd, &st) != 0 || !file_size(&st, &size))
        return 0;
    uint8_t *mapping = NULL;
    if (size) {
        mapping = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
        if (mapping == MAP_FAILED)
            return 0;
    }
    if (global_cfg.file && global_cfg.num_bytes)
        munmap(global_cfg.file, global_cfg.num_bytes);
    global_cfg.file = mapping;
    global_cfg.num_bytes = size;
    return 1;
}

void read_file_in_editor(FILE *f_in) {
    struct stat stbuf;
    int fd = fileno(f_in);
    if (fstat(fd, &stbuf) != 0)
        die_safely("fstat");
    if (!file_size(&stbuf, &global_cfg.num_bytes)) {
        /* Devices and directories report st_size 0, which would otherwise be
           indistinguishable from an empty file. perror needs errno set. */
        die_safely("Invalid file type or size");
    }
    if (!global_cfg.num_bytes) {
        global_cfg.file = NULL;
        global_cfg.numrows = 0;
        architecture_detect_file();
        return;
    }
    global_cfg.file = mmap(NULL, global_cfg.num_bytes,
                           PROT_READ, MAP_PRIVATE, fd, 0);
    global_cfg.numrows = global_cfg.num_bytes / global_cfg.cur_screencols + 1;
    if (global_cfg.file == MAP_FAILED) {
        die_safely("Map to memory is failed -> read_file_in_editor");
    }
    architecture_detect_file();
}

void open_file_to_view(char *filename) {
    free(global_cfg.filename);
    global_cfg.filename = strdup(filename);
    global_cfg.fp = fopen(filename, "rb");
    if (!global_cfg.fp)
        die_safely("fopen");
    read_file_in_editor(global_cfg.fp);
}

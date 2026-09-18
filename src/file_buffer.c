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

static int map_file(FILE *stream) {
    struct stat st;
    size_t size;
    if (!stream) {
        errno = EBADF;
        return 0;
    }
    int fd = fileno(stream);
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

int reload_file_in_editor(void) {
    return map_file(global_cfg.fp);
}

void read_file_in_editor(FILE *f_in) {
    if (!map_file(f_in))
        die_safely("Cannot map regular file");
    size_t width = global_cfg.cur_screencols ? global_cfg.cur_screencols : 1;
    global_cfg.numrows = global_cfg.num_bytes ? global_cfg.num_bytes / width + 1 : 0;
    architecture_detect_file();
}

void open_file_to_view(const char *filename) {
    char *name = strdup(filename);
    if (!name)
        die_safely("filename");
    FILE *stream = fopen(filename, "rb");
    if (!stream)
        die_safely("fopen");
    read_file_in_editor(stream);
    if (global_cfg.fp)
        fclose(global_cfg.fp);
    free(global_cfg.filename);
    global_cfg.filename = name;
    global_cfg.fp = stream;
}

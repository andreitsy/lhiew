#include "lhiew/types.h"
#include "lhiew/file_buffer.h"
#include "lhiew/terminal.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>

void read_file_in_editor(FILE *f_in) {
    struct stat stbuf;
    int fd = fileno(f_in);
    if (fstat(fd, &stbuf) != 0)
        die_safely("fstat");
    if (!S_ISREG(stbuf.st_mode)) {
        /* Devices and directories report st_size 0, which would otherwise be
           indistinguishable from an empty file. perror needs errno set. */
        errno = EINVAL;
        die_safely("Not a regular file");
    }
    global_cfg.num_bytes = stbuf.st_size;
    if (!global_cfg.num_bytes) {
        global_cfg.file = NULL;
        global_cfg.numrows = 0;
        return;
    }
    global_cfg.file = mmap(NULL, global_cfg.num_bytes,
                           PROT_READ, MAP_PRIVATE, fd, 0);
    global_cfg.numrows = global_cfg.num_bytes / global_cfg.cur_screencols + 1;
    if (global_cfg.file == MAP_FAILED) {
        die_safely("Map to memory is failed -> read_file_in_editor");
    }
}

void open_file_to_view(char *filename) {
    free(global_cfg.filename);
    global_cfg.filename = strdup(filename);
    global_cfg.fp = fopen(filename, "rb");
    if (!global_cfg.fp)
        die_safely("fopen");
    read_file_in_editor(global_cfg.fp);
}

#ifndef OTUS_PROJECT_EDITOR_H
#define OTUS_PROJECT_EDITOR_H
#include "data.h"
#include <ctype.h>
#include <errno.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>

#define TAB_STOP 4
#define SCREENCOLS_MIN 80

size_t get_row_len(void);
void die_safely(const char *s);
void switch_mode(void);
int get_cursor_position(size_t *rows, size_t *cols);
int get_window_size(size_t *rows, size_t *cols);
void read_file_in_editor(FILE *f_in);

int editor_read_key(void);
void disable_raw_mode(void);
void enable_raw_mode(void);
void free_dissasembled_buffer(void);
void open_file_to_view(char *filename);

/*** append buffer ***/
typedef struct append_buffer {
    char *buffer;
    size_t len;
} append_buffer;
#define ABUF_INIT {NULL, 0}

enum editorKey {
    ARROW_LEFT = 1001,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN,
    PAGE_UP,
    PAGE_DOWN,
    DEL_KEY,
};
void append_to_buffer(append_buffer *ab, const char *s, size_t len);
void free_append_buffer(append_buffer *ab);
void init_editor(void);

#endif //OTUS_PROJECT_EDITOR_H

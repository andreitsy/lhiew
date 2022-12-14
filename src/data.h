#ifndef OTUS_PROJECT_DATA_H
#define OTUS_PROJECT_DATA_H

// Feature test macro to able to work with getline
#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <termios.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <inttypes.h>
#include <time.h>

#define HEX_BYTE_LENGTH 16
#define DISSASMBLED_BUFFER_SIZE 128

typedef enum editorMode {
    TEXT_MODE = 0,
    HEX_MODE,
    DISSASEMBLER_MODE,
} editorMode;

typedef enum dissasemblerMode {
    REAL = 0,
    MODE_LONG_COMPAT_16,
    MODE_LONG_COMPAT_32,
    MODE_LONG_COMPAT_64,
} dissasemblerMode;

typedef struct editorRow {
    size_t size;
    char *chars;
} editorRow;

typedef struct dissasemblerRow {
    size_t start_byte;
    size_t end_byte;
    char diss_str[DISSASMBLED_BUFFER_SIZE];
} dissasemblerRow;

typedef struct editorConfig {
    size_t cx;
    size_t cy;
    size_t rx;
    size_t rowoff;
    size_t coloff;
    size_t screenrows;
    size_t screencols;
    size_t cur_screencols;
    size_t numrows;
    size_t num_bytes;
    size_t cur_byte;
    editorMode mode;
    dissasemblerMode dissasembler_mode;
    uint8_t* file;
    time_t statusmsg_time;
    FILE* fp;
    char* filename;
    char statusmsg[80];
    dissasemblerRow* dissasembler_buffer;
    struct termios orig_termios;
} editorConfig;

extern editorConfig global_cfg;

#define EDITOR_VERSION "0.0.1"
#define HELLO_MESSAGE "Help: Ctrl-q = Quit, Ctrl-m = Prev Mode, m = Next Mode, o - Next OpSize"
#define TEXT_MODE_STR "Text Mode"
#define HEX_MODE_STR "Hex Mode"
#define DISSASEMBLER_MODE_STR "Disassembler Mode"

#endif //OTUS_PROJECT_DATA_H

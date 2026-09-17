#pragma once

#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <time.h>

#define HEX_BYTE_LENGTH    16
#define DISASSEMBLED_BUFFER_SIZE 128
#define SCREENCOLS_MIN     24
#define SCREENROWS_MIN     5

#define EDITOR_VERSION     "0.0.1"
#define HELLO_MESSAGE      "Help: Ctrl-q = Quit, Ctrl-m = Prev Mode, m = Next Mode, o - Next OpSize"
#define TEXT_MODE_STR      "Text Mode"
#define HEX_MODE_STR       "Hex Mode"
#define DISASSEMBLER_MODE_STR "Disassembler Mode"

typedef enum editorMode {
    TEXT_MODE = 0,
    HEX_MODE,
    DISASSEMBLER_MODE,
} editorMode;

typedef enum disassemblerMode {
    REAL = 0,
    MODE_LONG_COMPAT_16,
    MODE_LONG_COMPAT_32,
    MODE_LONG_COMPAT_64,
} disassemblerMode;

typedef enum architectureId {
    ARCH_X86 = 0,
    ARCH_ARM,
    ARCH_THUMB,
    ARCH_AARCH64,
    ARCH_MIPS32,
    ARCH_MIPS64,
    ARCH_PPC32,
    ARCH_PPC64,
    ARCH_SPARC32,
    ARCH_SPARC64,
    ARCH_SYSTEMZ,
    ARCH_M68K,
    ARCH_RISCV32,
    ARCH_RISCV64,
    ARCH_EBPF,
    ARCH_SH,
    ARCH_TRICORE,
    ARCH_XCORE,
    ARCH_TMS320C64X,
    ARCH_M680X,
    ARCH_MOS65XX,
    ARCH_UNKNOWN,
} architectureId;

typedef struct architectureSpec {
    architectureId id;
    disassemblerMode x86_mode;
    int big_endian;
} architectureSpec;

typedef struct editorRow {
    size_t size;
    char  *chars;
} editorRow;

typedef struct disassemblerRow {
    size_t start_byte;
    size_t end_byte;
    char   diss_str[DISASSEMBLED_BUFFER_SIZE];
} disassemblerRow;

typedef struct editorConfig {
    size_t            cx;
    size_t            cy;
    size_t            rx;
    size_t            rowoff;
    size_t            coloff;
    size_t            screenrows;
    size_t            screencols;
    size_t            terminal_rows;
    int               window_too_small;
    size_t            cur_screencols;
    size_t            numrows;
    size_t            num_bytes;
    size_t            cur_byte;
    editorMode        mode;
    disassemblerMode  disassembler_mode;
    architectureId    architecture;
    int               big_endian;
    int               architecture_manual;
    int               architecture_menu;
    size_t            architecture_choice;
    int               binary_detected;
    int               editing;
    int               edit_ascii;
    int               edit_nibble;
    int               edit_exit_prompt;
    int               goto_prompt;
    size_t            goto_length;
    char              goto_input[2 * sizeof(size_t) + 3];
    int               executable_browser;
    int               executable_prompt;
    int               executable_imports;
    size_t            executable_choice;
    size_t            executable_input_length;
    char              executable_input[256];
    uint8_t          *file;
    time_t            statusmsg_time;
    FILE             *fp;
    char             *filename;
    char              statusmsg[80];
    disassemblerRow  *disassembler_buffer;
    struct termios    orig_termios;
} editorConfig;

extern editorConfig global_cfg;

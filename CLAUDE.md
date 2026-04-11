# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

LHiew is a Linux terminal clone of the Hiew editor. It views binary files in text, hex, or x86 disassembly (AT&T-style) modes. It is a C17 program built with CMake that uses raw-mode termios I/O (no curses) and links against the Zydis disassembler.

## Build / Run / Test

The repo must be cloned recursively because the disassembler lives in a git submodule at `deps/zydis`:

```sh
git submodule update --init --recursive   # if not cloned with --recursive
mkdir -p build && cd build
cmake ..
make
./lhiew ../<some-binary>
```

Run the test suite with CTest:

```sh
cd build
ctest --output-on-failure       # all tests
ctest -R append_buffer          # single module
```

`cmake-build-debug/` is a CLion-generated build tree; prefer a fresh `build/` directory for command-line work.

Compiler flags: `-Wall -Wextra -Wpedantic` via `target_compile_options`. New warnings should be treated as real. There is no lint target or formatter config.

## Architecture

### Project layout

```
include/lhiew/    Public headers (one per module)
src/              Implementation files + main.c
tests/            Per-module test files + test_harness.h
deps/zydis/       Zydis disassembler (git submodule)
```

### Build targets

- `lhiew_core` — static library containing all modules except `main.c`. Public include path: `include/`.
- `lhiew` — executable, links `lhiew_core`.
- `test_<module>` — one test executable per module, each links `lhiew_core`.

### Global state

The editor is built around a single global `editorConfig global_cfg` (declared in `include/lhiew/types.h`, defined in `src/types.c`). Every module reads and writes this struct directly — there is no handle passed around — so when adding state, add a field to `editorConfig` and initialize it in `init_editor()` (`src/editor.c`).

### Module responsibilities

- **types** (`types.h`, `types.c`) — all type definitions (`editorConfig`, `editorMode`, `disassemblerMode`, `disassemblerRow`, `editorRow`), constants, and the `global_cfg` definition.
- **append_buffer** (`append_buffer.h`, `append_buffer.c`) — growable byte buffer used to assemble a full screen frame before a single `write()`.
- **terminal** (`terminal.h`, `terminal.c`) — termios raw-mode setup/teardown, escape-sequence key decoder (arrows, PgUp/PgDn, Del), window-size detection, `die_safely`.
- **file_buffer** (`file_buffer.h`, `file_buffer.c`) — file open and read-only `mmap` into `global_cfg.file`. The editor never copies file contents.
- **editor** (`editor.h`, `editor.c`) — `init_editor` lifecycle, `switch_mode` (recomputes `cx`/`cy`/`numrows` from `cur_byte`), `get_row_len`.
- **input** (`input.h`, `input.c`) — `editor_process_keypress` dispatches keys: mode switching (`m` / `Ctrl-M`), disassembler operand-size cycling (`o`), cursor movement via `editor_move_cursor`, quit (`Ctrl-Q`).
- **render** (`render.h`, `render.c`) — per-mode row drawing (`draw_row_text`, `draw_row_hex`, `draw_row_disassembler`), status/message bars, scrolling, `editor_refresh_screen`.
- **disassembler** (`disassembler.h`, `disassembler.c`) — wraps Zydis. `disassemble_block(cur_byte)` fills `global_cfg.disassembler_buffer` with `screenrows` decoded instruction rows.

### Runtime loop (`src/main.c`)

1. `enable_raw_mode()` saves termios and puts stdin into non-canonical, no-echo mode; `disable_raw_mode` is registered via `atexit`.
2. `init_editor()` queries window size, sets defaults, and allocates `disassembler_buffer`.
3. `open_file_to_view()` opens and mmaps the file.
4. The main loop alternates `editor_refresh_screen()` and `editor_process_keypress()` forever.

### Modes and coordinates

`editorMode` (`TEXT_MODE`, `HEX_MODE`, `DISASSEMBLER_MODE`) selects which `draw_row_*` runs. `switch_mode()` recomputes `cur_screencols`, `cy`, `cx`, and `numrows` from `cur_byte` — hex mode forces `cur_screencols = HEX_BYTE_LENGTH` (16), other modes use the terminal width clamped to `SCREENCOLS_MIN` (80).

The canonical cursor state is `global_cfg.cur_byte` (an absolute byte offset into the mmap). `cx`/`cy` are derived from it via `cur_screencols`. When adding a movement or mode feature, update `cur_byte` and let `switch_mode()` / `get_byte_position()` re-derive the rest; do not maintain `cx`/`cy` independently.

`disassemblerMode` (`REAL`, `MODE_LONG_COMPAT_16/32/64`) is a separate axis that only affects how Zydis decodes instructions; `init_editor()` defaults it to 32-bit.

### Rendering invariant

All drawing goes through `append_buffer`: code appends strings (including ANSI escapes like `\x1b[7m` for highlight), and `editor_refresh_screen` writes the buffer to stdout in one syscall, then frees it. Never call `write`/`printf` directly during a frame.

### Include order convention

`lhiew/types.h` defines `_GNU_SOURCE` and other feature-test macros. It must be included **before** any standard library headers to ensure POSIX functions like `fileno` and `strdup` are declared. In source and test files, put `#include "lhiew/types.h"` first.

## Dependencies

- **Zydis** (submodule at `deps/zydis`) — added via `add_subdirectory` with `ZYDIS_BUILD_TOOLS` and `ZYDIS_BUILD_EXAMPLES` turned off, linked as `PUBLIC Zydis` through `lhiew_core`.
- Requires CMake >= 3.20 and a C17 compiler. Linux-only: depends on `termios.h`, `sys/ioctl.h` (`TIOCGWINSZ`), and `sys/mman.h` (`mmap`).

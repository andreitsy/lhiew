# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

LHiew is a Linux terminal binary viewer and hex editor inspired by Hiew. It views
files in text, hex, or disassembly modes and can overwrite existing bytes. It is
a C17 program built with CMake that uses raw-mode
termios I/O (no curses). Zydis handles x86 with AT&T syntax; Capstone 5.0.9 handles
the other supported CPUs. See `docs/architectures.md` for the support matrix,
design, source specifications, and limits.

## Build / Run / Test

Clone recursively to initialize the pinned `deps/zydis` and `deps/capstone` submodules:

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
deps/capstone/    Non-x86 decoders (pinned git submodule)
docs/            Architecture analysis and implementation design
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
- **file_buffer** (`file_buffer.h`, `file_buffer.c`) — regular-file open, checked file-size conversion and read-only private `mmap` into `global_cfg.file`. Reload replaces the mapping without copying the whole file.
- **hex_edit** (`hex_edit.h`, `hex_edit.c`) — writable reopen with file identity checks, mandatory backup before edit enable, private copy-on-write editing and a sparse sorted list of changed bytes. `hex_edit_patch` validates and reserves an entire group of field changes before staging any bytes. Saves verify file identity/version and expected original bytes, then use `pwrite` and `fsync`; failed saves retain pending records. Discard remaps the originally opened file. See `docs/hex-editing.md` for limits and failure semantics.
- **file_backup** (`file_backup.h`, `file_backup.c`) — creates `<filename>.backup` with bounded-memory, sparse-aware copying and exclusive publication. Preserves an existing independent regular backup; source changes or backup failures prevent editing. Flushes the copy and parent directory before editing begins.
- **executable** (`executable.h`, `executable.c`, `executable_{pe,ne,linear,nlm}.c`) — bounded PE32/PE32+, NE, LE/LX and i386 NLM v4 metadata parsers, independent of editor state. Rows expose raw file spans and validated editable name/ordinal fields; errors disable all structured edits. Initialize `executableInfo` to zero and release its rows with `executable_free`.
- **executable_browser** (`executable_browser.h`, `executable_browser.c`) — F8/`b` header/import browser, raw-byte navigation and exact-length name/width-preserving ordinal prompts. Uses the existing hex edit transaction/save/discard path; paired PE lookup/IAT changes stage together. See `docs/executable-imports.md` for format limits.
- **editor** (`editor.h`, `editor.c`) — `init_editor` lifecycle, `switch_mode` (recomputes `cx`/`cy`/`numrows` from `cur_byte`), `get_row_len`.
- **input** (`input.h`, `input.c`) — `editor_process_keypress` dispatches keys: mode switching (`m` / `Ctrl-M`), disassembler operand-size cycling (`o`), cursor movement via `editor_move_cursor`, quit (`Ctrl-Q`).
- **render** (`render.h`, `render.c`) — per-mode row drawing (`draw_row_text`, `draw_row_hex`, `draw_row_disassembler`), status/message bars, scrolling, `editor_refresh_screen`.
- **binary** (`binary.h`, `binary.c`) — bounded ELF, PE/TE, DOS MZ, thin Mach-O and i386 NLM v4 parsing. Distinguishes raw, detected, unsupported and malformed files; supplies entry/first-code offsets and file-region-to-runtime-address mappings without heap allocation. NLM uses file offsets because its load base is not encoded.
- **search** (`search.h`, `search.c`) — byte and text pattern search over the mapping. `search_compile` turns prompt text into at most `SEARCH_PATTERN_MAX` bytes, rejecting incomplete hexadecimal pairs; `search_find` is a pure forward/backward scan. Both are side-effect free and unit tested. The prompt, repeat and status handling live in the same module and move `cur_byte` through `switch_mode()`.
- **architecture** (`architecture.h`, `architecture.c`) — named decoder profiles, automatic detection on file read, manual overrides, profile labels, instruction alignment and entry navigation. Static detection metadata is valid only for the currently detected file; `binary_detected` and file identity gate access.
- **disassembler** (`disassembler.h`, `disassembler.c`) — wraps Zydis/Capstone. `disassemble_block(cur_byte)` fills `global_cfg.disassembler_buffer` with up to `screenrows` rows, centering the instruction containing `cur_byte`. A single forward decoding pass retains preceding rows in a ring, preserving Thumb IT state. Bounded lookback respects cached boundaries, known entry points, instruction alignment and mapped region ends.

### Runtime loop (`src/main.c`)

1. `enable_raw_mode()` saves termios and puts stdin into non-canonical, no-echo mode; `disable_raw_mode` is registered via `atexit`.
2. `init_editor()` queries window size, sets defaults, and allocates `disassembler_buffer`.
3. `open_file_to_view()` opens and mmaps the file.
4. The main loop alternates `editor_refresh_screen()` and `editor_process_keypress()` forever.

### Modes and coordinates

`editorMode` (`TEXT_MODE`, `HEX_MODE`, `DISASSEMBLER_MODE`) selects which `draw_row_*` runs. `switch_mode()` recomputes `cur_screencols`, `cy`, `cx`, and `numrows` from `cur_byte`. Text uses the actual terminal width; hex mode calculates how many bytes fit alongside offsets and ASCII. Disassembly drops the raw-byte column on narrow terminals and clips instruction text to fit.

`editor_resize()` records the physical `terminal_rows` and `screencols`, reserves two rows for status/help, resizes the disassembly buffer, and reflows the cursor. Below `SCREENCOLS_MIN` columns or `SCREENROWS_MIN` total rows (24x5), `window_too_small` pauses navigation and displays a resize message. Input timeouts poll the terminal dimensions, so resizing redraws without a keypress; enlarging restores the selected byte.

The canonical cursor state is `global_cfg.cur_byte` (an absolute byte offset into the mmap). `cx`/`cy` are derived from it via `cur_screencols`. When adding a movement or mode feature, update `cur_byte` and let `switch_mode()` / `get_byte_position()` re-derive the rest; do not maintain `cx`/`cy` independently.

`disassemblerMode` (`REAL`, `MODE_LONG_COMPAT_16/32/64`) controls x86 decoding;
`architecture` and `big_endian` select the wider CPU profile. `init_editor()`
defaults to x86-32. File headers override those defaults; raw files retain the
x86 fallback, while unsupported/malformed headers select `ARCH_UNKNOWN` and
render bytes. `architecture_select()` clears cached rows without moving
`cur_byte`; selecting Auto re-detects the header (raw resets to x86-32).

Shift-F1 or `a` opens the scrollable architecture menu. `architecture_menu` and
`architecture_choice` hold its state; Escape cancels, Enter applies. `o` cycles
x86 modes via the same profile API. `e` jumps to the detected entry/first code
region; entering assembly mode at offset zero also performs that jump. All new
state is initialized in `init_editor()`. The menu uses the normal append buffer.

The left gutter remains a file offset. Instruction formatting uses the mapped
runtime address for supported containers; raw/unmapped bytes use file offsets.
Native relative operand syntax, including RISC-V branch displacements, is retained.
No relocations, symbols, universal-binary slices, or mixed ARM mapping symbols
are applied. Changing a decode profile does not claim to translate file content.

Page Up/Down moves by `screenrows` instructions in disassembly, preserving the byte offset within the destination instruction where it fits. Text and hex paging moves by `screenrows * cur_screencols` bytes. All modes clamp paging at file boundaries.

F3 enters hex editing from any view. `editing`, `edit_ascii` and `edit_nibble`
track the edit session, input pane and nibble. Each typed digit updates the
private mapped byte immediately; after two digits the cursor advances. Tab
switches to printable ASCII. F9 saves; Escape/F10 leaves edit mode. Leaving or
quitting with pending changes opens `edit_exit_prompt` (1 = leave, 2 = quit):
`s` saves, `d` discards, Escape continues. Mode/architecture shortcuts are
inactive while editing so letters remain data. Editing never resizes the file.

Before editing begins, `<filename>.backup` must exist as an independent regular
file. A new backup captures the original bytes; existing backups are preserved
across saves and sessions. Failure to copy, validate or flush the backup prevents
editing. Backups preserve file bytes, not all metadata, and are not automatically
restored after a failed save.

F8 or `b` while viewing opens the executable browser. `executable_browser`,
`executable_imports` and `executable_choice` track its state; Tab switches the
import and header/region lists, Enter jumps to a raw file span, and F3 opens a
field prompt. `executable_prompt` (1 = name, 2 = ordinal), `executable_input` and
`executable_input_length` hold prompt state. Names keep their exact byte length;
ordinal edits preserve field width and encoding flags. F9 saves; Escape cancels
a prompt or closes the browser, retaining staged edits for the ordinary hex
save/discard flow. F8 remains available while editing; `b` remains byte input.
Browser text is copied from bounded metadata, sanitized and drawn through the
append buffer. No table resizing, import insertion/removal or relocation occurs.

F5 (or `g` while viewing) opens `goto_prompt`; `goto_input`/`goto_length` hold an
absolute hexadecimal file offset, with optional `0x`. Parsing checks overflow
and file bounds. Home/End select row boundaries; Ctrl-Home/End select the first
and last bytes. One-past-EOF is a valid viewing cursor but cannot be edited.
Prompts and edit cursors use the normal append-buffer rendering path. When an
edit-session file conflict is detected, rendering avoids the stale mapping.
The build requests 64-bit `off_t`; mappings still require enough virtual address
space, and pending changes require memory proportional to bytes/pages touched.

F7 (or `s` while viewing) opens `search_prompt`. Tab switches between hexadecimal
pairs and literal text, Ctrl-U clears, and Enter compiles the pattern into
`search_pattern`/`search_pattern_length` and searches from the cursor. Shift-F7
repeats in the recorded direction; `n`/`N` repeat forward/backward while viewing
and set that direction. Repeats start one byte past the cursor so a match already
under it is not returned again. Search works during an edit session, where it
reads the private mapping and therefore matches pending changes; `F7` is used
there because letters are data. A scan is a single uninterruptible pass, so add a
progress/abort path before relaxing the pattern length or adding wildcards.

### Rendering invariant

All drawing goes through `append_buffer`: code appends strings (including ANSI escapes like `\x1b[7m` for highlight), and `editor_refresh_screen` writes the buffer to stdout in one syscall, then frees it. Never call `write`/`printf` directly during a frame.

### Include order convention

`lhiew/types.h` defines `_GNU_SOURCE` and other feature-test macros. It must be included **before** any standard library headers to ensure POSIX functions like `fileno` and `strdup` are declared. In source and test files, put `#include "lhiew/types.h"` first.

## Dependencies

- **Zydis** (submodule at `deps/zydis`) — added via `add_subdirectory` with `ZYDIS_BUILD_TOOLS` and `ZYDIS_BUILD_EXAMPLES` turned off, linked as `PUBLIC Zydis` through `lhiew_core`.
- **Capstone 5.0.9** (submodule at `deps/capstone`) — static native non-x86 backends; x86/EVM/WASM, tools, install targets, and upstream tests disabled. Full instruction strings enabled. Headers treated as third-party system includes.
- Requires CMake >= 3.20 and a C17 compiler. Linux-only: depends on `termios.h`, `sys/ioctl.h` (`TIOCGWINSZ`), and `sys/mman.h` (`mmap`).

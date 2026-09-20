# Repository Guidelines

## Project

LHiew is a Linux terminal binary viewer and hex editor inspired by Hiew. It views
files in text, hex, or disassembly modes and can overwrite existing bytes. It is
a C17 program built with CMake that uses raw-mode
termios I/O (no curses). Zydis handles x86 with AT&T syntax; Capstone 5.0.9 handles
the other supported CPUs. See `docs/architectures.md` for the support matrix,
design, source specifications, and limits.

Requires CMake >= 3.21 and GCC or Clang with C17 support. Linux is the target
platform; the application depends on `termios.h`, `sys/ioctl.h` (`TIOCGWINSZ`),
and `sys/mman.h` (`mmap`). `deps/zydis/` and `deps/capstone/` are pinned decoder
Git submodules; `pics/` contains README screenshots.

## Build, Test, and Development Commands

Run from the repository root:

```sh
git submodule update --init --recursive  # Fetch both decoders and their dependencies
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug  # Configure
cmake --build build --parallel          # Compile application and tests
ctest --test-dir build --output-on-failure  # Run all tests
ctest --test-dir build -R '^append_buffer$' --output-on-failure
./build/lhiew /path/to/binary            # Launch in a terminal
```

Press `Ctrl-Q` to quit. Keep generated build artifacts out of commits.
`cmake-build-debug/` is a CLion-generated build tree; prefer a fresh `build/`
directory for command-line work.

`lhiew_options` shares strict first-party warnings, including conversions,
prototypes, format strings, shadowing and `-Werror`. Release uses GCC `-Ofast`
or Clang's documented equivalent. `LHIEW_SANITIZERS=ON` enables ASan/UBSan.
See [development.md](docs/development.md) for the full build matrix and options.
Keep these policies out of vendored targets. There is no formatter configuration.

## Coding Style & Naming Conventions

Use four-space indentation and opening braces on the declaration or control-flow line. Follow existing snake_case function, variable, and module names; preserve existing type names such as `editorConfig`. Use uppercase constants and enum members. Headers use `#pragma once`.

No formatter configuration or lint target exists; match surrounding code and resolve the strict warnings configured by `lhiew_options`, including conversion, prototype and format checks. Warnings are errors by default. Prefer small typed helpers over repeated parsing, prompt or file-state logic.

### Include order convention

`lhiew/types.h` defines `_GNU_SOURCE` and other feature-test macros. It must be included **before** any standard library headers to ensure POSIX functions like `fileno` and `strdup` are declared. In source and test files, put `#include "lhiew/types.h"` first.

## Testing Guidelines

Tests use the custom assertion harness in `tests/test_harness.h`, executed through CTest. Name files `tests/test_<module>.c` and test functions `test_<behavior>`. Add new modules to the `lhiew_add_test` loop in `CMakeLists.txt`. Reset shared editor state with `RESET_GLOBAL_CFG()` when needed.

Add regression coverage for behavioral fixes and exercise mode transitions, cursor boundaries, or rendering output as applicable. There is no configured coverage threshold. Run the full suite before submitting; GitHub Actions tests GCC and Clang Debug/Release builds, including Clang ASan/UBSan. Release enables GCC `-Ofast` or Clang's equivalent; validate both Debug and Release after behavioral changes. See [docs/development.md](docs/development.md) for commands and build options.

## Commit & Pull Request Guidelines

Use concise imperative subjects, following history such as “Fix cmake” or “Move code to src”; no mandatory prefix convention exists. Keep changes focused. Describe the problem, resulting behavior, and validation in PRs; link relevant issues and include terminal screenshots for visible interface changes.

## Architecture

Initialize new `global_cfg` fields in `init_editor()`. Treat `cur_byte` as the
canonical cursor offset. Assemble screen frames through `append_buffer` and keep
terminal writes in the refresh path. The sections below detail each invariant.

### Global state

The editor uses a single `editorConfig global_cfg` (declared in `include/lhiew/types.h`,
defined in `src/types.c`). UI modules access it directly; the binary parsers and
pure search routines receive explicit inputs. `init_editor()` uses a designated
initializer for nonzero defaults and preserves the saved terminal settings;
other fields start at zero. Initialize new state there.

### Module responsibilities

- **types** (`types.h`, `types.c`) — all type definitions (`editorConfig`, `editorMode`, `disassemblerMode`, `disassemblerRow`), constants, and the `global_cfg` definition.
- **append_buffer** (`append_buffer.h`, `append_buffer.c`) — byte buffer with geometric capacity growth and checked size arithmetic. Initialize with `ABUF_INIT`; freeing resets it for reuse. Used to assemble a screen frame before terminal output.
- **terminal** (`terminal.h`, `terminal.c`) — termios raw-mode setup/teardown, escape-sequence key decoder (arrows, PgUp/PgDn, Del), window-size detection, `die_safely`.
- **file_buffer** (`file_buffer.h`, `file_buffer.c`) — regular-file open, checked file-size conversion and read-only private `mmap` into `global_cfg.file`. Reload replaces the mapping without copying the whole file.
- **hex_edit** (`hex_edit.h`, `hex_edit.c`) — writable reopen with file identity checks, mandatory backup before edit enable, private copy-on-write editing and a sparse sorted list of changed bytes. `hex_edit_patch` validates and reserves an entire group of field changes before staging any bytes. Saves verify file identity/version and expected original bytes, then use `pwrite` and `fsync`; failed saves retain pending records. Discard remaps the originally opened file. See `docs/hex-editing.md` for limits and failure semantics.
- **file_backup** (`file_backup.h`, `file_backup.c`) — creates `<filename>.backup` with bounded-memory, sparse-aware copying and exclusive publication. Preserves an existing independent regular backup; source changes or backup failures prevent editing. Flushes the copy and parent directory before editing begins.
- **executable** (`executable.h`, `executable.c`, `executable_{pe,ne,linear,nlm}.c`) — bounded PE32/PE32+, NE, LE/LX and i386 NLM v4 metadata parsers, independent of editor state. Rows expose raw file spans and validated editable name/ordinal fields; errors disable all structured edits. Initialize `executableInfo` to zero and release its rows with `executable_free`.
- **executable_browser** (`executable_browser.h`, `executable_browser.c`) — F8/`b` header/import browser, raw-byte navigation and exact-length name/width-preserving ordinal prompts. Uses the existing hex edit transaction/save/discard path; paired PE lookup/IAT changes stage together. See `docs/executable-imports.md` for format limits.
- **editor** (`editor.h`, `editor.c`) — initialization, resizing and `switch_mode`, which recomputes `cx`/`cy`/`numrows` from `cur_byte` with overflow-safe row arithmetic.
- **input** (`input.h`, `input.c`) — `editor_process_keypress` dispatches keys: mode switching (`m` / `Ctrl-M`), disassembler operand-size cycling (`o`), cursor movement via `editor_move_cursor`, quit (`Ctrl-Q`).
- **render** (`render.h`, `render.c`) — per-mode row drawing (`draw_row_text`, `draw_row_hex`, `draw_row_disassembler`), status/message bars, scrolling, `editor_refresh_screen`.
- **binary** (`binary.h`, `binary.c`) — bounded ELF, PE/TE, DOS MZ, thin Mach-O and i386 NLM v4 parsing. Distinguishes raw, detected, unsupported and malformed files; supplies entry/first-code offsets and file-region-to-runtime-address mappings without heap allocation. NLM uses file offsets because its load base is not encoded.
- **search** (`search.h`, `search.c`) — byte and text pattern search over the mapping. `search_compile` turns prompt text into at most `SEARCH_PATTERN_MAX` bytes, rejecting incomplete hexadecimal pairs; `search_find` scans forward/backward without changing editor state. Forward scanning uses `memchr` to locate candidate first bytes. The prompt, repeat and status handling live in the same module and move `cur_byte` through `switch_mode()`.
- **architecture** (`architecture.h`, `architecture.c`) — named decoder profiles, automatic detection on file read, manual overrides, profile labels, instruction alignment and entry navigation. Static detection metadata is valid only for the currently detected mapping; `binary_detected`, its pointer and size gate access.
- **disassembler** (`disassembler.h`, `disassembler.c`) — wraps Zydis/Capstone. `disassemble_block(cur_byte)` fills `global_cfg.disassembler_buffer` with up to `screenrows` rows, centering the instruction containing `cur_byte`. A single forward decoding pass retains preceding rows in a ring, preserving Thumb IT state. Bounded lookback respects cached boundaries, known entry points, instruction alignment and mapped region ends.

### Modes and coordinates

`editorMode` (`TEXT_MODE`, `HEX_MODE`, `DISASSEMBLER_MODE`) selects which `draw_row_*` runs. `switch_mode()` recomputes `cur_screencols`, `cy`, `cx`, and `numrows` from `cur_byte`. Text uses the actual terminal width; hex mode calculates how many bytes fit alongside offsets and ASCII. Disassembly drops the raw-byte column on narrow terminals and clips instruction text to fit.

`editor_resize()` records the physical `terminal_rows` and `screencols`, reserves two rows for status/help, resizes the disassembly buffer, and reflows the cursor. Below `SCREENCOLS_MIN` columns or `SCREENROWS_MIN` total rows (24x5), `window_too_small` pauses navigation and displays a resize message. Input timeouts poll the terminal dimensions, so resizing redraws without a keypress; enlarging restores the selected byte.

The canonical cursor state is `global_cfg.cur_byte` (an absolute byte offset into the mmap). `cx`/`cy` are derived from it via `cur_screencols`. When adding a movement or mode feature, update `cur_byte` and let `switch_mode()` re-derive the rest; do not maintain `cx`/`cy` independently.

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

Keyboard and UI feature contracts — paging, F3 hex editing, the mandatory
`<filename>.backup` precondition, the F8 browser, F5 goto and F7 search — live in
the `lhiew-ui-features` skill (`.claude/skills/lhiew-ui-features/SKILL.md`).
Read it before changing `input.c`, `render.c`, `hex_edit.c`,
`executable_browser.c` or `search.c`.

### Rendering invariant

All drawing goes through `append_buffer`: code appends strings (including ANSI
escapes like `\x1b[7m` for highlight), and `editor_refresh_screen` sends the complete
frame through `terminal_write`, then frees it. Output normally takes one syscall;
the helper completes short writes and retries `EINTR`. A failed frame write exits
through the normal terminal restoration path. Exit-time screen cleanup is best
effort and preserves the original diagnostic. Never call `write`/`printf`
directly during a frame.
### Shared internals

`src/byte_reader.h` provides unaligned endian reads and subtraction-based span
checks; callers validate spans before reading. `src/nlm_internal.h` shares NLM
header validation between detection and browsing. `src/file_state.h` distinguishes
inode identity, file extent and version checks for backup/save operations.
Reuse these helpers; keep format-specific validation in each parser. Goto,
search and import prompts share printable ASCII input, Backspace/Ctrl-H and
Ctrl-U handling in `input`, plus common drawing in `render`. Architecture and
browser menus share bounded arrow, j/k, paging and Home/End navigation.

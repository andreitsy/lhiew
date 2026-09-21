# Repository Guidelines

## Project

LHiew is a Linux terminal binary viewer and hex editor inspired by Hiew. It views
files in text, hex, or disassembly modes and can overwrite existing bytes. It is
a C17 program built with CMake that uses raw-mode
termios I/O (no curses). Zydis handles x86 with AT&T syntax; Capstone 5.0.9 handles
the other supported CPUs. Linux is the target platform. See
`docs/architectures.md` for the support matrix, design, source specifications,
and limits.

## Build, Test, and Development Commands

Standard CMake/CTest from the repository root. Run
`git submodule update --init --recursive` first; the decoder builds fail without it.

Press `Ctrl-Q` or `Ctrl-C` to quit, `Ctrl-Z` to suspend to the shell. Keep
generated build artifacts out of commits.
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

Each module is one `.h`/`.c` pair under `include/lhiew/` and `src/`; the notes
below are the contracts the headers do not state.

- **types** — all type definitions and the `global_cfg` definition.
- **append_buffer** — initialize with `ABUF_INIT`; freeing resets it for reuse.
- **terminal** — termios raw mode, `terminal_suspend` job-control stop and resume, escape-sequence key decoding, `die_safely`.
- **file_buffer** — read-only private `mmap` into `global_cfg.file`. Reload replaces the mapping without copying the whole file.
- **hex_edit** — writable reopen with file identity checks, mandatory backup before edit enable, private copy-on-write editing. `hex_edit_patch` validates and reserves an entire group of field changes before staging any bytes. Saves verify file identity/version and expected original bytes; failed saves retain pending records. Discard remaps the originally opened file. See `docs/hex-editing.md` for limits and failure semantics.
- **file_backup** — preserves an existing independent regular backup; source changes or backup failures prevent editing. Flushes the copy and parent directory before editing begins.
- **executable** (plus `executable_{pe,ne,linear,nlm}.c`) — bounded PE32/PE32+, NE, LE/LX and i386 NLM v4 parsers, independent of editor state. Errors disable all structured edits. Initialize `executableInfo` to zero and release its rows with `executable_free`.
- **executable_browser** — uses the existing hex edit transaction/save/discard path; paired PE lookup/IAT changes stage together. See `docs/executable-imports.md` for format limits.
- **editor** — `switch_mode` recomputes `cx`/`cy`/`numrows` from `cur_byte` with overflow-safe row arithmetic.
- **input** — `editor_process_keypress` dispatches every key, including quit (`Ctrl-Q` or `Ctrl-C`) and suspend (`Ctrl-Z`).
- **render** — per-mode row drawing, status/message bars, scrolling, `editor_refresh_screen`.
- **binary** — bounded ELF, PE/TE, DOS MZ, thin Mach-O and i386 NLM v4 parsing. Distinguishes raw, detected, unsupported and malformed files, without heap allocation. NLM uses file offsets because its load base is not encoded.
- **search** — `search_compile` rejects incomplete hexadecimal pairs and caps patterns at `SEARCH_PATTERN_MAX`; `search_find` scans without changing editor state. The prompt, repeat and status handling live in the same module and move `cur_byte` through `switch_mode()`.
- **architecture** — decoder profiles, detection, manual overrides and entry navigation. Static detection metadata is valid only for the currently detected mapping; `binary_detected`, its pointer and size gate access.
- **disassembler** — wraps Zydis/Capstone. `disassemble_block(cur_byte)` fills `global_cfg.disassembler_buffer`, centering the instruction containing `cur_byte`. A single forward decoding pass retains preceding rows in a ring, preserving Thumb IT state. Bounded lookback respects cached boundaries, known entry points, instruction alignment and mapped region ends.

### Modes and coordinates

The canonical cursor state is `global_cfg.cur_byte` (an absolute byte offset
into the mmap). `cx`/`cy` are derived from it via `cur_screencols`. When adding a
movement or mode feature, update `cur_byte` and let `switch_mode()` re-derive the
rest; do not maintain `cx`/`cy` independently. Initialize all new state in
`init_editor()`.

`editorMode` (`TEXT_MODE`, `HEX_MODE`, `DISASSEMBLER_MODE`) selects which
`draw_row_*` runs; `disassemblerMode` and `architecture`/`big_endian` select the
decode profile. Changing a decode profile does not claim to translate file
content, and the gutter stays a file offset in every mode.

Keyboard and UI feature contracts — Ctrl-Q/Ctrl-C quitting and Ctrl-Z job
control, paging, F3 hex editing, the mandatory `<filename>.backup` precondition,
the F8 browser, F5 goto and F7 search — plus mode/coordinate recomputation,
resize behavior and architecture selection live in the `lhiew-ui-features` skill
(`.claude/skills/lhiew-ui-features/SKILL.md`). Read it before changing
`input.c`, `terminal.c`, `render.c`, `hex_edit.c`, `executable_browser.c` or
`search.c`.

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

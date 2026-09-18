# Repository Guidelines

## Project Structure & Module Organization

LHiew is a Linux terminal binary viewer with text, hex, and multiarchitecture disassembly modes plus byte-overwrite editing. `src/` contains module implementations and `main.c`; matching public headers live in `include/lhiew/`. CMake builds modules into `lhiew_core`, shared by the executable and tests. `tests/` holds module tests and `test_harness.h`. `deps/zydis/` and `deps/capstone/` are pinned decoder Git submodules; `pics/` contains README screenshots.

## Build, Test, and Development Commands

Use Linux, CMake 3.21 or newer, and GCC or Clang with C17 support. Run from the repository root:

```sh
git submodule update --init --recursive  # Fetch both decoders and their dependencies
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug  # Configure
cmake --build build --parallel          # Compile application and tests
ctest --test-dir build --output-on-failure  # Run all tests
ctest --test-dir build -R '^append_buffer$' --output-on-failure
./build/lhiew /path/to/binary            # Launch in a terminal
```

Press `Ctrl-Q` to quit. Keep generated build artifacts out of commits.

## Coding Style & Naming Conventions

Use four-space indentation and opening braces on the declaration or control-flow line. Follow existing snake_case function, variable, and module names; preserve existing type names such as `editorConfig`. Use uppercase constants and enum members. Headers use `#pragma once`.

Include `lhiew/types.h` before standard headers when using its feature-test macros. No formatter configuration or lint target exists; match surrounding code and resolve the strict warnings configured by `lhiew_options`, including conversion, prototype and format checks. Warnings are errors by default. Prefer small typed helpers over repeated parsing, prompt or file-state logic.

## Testing Guidelines

Tests use the custom assertion harness in `tests/test_harness.h`, executed through CTest. Name files `tests/test_<module>.c` and test functions `test_<behavior>`. Add new modules to the `lhiew_add_test` loop in `CMakeLists.txt`. Reset shared editor state with `RESET_GLOBAL_CFG()` when needed.

Add regression coverage for behavioral fixes and exercise mode transitions, cursor boundaries, or rendering output as applicable. There is no configured coverage threshold. Run the full suite before submitting; GitHub Actions tests GCC and Clang Debug/Release builds, including Clang ASan/UBSan. Release enables GCC `-Ofast` or Clang's equivalent; validate both Debug and Release after behavioral changes. See [docs/development.md](docs/development.md) for commands and build options.

## Commit & Pull Request Guidelines

Use concise imperative subjects, following history such as “Fix cmake” or “Move code to src”; no mandatory prefix convention exists. Keep changes focused. Describe the problem, resulting behavior, and validation in PRs; link relevant issues and include terminal screenshots for visible interface changes.

## Architecture Invariants

Initialize new `global_cfg` fields in `init_editor()`. Treat `cur_byte` as the canonical cursor offset. Assemble screen frames through `append_buffer` and keep terminal writes in the refresh path. See `CLAUDE.md` for detailed architecture guidance.

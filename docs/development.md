# Development and validation

LHiew is a C17 terminal application. Favor clear control flow, small typed
helpers, explicit ownership, and bounded arithmetic. Share a helper when its
callers have the same contract; keep executable-format rules local to their
parsers. Avoid cast-based structure overlays on untrusted file bytes.

## Build and test

CMake 3.21 or newer is required: its [C_STANDARD support](https://cmake.org/cmake/help/latest/prop_tgt/C_STANDARD.html)
first recognizes C17 in 3.21. Initialize dependencies with `git submodule update --init --recursive`, then
build outside the source directories. GCC and Clang are supported; Linux is the
target platform. Python 3 enables the fixture and pseudo-terminal checks.

```sh
cmake -S . -B build/debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build/debug --parallel
ctest --test-dir build/debug --output-on-failure

cmake -S . -B build/release -DCMAKE_BUILD_TYPE=Release
cmake --build build/release --parallel
ctest --test-dir build/release --output-on-failure

cmake -S . -B build/sanitize -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_C_COMPILER=clang -DLHIEW_SANITIZERS=ON
cmake --build build/sanitize --parallel
ctest --test-dir build/sanitize --output-on-failure
```

Set `CMAKE_C_COMPILER=gcc` or `clang` in a fresh build directory to switch
compilers. CI tests both compilers in Debug and Release; the Clang Debug job
also enables AddressSanitizer and UndefinedBehaviorSanitizer.

| CMake option | Default | Purpose |
| --- | --- | --- |
| `LHIEW_WARNINGS_AS_ERRORS` | `ON` | Fail first-party compilation on warnings |
| `LHIEW_FAST_RELEASE` | `ON` | GCC `-Ofast`; Clang `-O3 -ffast-math -fstrict-aliasing`, in Release only |
| `LHIEW_SANITIZERS` | `OFF` | Instrument LHiew and its C tests with ASan/UBSan; stop on undefined behavior |
| `BUILD_TESTING` | `ON` | Build/register unit, fixture and terminal tests |

`lhiew_options` applies the same policy to the application, core library, tests,
and fault-injection backend. Vendored decoders keep their upstream flags and are
not instrumented by `LHIEW_SANITIZERS`. The unused Zydis encoder is disabled,
while its decoder and instruction-formatting support remain enabled. Debug keeps normal CMake debug flags;
RelWithDebInfo and MinSizeRel retain their normal optimization levels.

Strict warnings cover `-Wall -Wextra -Wpedantic`, conversions and signedness,
shadowing, strict/missing prototypes, format strings, undefined preprocessor
symbols, writable string literals, qualifier/alignment casts, pointer arithmetic,
variable-length arrays, null dereferences and double promotion. Formatted
message helpers carry compiler format annotations. Fix a warning's cause before
adding casts; annotate intentional, range-checked narrowing conversions.

`-Ofast` is aggressive optimization, not a stricter language-conformance check:
it relaxes some C semantics. The editor and parsers use integer/byte arithmetic,
but passing tests is not a guarantee for every input or compiler. Set
`-DLHIEW_FAST_RELEASE=OFF` for CMake's normal Release optimization. Clang's
equivalent flags avoid its `-Ofast` deprecation diagnostic. See the official
[GCC optimization reference](https://gcc.gnu.org/onlinedocs/gcc/Optimize-Options.html)
and [Clang optimization reference](https://clang.llvm.org/docs/CommandGuide/clang.html).

## Shared implementation

- `byte_reader.h` centralizes span/overlap checks and unaligned endian reads.
  A reader's caller must first establish that its bytes exist. The detector and
  import browser also share primary NLM header validation in `nlm_internal.h`.
- `file_state.h` distinguishes inode identity from equal extent and version.
  Backup aliases must be rejected even when the source size changes. Opening
  and reloading share the mapping path, and publish new state only after success.
- `input` shares hexadecimal conversion, prompt editing and bounded menu
  navigation. Goto, search and import prompts accept printable ASCII with
  Backspace/Ctrl-H and Ctrl-U editing. `render` shares prompt layout and writes
  hex digits directly.
  Cursor coordinates are derived from `cur_byte`; redundant coordinate state
  and unused conversion APIs have been removed.
- Forward search uses `memchr` to skip to candidate first bytes; both directions
  preserve overlapping matches and stop at file boundaries without wrapping.
- `append_buffer` grows capacity geometrically, checks addition overflow and
  resets all fields when freed. An allocation failure leaves its prior contents
  intact. It is a byte buffer, not a NUL-terminated string.
- `terminal_write` completes partial writes and retries interruptions. Rendering
  handles permanent output failures, while exit-time cleanup remains best effort.
  Fault-injection tests cover short writes, `EINTR`, zero progress and errors;
  checking results also keeps optimized glibc builds clean under `-Werror`.
- Architecture selection and labels share profile matching. The decoder caches
  alignment and detection metadata for each frame while still resolving mapped
  region boundaries for each instruction and retaining Thumb IT context.

These choices follow the simplicity, clarity, testing and measured-performance
principles described in [Kernighan and Pike's preface to The Practice of
Programming](https://www.cs.princeton.edu/~bwk/tpop.webpage/preface.html).
The requested K&R solutions PDF could not be retrieved during this review;
it is not treated as a verified source.

## Regression coverage

Run the full suite after behavior changes, including optimized builds. Unit
tests exercise parser truncation/overlap, huge offsets, stale detection metadata,
single-row disassembly, menu limits, narrow prompts, buffer growth, failed
reloads and invalid patch transactions. Storage tests also inject allocation,
read, write and flush failures and exercise sparse files beyond 4 GiB.

Expected fixture bytes and instruction strings remain independent of the
decoder under test. Python integration tests drive real pseudo-terminals for
resize, editing, import-browser and search workflows. Keep generated builds,
temporary files and test snapshots out of commits.

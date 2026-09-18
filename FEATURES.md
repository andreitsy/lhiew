# LHiew vs. Hiew — Feature Comparison

**LHiew audit date: 2026-09-17.** Comparison items follow the
[Hiew Documentation Project](https://taviso.github.io/hiewdocs/index.htm),
[Tavis Ormandy's walkthrough of three Hiew workflows](https://lock.cmpxchg8b.com/hiew.html),
and [Kris Kaspersky's Hiew 6.03 article](http://unicornix.spb.ru/docs/prog/heap/hiew.htm).
The last is a historical review. Its additions are grouped below; unlabelled
Hiew shortcuts use the community reference, not a combined version specification.
LHiew status is based on the current source and regression evidence linked below.
Tables group shortcuts and repeat some capabilities in different contexts; they
are not a count of unique shortcuts or a percentage of Hiew support.

✅ means implemented or an equivalent binding; ⚠️ means the described subset is
implemented; ❌ means the listed command or capability is absent.

Common desktop and mobile targets are the priority: **x86, ARM/Thumb/AArch64, and
RISC-V**. Other exposed profiles remain supported through selected decoder modes,
with limits on ISA revisions and executable variants. See the
[architecture analysis, implementation plan, and limits](docs/architectures.md).

## Workflow scenarios

The shortcut categories further down inventory commands one at a time. These
three scenarios come from [Tavis Ormandy's walkthrough](https://lock.cmpxchg8b.com/hiew.html)
and instead follow a task end to end, which is what decides whether LHiew is
usable for a job. A scenario is only as strong as its weakest step: one missing
command can block the rest of the sequence even when neighbouring rows are ✅.

### Scenario 1 — Map an undocumented file format

Locate candidate fields, annotate them, then extract a length-prefixed payload.

| Hiew step | Hiew key | LHiew |
|---|---|---|
| List the strings in the file | `Alt-F6` | ❌ No string extraction or strings dialog |
| Mark a field as a block | `*` | ❌ No range selection; only the cursor byte is highlighted |
| Move/search while defining the block | movement keys | ⚠️ Movement and search work; neither extends a selection |
| Colour a marked block | `Alt-M`, `Alt-Shift-M` | ❌ No block colours or `.cmarkers` persistence |
| Comment a location | `;` | ❌ No comment store |
| Name a location | `Shift-F12` | ❌ No name store |
| Step between marked fields | `Alt-N`, `Alt-Shift-N` | ❌ Needs marked blocks first |
| Open the names list and jump | `F12` | ❌ No names window |
| Read a field as a number | `Alt-=` calculator | ❌ No calculator; a DWORD must be read from the hex pane by hand |
| Jump forward by a decimal count | `F5`, `+203008t` | ⚠️ `F5`/`g` take an absolute hex offset; no relative or decimal input |
| Save the selected block to a file | `F2` | ❌ No block export |
| Return to the previous location | `BkSp` | ❌ No location history |
| Prefill fields from a format template | — | ❌ No templates |

**Blocked at step 2.** Everything after the first field depends on block marking
and an annotation store, neither of which exists. Search is the one step of this
scenario that now works.

### Scenario 2 — Patch an executable without source

Flip to the code view, follow the control flow, find the import, patch it out.

| Hiew step | Hiew key | LHiew |
|---|---|---|
| Flip text / code / data without losing your place | `Enter` | ✅ `m` forward, `Ctrl-M` backward; `cur_byte` is preserved |
| Disassemble the code under the cursor | — | ✅ Zydis for x86, Capstone profiles for other ISAs |
| Follow a branch from its marker | `1`–`9` | ❌ No target markers and no branch-follow command |
| Comment inside the listing | `;` | ❌ No comment store |
| Push/pop a bookmark while exploring | `+`, `-` | ❌ No bookmark stack |
| Recognize the executable format automatically | — | ✅ ELF, PE, TE, MZ, thin Mach-O, i386 NLM |
| Browse and patch headers, sections and tables | `F8` | ✅ `F8`/`b` browser for PE, NE, LE, LX, NLM |
| Inspect the imports table | `F8` | ✅ Module and import rows with names and ordinals |
| Find a specific import by name | — | ⚠️ The list can be read and searched in the file, but names are not resolved onto call operands |
| NOP an instruction | `Alt-F2` | ⚠️ `F3` then typing `90` over the bytes; no instruction-aware NOP |
| Assemble a replacement instruction | — | ❌ No assembler; the unused Zydis encoder is disabled at build time |

**Largely reachable.** This is the scenario LHiew can follow today: the format is
recognized, the imports table opens, and the bytes can be patched and saved. The
gaps are ergonomic rather than structural — branch following and an assembler
replace hand-counted offsets and hand-typed opcodes.

### Scenario 3 — Repair a damaged file

Find every corrupted byte pair and delete one byte of each, repeatedly.

| Hiew step | Hiew key | LHiew |
|---|---|---|
| Inspect the broken header | — | ✅ Hex view with offset, hex and ASCII panes |
| Search for the next `0D 0A` | `F7` | ✅ `F7`/`s`; hex-pair or text input, forward and backward |
| Repeat the search | `Shift-F7`, `Ctrl-Enter` | ✅ `n` next, `N` previous, `Shift-F7` in the recorded direction |
| Mark the byte to remove | `*` | ❌ No range selection |
| Delete it and shorten the file | `Shift-F2` | ❌ Editing is overwrite-only; file length never changes |
| Record the fix as a macro | `Alt-.` | ❌ No macro recorder |
| Loop it until the search fails | `Alt-=` manager, `L`/`S` flags | ❌ No macro manager or execution controls |
| Abort a running macro | `Esc` | ❌ Nothing to abort |

**Blocked at the delete.** Search now covers the locating half. The repair half
needs file-shortening edits, which the current
[overwrite model](docs/hex-editing.md) deliberately excludes, and then macros to
make the repetition practical.

## Summary

| | Hiew comparison item | LHiew |
|---|---|---|
| Role | Binary editor | Binary viewer and hex/ASCII overwrite editor; explicit save |
| Views | Hex, Text, Code | Hex, Text, Code |
| Architectures | x86 modes and architecture selection with `Shift-F1` | Zydis for x86; Capstone for selected native ISAs; Auto plus 32 manual profiles |
| Disassembly syntax | Intel for x86 | AT&T for x86; backend syntax for other ISAs; no syntax toggle |
| Large files | Advertised unlimited-size viewing/editing | Demand-paged viewing/overwrite editing with no application size cap; whole-file mapping and platform limits apply |
| Assembler | Built-in assembly/patching | No assembly-text input; instruction bytes can be patched in the hex editor |
| Physical / logical drives | View and edit | Unsupported; nonregular files are rejected |
| Executable containers | NE, LE, LX, PE/PE32+, ELF/ELF64, Mach-O, TE | Automatic CPU detection for ELF, PE/TE, MZ, thin Mach-O and i386 NLM v4; header/import browser for PE/NE/LE/LX/NLM |
| Files open at once | Multiple files and history | One file from `argv[1]` |
| Addressing | File offsets and virtual-address navigation | File-offset cursor/gutter and absolute hex goto; decoders receive mapped runtime addresses; no VA goto |
| Search | Bytes, text, instruction patterns, multi-file replace | Byte and text search with forward/backward repeat; no wildcards, instruction patterns, replace or multi-file scope |
| Extensibility | HEM plugins, macros, Crypt interpreter | None |

LHiew combines byte-overwrite editing with architecture selection, executable
header/import browsing, file-offset/entry navigation, byte and text search, and
mapped disassembly addresses. Existing import names/ordinals can be edited in
place, with an automatic original-file backup. Insertion/deletion, assembly-text
input, search-and-replace, virtual-address goto, block operations, macros and
general symbol/export browsing remain gaps. See the
[editing workflow](docs/hex-editing.md) and [import workflow](docs/executable-imports.md).

## Product-level capabilities

Rows follow the advertised feature list for Hiew release VIII. They describe
whole capabilities rather than individual shortcuts, so a ⚠️ row usually means a
usable subset with a named limit rather than partial credit.

| Release VIII capability | LHiew | Current extent |
|---|---|---|
| View and edit files of unlimited size in text, hex, and disassembler modes | ⚠️ | Hex/ASCII overwrite editing reached from any view; no insertion/deletion, assembly input, or sliding mapping window |
| x86-64 disassembler and assembler (AVX instructions supported) | ⚠️ | Decoding through bundled Zydis; no assembler; no claim of exhaustive instruction validation |
| Physical and logical drive view and edit | ❌ | Regular files only; nonregular inputs are rejected |
| NE, LE, LX, PE/PE32+, ELF/ELF64, Mach-O, TE/TE64 format support | ⚠️ | Two subsystems with different coverage; see [§4](#4-executable-containers-and-addresses) |
| Netware Loadable Modules: NLM, DSK, LAN, … | ⚠️ | i386 NLM v4 header, code/data regions and imports; module type is not distinguished, so DSK/LAN are parsed as NLM |
| Navigate direct call/jmp instructions with a single click | ❌ | Branch targets are formatted in the operand text only; no markers, follow command or return stack |
| Search by instruction pattern in the disassembler | ❌ | Byte and text search only; no decoded-instruction matcher or wildcards |
| Built-in simple 64-bit decrypt/crypt system | ❌ | No transform interpreter, XOR mask or register state |
| Built-in powerful 64-bit calculator | ❌ | No expression evaluator or numeric panel |
| Block file operations: read, write, fill, copy, move, insert, delete, crypt | ❌ | No range selection, so no block operation exists |
| Search and replace across multiple files | ❌ | Search is read-only and scoped to the single open file |
| Keyboard macros | ❌ | No recorder, manager or playback |
| Unicode/UTF-8 support | ❌ | Rendering is one terminal cell per byte; non-ASCII bytes show as placeholders |
| Hiew External Module (HEM) support | ❌ | No plugin interface |
| ARMv6 disassembler | ⚠️ | ARM, Thumb and AArch64 through Capstone; a specific architecture revision is not asserted |

### 1. Large-file viewing and editing

[`read_file_in_editor()`](src/file_buffer.c) maps regular files rather than first
reading the entire file into a heap buffer. The operating system brings pages
into memory as accessed. Editing uses a private mapping and records modified
bytes; explicit save writes the changes without rewriting the complete file.
The offset gutter grows beyond eight hexadecimal digits, and disassembly uses a
viewport and bounded lookback.

The entire file must still fit in one virtual-address mapping. File-size types,
address space, operating-system limits, and mapping failures constrain supported
sizes, particularly on 32-bit builds. There is no application-defined file-size
cap or screen-sized editing restriction, but there is no sliding-window mapping.
Sparse-file regressions exercise editing beyond 4 GiB; they do not establish
support for every possible file size or a universal opening-time guarantee.

F3 enters hex editing; hexadecimal nibbles or Tab-selected printable ASCII
overwrite existing bytes. F5 accepts an absolute hexadecimal file offset, while
arrows, pages, Home/End, and Ctrl-Home/Ctrl-End reach other locations. F9 saves and
continues editing. Leaving or quitting with pending changes offers save, discard,
or continue. File length stays unchanged, and empty/read-only files remain
viewable without entering this editor.

Save verifies file identity/metadata, writes changed bytes, and flushes them.
It is not atomic: failed writes or flushing may leave partial changes on disk.
Pending changes remain after failure; discard reloads disk contents rather than
rolling back completed writes. Before entering editing, LHiew creates
`<filename>.backup`; the first independent regular backup is preserved across
saves and later sessions. Backup failures block editing. Sparse copies use
bounded memory, but copying nonsparse data takes time and disk space. General
undo, insertion, deletion, and resizing remain absent. Dirty pages/change records also consume
memory as more bytes are edited. See [save behavior and limits](docs/hex-editing.md#storage-conflicts-and-limits).

### 2. Disassembly, architectures, and assembly

[`disassembler.c`](src/disassembler.c) uses Zydis for x86 real-16, protected-16,
32-bit, and 64-bit modes. Zydis supplies decoding for supported AVX-family
encodings as well as legacy x86 instructions. This is backend capability, not a
claim that LHiew's tests cover every instruction, extension, or invalid encoding.
Earlier ad hoc AVX examples are not an exhaustive validation suite.

Capstone provides the non-x86 profiles. Priority families are ARM, Thumb,
AArch64, and RISC-V32/64 with compressed instructions, alongside x86. Additional
profiles cover MIPS, PowerPC, SPARC, SystemZ, M68K, eBPF, SuperH, TriCore, XCore,
TMS320C64x, Motorola 6809, and MOS 6502. These select specific modes and revisions;
they do not imply support for every processor or extension within a family.
The [profile table](docs/architectures.md#implemented-profiles) gives the scope.

While viewing, `Shift-F1` or `a` opens an architecture menu in every view. Arrows or `j`/`k` move
the selection; Page Up/Down scroll by a menu page; Home/End select the first/last
profile; Enter applies; Escape cancels.
Auto uses supported file headers. Manual selection preserves the selected file
byte and invalidates cached disassembly. `o` cycles x86 modes; other families use
the architecture menu.

Raw files cannot identify an ISA reliably. They retain the default x86 viewing
behavior until a profile is chosen; Auto on raw input uses x86-32. The assembly
status identifies raw fallback when space permits. Unknown and malformed headers
remain inspectable and show an explicit unsupported state instead of claiming a
detected CPU. The menu explains the detection result.

The selected instruction is centered when enough preceding instructions exist.
Page Up/Down moves by a screen of decoded instructions, with file-boundary
clamping. Invalid or truncated encodings produce `db XX` rows so navigation can
continue. Backward decoding remains heuristic without a known boundary within
the lookback. ARM/Thumb interworking and mapping symbols are not tracked;
mixed-mode locations require manual selection.

**Assembly-text editing remains missing.** The unused Zydis encoder is disabled
in the application build. Hex editing can patch instruction
bytes, but there is no instruction-input line, assembler, or general undo history.
Selecting an architecture changes decoding only. x86 output remains AT&T rather
than Intel.

### 3. Physical and logical drives

Drive access is not implemented. [`file_buffer.c`](src/file_buffer.c) rejects
nonregular files and terminates on `fstat` errors. The earlier bugs that continued
after the regular-file check and printed an ordinary `stdout` diagnostic in raw
terminal mode are fixed; devices are no longer treated as empty files.

Drive support would need platform-specific device sizing and an appropriate
access strategy. Writable access would additionally need an explicit write model
and error recovery. Regular disk-image files can already be opened, but no
filesystem or partition browser is provided.

### 4. Executable containers and addresses

[`binary.c`](src/binary.c) validates container metadata and identifies supported
CPUs, file-backed regions, and an entry location. The file-offset cursor remains
canonical. [`architecture.c`](src/architecture.c) applies detected or manual
profiles and exposes region mappings to the disassembler.

**Two subsystems cover containers, and their format lists differ.** `binary.c`
decides the decoder profile and the runtime addresses fed to it; it handles ELF,
PE, TE, MZ, thin Mach-O and NLM, and deliberately leaves NE, LE and LX without a
CPU mode or address mapping. [`executable.c`](src/executable.c) backs the `F8`
browser and parses PE, NE, LE, LX and NLM tables, with no ELF, Mach-O or TE
support. A format can therefore be fully browsable while its bytes still
disassemble under a manually chosen profile, or map cleanly for disassembly with
no table browser. The column below states which of the two applies.

| Container | Current behavior | Remaining limits |
|---|---|---|
| ELF32 / ELF64 | Detect CPU/mode/byte order; parse program/section mappings; resolve entry or an executable region | No symbols, relocation application, or header browser; selected mixed-mode variants unsupported |
| PE32 / PE32+ | Detect machine/mappings/entry; browse header, sections and standard imports; edit existing module/function names and ordinals | Bound imports browse-only; no delay-import decoding, exports or table rebuilding; ARM64EC/ARM64X hybrid decoding unsupported |
| TE | Detect machine and map sections/entry with stripped-header adjustment | No firmware-volume or UEFI metadata browser |
| Thin Mach-O32 / Mach-O64 | Detect CPU/byte order; map sections/segments and LC_MAIN entry | No full LC_UNIXTHREAD entry decoder or symbol browser; executable-region fallback where available |
| DOS MZ | Select real-16 and resolve header/CS:IP entry | No DOS loader emulation or relocation application |
| Universal Mach-O | Recognize and validate slice table, report unsupported | No slice chooser or per-slice state |
| NE / LE / LX | Browse headers, segment/object records, module names and imported name/ordinal fixups; edit existing fields | No automatic decoding-mode/address mapping; no compressed-page expansion or entry-table forwarders; LE/LX fixup checksums make imports read-only |
| Raw binaries / unrecognized containers | Permit byte viewing and manual ISA selection | No reliable automatic CPU identification or load-address mapping |
| NLM | i386 v4 x86-32 detection, entry and code/data bounds; primary dependency/global-import browser and exact-length name edits | Runtime addresses unknown, so decoder uses file offsets; other CPUs/versions and shared-image import extensions unsupported |

F8 (or view-mode `b`) opens the executable browser. Tab selects imports or
headers/regions, Enter jumps to the selected file record, F3 edits an existing
name/ordinal, and F9 saves. Names retain their byte length and ordinals their
field width; PE lookup/address ordinal fields are updated together where safe.
Malformed or incomplete results disable structured edits. No imports are added
or removed, and no checksums or executable signatures are regenerated. See the
[format coverage and limits](docs/executable-imports.md#format-coverage).

Entering assembly view from file offset zero jumps to the mapped entry, or to
the first file-backed executable region when the parser supplies that fallback.
`e` opens the same location directly. A file without a usable entry remains at
its current offset. The fallback is a viewing location, not a claim about where
the program will execute.

For mapped instructions, decoders receive the region's runtime address. x86,
ARM, and AArch64 branch operands formatted as absolute targets can therefore
reflect executable virtual addresses instead of file-relative targets. Backend
formatting is retained: for example, Capstone's RISC-V `jal 8` is a relative
operand even when decoding receives a mapped address. **The gutter and cursor
still use file offsets.** There is no VA/RVA gutter, virtual-address goto,
branch-follow command, symbol lookup, or relocation application. Raw and
unmapped bytes use file offsets as decoder addresses.

The parser does not classify all bytes as code or data. Navigation into headers
or data still attempts disassembly under the selected profile. Packed/encrypted
payloads, embedded VM bytecode, mixed architectures, and additional ISA revisions
need separate handling. See [explicit limits and follow-up work](docs/architectures.md#explicit-limits-and-follow-up-work).

## What LHiew already has

| Capability | Hiew key | LHiew key | Notes |
|---|---|---|---|
| Switch Hex / Text / Code view | `Enter`, `F4` menu | `m` next, `Ctrl-M` previous | While viewing; no view-picker menu |
| Edit existing bytes | `F3` | `F3`, hex digits, `Tab` for ASCII | Enters hex editing from any view; overwrite only |
| Save changes | `F9` in editor | `F9` | Saves changed bytes and remains editing |
| Preserve original bytes | — | Automatic before F3 edits | First `<filename>.backup` retained across sessions; failure blocks editing |
| Browse executable headers/imports | `F8` | `F8`, or view-mode `b` | PE/NE/LE/LX/i386 NLM v4; Tab changes table, Enter opens bytes |
| Edit existing import fields | Within executable UI | Browser `F3`, then Enter | Exact-length names and existing-width ordinals; F9 saves |
| Leave editor | context-dependent | `Esc` or `F10` | Remains in hex view; unsaved changes prompt save/discard/continue |
| Goto file offset | `F5` | `F5`, or `g` while viewing | Absolute hexadecimal file offsets only |
| Search for bytes or text | `F7` | `F7`, or `s` while viewing | `Tab` picks hex pairs or literal text; up to 64 bytes; also available while editing |
| Repeat the last search | `Shift-F7`, `Ctrl-Enter` | `Shift-F7`; `n`/`N` while viewing | `n` forward, `N` backward; `Shift-F7` reuses the last direction |
| Row/file boundaries | `Home`/`End`, `Ctrl-Home`/`Ctrl-End` | same | First/last byte in row or file |
| Change x86 opcode size | `Ctrl-F1` | `o` | real-16, protected-16, 32, 64; sets a manual architecture choice |
| Choose architecture | `Shift-F1` | `Shift-F1` or `a` | Auto plus 32 manual profiles; supported terminal sequence variants |
| Open entry location | Within executable header UI | `e` | Detected entry or executable-region fallback; separate from browser record jumps |
| Cursor movement | arrows | arrows; `h/j/k/l` while viewing | Printable keys become input while editing |
| Page up/down | `PgUp` / `PgDn` | `PgUp` / `PgDn` | Instruction-aware paging in code view |
| Hex dump with ASCII pane | — | built-in | Offset, hex, ASCII, and selected-byte highlight |
| Centered disassembly | — | built-in | Context above/below selected instruction where available |
| Status bar | partly `Ctrl-Alt` | built-in | Filename, mode, offset, and architecture selection state when space permits |
| Quit | `Esc` / `F10` | `Ctrl-Q` | Unsaved-edit prompt when needed; also works in architecture menu |
| Live terminal resize | — | built-in | Minimum 24×5; clipped content/menus; navigation pauses below minimum |

## Category 1 — Working with blocks

**No block commands are implemented.** LHiew highlights a selected byte or
instruction, but has no multi-byte range selection or marked-block state.

| Key | Feature | Status |
|---|---|---|
| `*` | Mark/unmark block | ❌ |
| `Ctrl-*` | Add whole file to block | ❌ |
| `Alt-*` | Resize block to current offset | ❌ |
| `Alt-M` | Assign block colour, persisted in `.cmarkers` | ❌ |
| `Shift-Alt-M` | Assign random colour | ❌ |
| `Alt-N` | Jump to next/previous marked block | ❌ |
| `[` / `]` | Move to block start/end | ❌ |
| `Ins` | Toggle insert/overwrite mode | ❌ |
| `F2` | Write block to file | ❌ |
| `Ctrl-F2` | Read block from file, insert or overwrite | ❌ |
| `Alt-F2` | Fill block with pattern or NOP current instruction | ❌ |
| `Shift-F2` | Delete block and truncate file | ❌ |
| `Shift-F5` / `Shift-F6` | Copy/move block | ❌ |
| `Shift-F4` | Print block to file or clipboard | ❌ |

## Category 2 — Navigating around files

View cycling, cursor movement, paging, file-offset goto, boundary jumps, byte
and text search, and direct entry navigation are available. Virtual/relative
goto, instruction-pattern search and replace remain missing.

| Key | Feature | Status |
|---|---|---|
| `F5` | Goto offset: absolute, relative, virtual address, hex/decimal input | ⚠️ F5 or view-mode `g` accepts absolute hex file offsets; no relative/VA/decimal expression input |
| `F7` | Search bytes, strings, or assembled instructions | ⚠️ F7 or view-mode `s` searches hex-pair or literal text patterns up to 64 bytes; no wildcards, instruction patterns, block scope or replace |
| `Ctrl-Enter` / `Shift-F7` | Repeat last search | ✅ Shift-F7 repeats in the recorded direction; view-mode `n`/`N` force forward/backward |
| `Alt-F7` | Toggle search direction | ⚠️ `n`/`N` record the direction for subsequent repeats; the prompt shows it, but no `Alt-F7` toggle exists |
| `Alt-F8` | Translation table / string encoding | ❌ |
| `Alt-F6` | Strings dialog with length/encoding/offset/filter controls | ❌ |
| `F6` / `Ctrl-F6` | Find code references to current location | ❌ |
| `Ctrl-Home` / `Ctrl-End` | Jump to start/end of file | ✅ first/last existing byte; Home/End move within the current row |
| `BkSp` | Return to previous location | ❌ |
| `+` `-` `Alt--` `Alt-0` `Alt-1..8` | Bookmark stack | ❌ |
| `Ctrl-.` / `Ctrl-0..8` | Record/play macros | ❌ |
| `;` | Comment current location | ❌ |
| `F12` / `Shift-F12` | Names window; name locations; import/export symbols | ⚠️ F8 browses stored import names/ordinals; no general names window, user labels or exports |
| `Enter`, `F4` | Cycle or pick view mode | ⚠️ cycling via `m`/`Ctrl-M`; no view-picker menu |
| `Esc` | Exit without touching timestamp | ⚠️ Esc cancels prompts or leaves editing; Ctrl-Q quits; saving writes file contents rather than promising timestamp preservation |
| `Tab`, `Ctrl-BkSp`, `Ctrl-F11`/`F12`, `F9` | File history/manager, next/previous argument, open file | ❌; one `argv[1]` file |
| `Alt-=` | Programmer calculator with cursor-relative reads | ❌ |
| `Ctrl-Alt` | File/system information panel | ⚠️ basic status bar only; no dedicated information panel |
| `Alt-P` | Text screenshot command | ❌; repository screenshots are captured externally |
| `Alt-B` | Toggle beeps | ❌ |

## Category 3 — Features specific to executables

CPU selection, container detection, entry navigation, mapped instruction
addresses and a bounded executable browser are implemented. Structured editing
is limited to existing import names/ordinals; broader header editing and analysis
remain absent.

| Key | Feature | Status |
|---|---|---|
| `F8` | Header viewer/editor with entry, section, import, and export navigation | ✅ for browsing, ⚠️ for editing: F8/b exposes PE/NE/LE/LX/NLM header/region/import records and `e` reaches the detected entry; edits are limited to existing import names/ordinals, with no exports or general header fields |
| `1`–`9`, `A` | Follow branch/call targets and show direction markers | ❌; mapped operand text does not provide navigation |
| `/` | Re-synchronize disassembly from cursor | ⚠️ bounded lookback, cached boundaries, and ISA alignment; no forced-resync command |
| `Ctrl-F1` | Cycle opcode size | ✅ equivalent `o` for x86 |
| `Shift-F1` | Change architecture | ✅ architecture menu; `a` fallback; ISA/mode limits apply |
| `Alt-F3` / `Ctrl-F7` | Crypt dialog / instruction interpreter for data transforms | ❌ |
| `F11` | HEM plugin menu | ❌ |
| `F7` `F7` | Instruction-pattern search with `?` and `;;` wildcards | ❌ byte/text search only; matching decoded instructions needs its own design |
| — | Import names resolved onto call/jmp operands | ❌ the `F8` browser reads the tables, but nothing annotates disassembly operands with the name behind an indirect call |
| — | VA/RVA display beside file offsets | ❌ gutter remains file-offset based; decoders use mapped runtime addresses where available |

## Editing — implemented overwrite workflow and remaining gaps

Opening a file for viewing does not require write access. F3 verifies/reopens the
same nonempty regular file for writing and enters hex editing. Pending bytes
stay private until explicit save. A first `.backup` is secured before edit mode
and retained across later sessions. The [editing guide](docs/hex-editing.md) gives
a reproducible walkthrough and identifies which historical actions remain absent.

| Key | Feature | Status |
|---|---|---|
| `F3` | Enter edit mode; switch hex/character/opcode input | ⚠️ hex/printable ASCII overwrite with Tab; no opcode/assembler input |
| `Shift-F3` | Insert zero bytes, extending file | ❌ |
| `F9` | Save changes | ✅ saves changed bytes without changing file length; remains editing |
| `F10` | Exit and update timestamp | ⚠️ leaves editing in hex view; prompts if dirty; Ctrl-Q exits the application |
| `Ins` | Insert versus overwrite | ❌ |
| — | Inline assembler: instruction text to patched bytes | ❌ |
| — | Discard pending edits / general undo | ⚠️ discard all pending changes; no per-edit undo history or rollback of completed disk writes |
| — | Automatic original-file backup | ✅ creates `<filename>.backup` before editing; never replaces an existing independent regular backup; no built-in restore command |

## Historical additions: Hiew 6.03

The following inventory comes from the
[6.03 article](http://unicornix.spb.ru/docs/prog/heap/hiew.htm).
LHiew statuses were checked against [`main.c`](src/main.c),
[`input.c`](src/input.c), [`editor.c`](src/editor.c),
[`render.c`](src/render.c), [`binary.c`](src/binary.c), and
[`file_buffer.c`](src/file_buffer.c), [`executable_browser.c`](src/executable_browser.c),
the format parsers, [`hex_edit.c`](src/hex_edit.c), and
[`file_backup.c`](src/file_backup.c). These rows expand the existing categories;
they are not additional completed features or a shortcut count.

| Hiew 6.03 capability | LHiew status / remaining work |
|---|---|
| File navigator | ❌ No file/directory picker; no-argument startup shows an empty viewer |
| Sorting, filters, hidden files | ❌ No application file list to sort or filter |
| Filename completion | ❌ No filename-entry prompt, incremental matching, or completion |
| Directory bookmarks | ❌ No saved directory locations or directory navigation |
| Multiple files; recursive `/S` | ❌ Only `argv[1]` is opened; shell wildcard expansion does not provide in-app multi-file support |
| New-file creation | ❌ A missing input file fails to open; no creation command |
| Existing-file editor; F3/F9 | ⚠️ hex/ASCII overwrite and explicit save are available across the file; no insertion/deletion, instruction assembler, or general undo |
| Session persistence; `/SAV` | ❌ Mode, cursor, bookmarks, and file history are not saved across launches |
| Startup preferences; `/INI` | ❌ No option/configuration parser; initial view is fixed to text |
| Manual rebasing; `Ctrl-F5` | ❌ Header mappings exist, but raw firmware/ROM users cannot set a load address or a cursor-relative origin |
| NLM recognition | ⚠️ primary i386 v4 image, entry/code/data and global imports; other NLM variants unsupported |
| Import/ordinal annotations | ⚠️ stored module/function names and ordinals appear in the executable browser; no import annotation in disassembly or ordinal-name database |
| VxD/VMM annotations | ❌ No service-call interpretation or service-name tables; generic x86 decoding cannot supply these semantics |
| PE directories/flags | ⚠️ header summary, section RVA/size/raw flags and standard imports are visible; delay imports have raw-directory navigation; no complete directory/decoded-flag editor |
| MZ overlays/header maintenance | ❌ No overlay navigation or header repair; current MZ support detects mode and maps the load module/entry |
| Instruction patterns: `?`, `;`, `;;` | ❌ No decoded-instruction pattern matcher; this needs separate design from byte/string search |
| Byte/instruction scan steps | ⚠️ Byte search exists; there is no cross-reference engine and no selectable byte/instruction scan policy |
| Block-scoped search | ❌ Search scans the open file from the cursor in the selected direction; no marked-range scope or wrapping |
| Independent search continuation | ⚠️ The pattern and direction persist, but repeats resume from the cursor, so moving away loses the search position |
| Offset-based block I/O | ❌ No range import/export with a source/destination offset |
| Block transcoding | ❌ No encoding conversion during range import/export |
| Assembly expressions/prefix control | ❌ No assembly input, expression parser, or encoding-selection UI |
| Calculator result representations | ❌ No signed/unsigned, binary, or hexadecimal calculation panel |
| Crypt stepping/register reset | ❌ No transform interpreter, execution controls, or register state |
| XOR masks; saved Crypt programs | ❌ No data-transform mask or transform-program persistence |
| Wrap/tab/newline preferences | ⚠️ Text wraps to terminal width; tabs/newlines are displayed as control-byte placeholders, with no line-oriented interpretation or preference |
| Horizontal text scrolling | ❌ Text reflows; there is no horizontal viewport command or configurable column stride |
| Custom translation/casefold tables | ❌ No input/output translation tables or encoding-aware case-insensitive search |
| Colours/progress preferences | ⚠️ Selection highlighting and numeric offsets exist; no theme, progress display, or layout configuration |

**Version caveats from the article:** comments, general scripting/API and PE
header editing are absent; the calculator is 32-bit. Historical `Alt-F6` means
scan step, `0` branch return, and `Alt-F3` fill. These do not replace the
community-reference bindings above. Some F2/F10/entry bindings conflict internally;
they are not treated as verified shortcuts here.

This comparison records capabilities and gaps, not a requirement to reproduce
old DOS behavior, file formats, reported defects, or editing constraints.
The current overwrite editor provides explicit save/discard handling, but no
general undo or atomic recovery. A retained first `.backup` protects original
bytes. Its [workflow map](docs/hex-editing.md#relationship-to-the-hiew-603-article)
separates reachable editing actions from unrelated missing features. Future
address/search work should use validated container mappings rather than adopting
the article's example calculations as a parser specification.

## Suggested order of work

Priorities favor common desktop/mobile binaries and extending the existing
overwrite workflow before adding less common CPU families. The
[workflow scenarios](#workflow-scenarios) reorder some of this: block marking is
listed low as an individual command, but it is the first blocking step of two of
the three scenarios, and it also gates block export, fill, delete and search
scope. Treat it as the next structural piece rather than a later convenience.

1. **Deepen x86, ARM/Thumb/AArch64, and RISC-V coverage.** Add representative
   compiler output and executable variants, make decoder revision limits explicit,
   and address mixed-mode ARM and common hybrid/fat containers. Keep secondary
   profiles supported without presenting them as exhaustive ISA coverage.
2. **Extend navigation.** Absolute hex file-offset goto and row/file boundaries
   now exist. Add relative/VA goto, configurable raw-file load bases, jump history,
   and bookmarks. Reuse region mappings and report unmapped addresses.
3. **Search.** Byte and text patterns, forward/backward repeat, and searching
   during an edit session now exist in [`search.c`](src/search.c). Add wildcards,
   an independent repeat position, a progress/abort path for long scans on large
   mapped files, encoding choices, and replace. Instruction-pattern matching,
   byte/instruction scan policy and block scope remain separate capabilities.
4. **Extend executable browsing.** PE/NE/LE/LX/NLM header/import browsing and
   existing-field edits now exist. Add ELF/Mach-O/TE panels, exports, delay/shared
   import variants, full directory/flag views and broader mapping support.
   Import annotations in disassembly, checksum repair and table rebuilding need
   separate implementations.
5. **Branch following and explicit resynchronization.** Preserve target metadata,
   map targets back to file offsets, and add jump history. Account for indirect
   branches and instruction-mode changes; formatted operands alone are not enough.
6. **Block selection and export.** Add range marking, range navigation, and
   offset-based import/export as separate operations from single-byte overwrite.
   Once a range exists, block fill, copy/move, colour marking, block-scoped search
   and file-shortening delete become incremental rather than new subsystems.
7. **File/session management and preferences.** Add a file picker, multiple-file
   navigation, saved locations, startup configuration, and text/encoding options.
   Preserve the current byte-oriented view while defining a separate readable-text
   mode; do not silently change cursor offsets through text conversion.
8. **Device and mapping-window support.** Add platform-specific read-only device
   access if needed and sliding mappings for files that cannot fit one mapping.
9. **Extend editing and add assembly.** Overwrite, explicit save, and whole-pending
   discard and retained original-file backups now exist. Add general undo,
   stronger save recovery, insertion/deletion,
   and resizing before connecting an encoder to an instruction-input UI. New-file
   creation and data-transform tools also remain separate work.

## Regression evidence and limits

The refactoring consolidates bounded binary readers, NLM validation, prompt/menu
handling, file-state checks and buffer growth. It preserves the feature scope
described above. First-party C builds use strict warnings as errors; CI exercises GCC and
Clang Debug/Release configurations, including ASan/UBSan. GCC Release uses
`-Ofast`; see [build policy and validation](docs/development.md).


- [`tests/test_binary.c`](tests/test_binary.c) checks parser bounds, malformed and
  unsupported headers, executable mappings, entries, and container details.
- [`tests/test_architectures.c`](tests/test_architectures.c) loads deterministic
  files and checks exposed profiles, detected modes, mapped branch operands,
  override/Auto, invalid data, and navigation across instruction lengths.
- [`tests/architecture_fixtures/generate.py`](tests/architecture_fixtures/generate.py)
  reproduces serialized fixtures; `--check` verifies their bytes.
- [`tests/test_disassembler.c`](tests/test_disassembler.c) and
  [`tests/test_input.c`](tests/test_input.c) cover centering, boundaries, resize,
  cursor preservation, and instruction-aware paging.
- [`tests/test_terminal_resize.py`](tests/test_terminal_resize.py) exercises actual
  key sequences, Shift-F1 variants, menu apply/cancel/pages, entry jumps, common
  non-x86 detection, unsupported status, and resize.
- [`tests/test_file_buffer.c`](tests/test_file_buffer.c) checks ordinary file
  opening, mapped contents, empty files, failure-preserving reloads, and reopening
  an owned pathname.
- [`tests/test_hex_edit.c`](tests/test_hex_edit.c) and
  [`tests/test_hex_editor.py`](tests/test_hex_editor.py) check explicit
  save/discard, conflicts and failures, reachable editing controls, and sparse-file
  overwrites beyond 4 GiB; see [editing verification](docs/hex-editing.md#verification).
- [`tests/test_search.c`](tests/test_search.c) checks pattern compilation
  (hex pairs, separators, incomplete nibbles, the length limit, literal text) and
  forward/backward scanning at file boundaries and on impossible spans, including
  overlapping matches and direction retention after unsuccessful repeats.
  [`tests/test_search_ui.py`](tests/test_search_ui.py) drives `F7`, `Tab`,
  repeat, cancel, rejected input and searching inside an edit session through a
  real terminal.
- [`tests/test_file_backup.c`](tests/test_file_backup.c) checks exact contents,
  retained first backups, alias rejection, failed-copy cleanup and sparse copying
  beyond 4 GiB.
- [`tests/test_executable_pe.c`](tests/test_executable_pe.c),
  [`tests/test_executable_ne.c`](tests/test_executable_ne.c),
  [`tests/test_executable_linear.c`](tests/test_executable_linear.c) and
  [`tests/test_executable_nlm.c`](tests/test_executable_nlm.c) cover serialized
  import records, boundaries, field spans, encodings and unsupported cases.
  The [executable guide](docs/executable-imports.md#fixtures-and-verification)
  describes generated examples and terminal workflow coverage.

These are targeted regressions using bounded fixtures. They do not establish
complete ISA/container coverage, all-file-size support, or full Hiew editing
equivalence. Run the full CTest suite for the current result; this document
does not freeze a test count or pass percentage.

## Notes on fidelity

- Status normally shows decimal cursor offset/file size; the gutter uses hex file
  offsets, and a narrow status may fall back to hex.
- x86 disassembly uses AT&T syntax; there is no Intel/AT&T preference yet.
- Decoders receive mapped runtime addresses where available, with native operand
  formatting retained. Cursor, gutter, paging, and selection still use file bytes.
- `Ctrl-M` commonly arrives as the same byte as Enter. In view mode it cycles
  backward through views; in prompts/menus it applies the entered choice.
- Shift-F1 terminal encodings vary. Supported complete CSI forms are accepted;
  `a` is the view-mode fallback. Escape cancels prompts/menus or leaves editing.
- `DEL_KEY` is decoded but has no dispatched action. Keyboard recognition alone
  does not imply an editing feature.
- View-mode search uses `s` to open the prompt and `n`/`N` to repeat. These are
  LHiew additions, not Hiew bindings, chosen because printable keys are already
  the view-mode fallbacks for `g`, `a`, `b` and `e`. Hiew assigns `/` to
  disassembly resynchronization, so `/` is deliberately left unbound rather than
  reused for search. `F7` and `Shift-F7` match Hiew and also work while editing,
  where letters are data.
- Search reads the mapped bytes, so during an edit session it matches pending
  changes rather than the on-disk file. Scanning is a linear pass over the
  mapping with no progress indicator or abort key; a miss on a very large file
  blocks redraws until the scan completes.

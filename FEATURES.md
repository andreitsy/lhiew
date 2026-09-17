# LHiew vs. Hiew — Feature Comparison

**LHiew audit date: 2026-09-17.** Comparison items follow the
[Hiew Documentation Project](https://taviso.github.io/hiewdocs/index.htm).
LHiew status is based on the current source and regression evidence linked below.
Tables group shortcuts and repeat some capabilities in different contexts; they
are not a count of unique shortcuts or a percentage of Hiew support.

✅ means implemented or an equivalent binding; ⚠️ means the described subset is
implemented; ❌ means the listed command or capability is absent.

Common desktop and mobile targets are the priority: **x86, ARM/Thumb/AArch64, and
RISC-V**. Other exposed profiles remain supported through selected decoder modes,
with limits on ISA revisions and executable variants. See the
[architecture analysis, implementation plan, and limits](docs/architectures.md).

## Summary

| | Hiew comparison item | LHiew |
|---|---|---|
| Role | Binary editor | Read-only binary viewer; mapping uses `PROT_READ` |
| Views | Hex, Text, Code | Hex, Text, Code |
| Architectures | x86 modes and architecture selection with `Shift-F1` | Zydis for x86; Capstone for selected native ISAs; Auto plus 32 manual profiles |
| Disassembly syntax | Intel for x86 | AT&T for x86; backend syntax for other ISAs; no syntax toggle |
| Large files | Advertised unlimited-size viewing/editing | Demand-paged whole-file mapping, within address-space and platform limits |
| Assembler | Built-in assembly/patching | None; no byte editing, save, or assembly-text input |
| Physical / logical drives | View and edit | Unsupported; nonregular files are rejected |
| Executable containers | NE, LE, LX, PE/PE32+, ELF/ELF64, Mach-O, TE | Detects ELF32/64, PE32/PE32+, TE, DOS MZ, thin Mach-O32/64; selected others explicitly unsupported |
| Files open at once | Multiple files and history | One file from `argv[1]` |
| Addressing | File offsets and virtual-address navigation | File-offset cursor/gutter; decoders receive mapped runtime addresses; no VA goto |
| Extensibility | HEM plugins, macros, Crypt interpreter | None |

LHiew now combines a read-only viewer with architecture selection, executable
header detection, entry navigation, and mapped disassembly addresses. Editing,
search, arbitrary goto, symbols, and a header browser remain substantial gaps.

## Product-level capabilities

| Capability | LHiew | Current extent |
|---|---|---|
| View and edit large files in text, hex, and code views | ⚠️ | Demand-paged viewing; no editing or sliding mapping window |
| x86-64 disassembler and assembler, including AVX-family encodings | ⚠️ | Decoding through bundled Zydis; no assembler; no claim of exhaustive instruction validation |
| Physical and logical drive view/edit | ❌ | Regular files only |
| Executable-format support | ⚠️ | CPU detection, mapped regions, and entry navigation for supported containers; no header/import/export UI |

### 1. Large-file viewing and editing

[`read_file_in_editor()`](src/file_buffer.c) maps regular files with
`mmap(..., PROT_READ, MAP_PRIVATE, ...)`. It does not first read the entire file
into a heap buffer. The operating system brings mapped pages into memory as they
are accessed. The offset gutter grows beyond eight hexadecimal digits as needed,
and disassembly uses a viewport and bounded lookback.

The entire file must still fit in one virtual-address mapping. File-size types,
address space, operating-system limits, and mapping failures constrain supported
sizes, particularly on 32-bit builds. There is no sliding-window implementation,
and this audit makes no universal file-size or opening-time guarantee.

Editing is absent. Overwrite, insert/delete, undo, save, and failure recovery all
need an explicit edit path. A piece table or another representation of original
and inserted spans would be useful for large-file insertion/deletion; making the
mapping writable would not by itself supply those behaviors.

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

`Shift-F1` or `a` opens an architecture menu in every view. Arrows or `j`/`k` move
the selection; Page Up/Down scroll by a menu page; Enter applies; Escape cancels.
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

**Assembly and editing remain missing.** Zydis's encoder is present in the
dependency but unused by the application. There is no instruction-input line,
patch operation, writable edit buffer, save command, or undo history. Selecting
an architecture changes decoding only. x86 output remains AT&T rather than Intel.

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

| Container | Current behavior | Remaining limits |
|---|---|---|
| ELF32 / ELF64 | Detect CPU/mode/byte order; parse program/section mappings; resolve entry or an executable region | No symbols, relocation application, or header browser; selected mixed-mode variants unsupported |
| PE32 / PE32+ | Detect COFF machine; parse sections/image base; map entry RVA to file offset | No imports/exports UI; ARM64EC/ARM64X hybrid decoding unsupported |
| TE | Detect machine and map sections/entry with stripped-header adjustment | No firmware-volume or UEFI metadata browser |
| Thin Mach-O32 / Mach-O64 | Detect CPU/byte order; map sections/segments and LC_MAIN entry | No full LC_UNIXTHREAD entry decoder or symbol browser; executable-region fallback where available |
| DOS MZ | Select real-16 and resolve header/CS:IP entry | No DOS loader emulation or relocation application |
| Universal Mach-O | Recognize and validate slice table, report unsupported | No slice chooser or per-slice state |
| NE / LE / LX | Recognize as unsupported | No executable mapping or decoding-mode inference for these formats |
| Raw binaries / unrecognized containers | Permit byte viewing and manual ISA selection | No reliable automatic CPU identification or load-address mapping |

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
| Switch Hex / Text / Code view | `Enter`, `F4` menu | `m` next, `Ctrl-M` previous | Cycles views; no view-picker menu |
| Change x86 opcode size | `Ctrl-F1` | `o` | real-16, protected-16, 32, 64; sets a manual architecture choice |
| Choose architecture | `Shift-F1` | `Shift-F1` or `a` | Auto plus 32 manual profiles; supported terminal sequence variants |
| Open entry location | Within executable header UI | `e` | Detected entry or executable-region fallback; no header UI |
| Cursor movement | arrows | arrows plus `h` `j` `k` `l` | File-byte cursor preserved through view changes and resize |
| Page up/down | `PgUp` / `PgDn` | `PgUp` / `PgDn` | Instruction-aware paging in code view |
| Hex dump with ASCII pane | — | built-in | Offset, hex, ASCII, and selected-byte highlight |
| Centered disassembly | — | built-in | Context above/below selected instruction where available |
| Status bar | partly `Ctrl-Alt` | built-in | Filename, mode, offset, and architecture selection state when space permits |
| Quit | `Esc` / `F10` | `Ctrl-Q` | Also works inside architecture menu |
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

View cycling, cursor movement, paging, and direct entry navigation are available.
General offset/address goto and search are still missing.

| Key | Feature | Status |
|---|---|---|
| `F5` | Goto offset: absolute, relative, virtual address, hex/decimal input | ❌; `e` is a fixed entry shortcut, not arbitrary goto |
| `F7` | Search bytes, strings, or assembled instructions | ❌ |
| `Ctrl-Enter` / `Shift-F7` | Repeat last search | ❌ |
| `Alt-F7` | Toggle search direction | ❌ |
| `Alt-F8` | Translation table / string encoding | ❌ |
| `Alt-F6` | Strings dialog with length/encoding/offset/filter controls | ❌ |
| `F6` / `Ctrl-F6` | Find code references to current location | ❌ |
| `Ctrl-Home` / `Ctrl-End` | Jump to start/end of file | ❌; no Home/End key action |
| `BkSp` | Return to previous location | ❌ |
| `+` `-` `Alt--` `Alt-0` `Alt-1..8` | Bookmark stack | ❌ |
| `Ctrl-.` / `Ctrl-0..8` | Record/play macros | ❌ |
| `;` | Comment current location | ❌ |
| `F12` / `Shift-F12` | Names window; name locations; import/export symbols | ❌ |
| `Enter`, `F4` | Cycle or pick view mode | ⚠️ cycling via `m`/`Ctrl-M`; no view-picker menu |
| `Esc` | Exit without touching timestamp | ❌ as a binding; Escape cancels architecture menu, `Ctrl-Q` quits |
| `Tab`, `Ctrl-BkSp`, `Ctrl-F11`/`F12`, `F9` | File history/manager, next/previous argument, open file | ❌; one `argv[1]` file |
| `Alt-=` | Programmer calculator with cursor-relative reads | ❌ |
| `Ctrl-Alt` | File/system information panel | ⚠️ basic status bar only; no dedicated information panel |
| `Alt-P` | Text screenshot command | ❌; repository screenshots are captured externally |
| `Alt-B` | Toggle beeps | ❌ |

## Category 3 — Features specific to executables

CPU selection, container detection, entry navigation, and mapped instruction
addresses are implemented. Header editing and higher-level analysis are not.

| Key | Feature | Status |
|---|---|---|
| `F8` | Header viewer/editor with entry, section, import, and export navigation | ⚠️ detection/mapping and `e` entry shortcut; no `F8` UI, section browser, imports, or exports |
| `1`–`9`, `A` | Follow branch/call targets and show direction markers | ❌; mapped operand text does not provide navigation |
| `/` | Re-synchronize disassembly from cursor | ⚠️ bounded lookback, cached boundaries, and ISA alignment; no forced-resync command |
| `Ctrl-F1` | Cycle opcode size | ✅ equivalent `o` for x86 |
| `Shift-F1` | Change architecture | ✅ architecture menu; `a` fallback; ISA/mode limits apply |
| `Alt-F3` / `Ctrl-F7` | Crypt dialog / instruction interpreter for data transforms | ❌ |
| `F11` | HEM plugin menu | ❌ |
| — | VA/RVA display beside file offsets | ❌ gutter remains file-offset based; decoders use mapped runtime addresses where available |

## Editing — the structural gap

[`file_buffer.c`](src/file_buffer.c) opens files with `"rb"` and maps them with
`PROT_READ, MAP_PRIVATE`. No application command writes modified file contents.

| Key | Feature | Status |
|---|---|---|
| `F3` | Enter edit mode; switch hex/character/opcode input | ❌ |
| `Shift-F3` | Insert zero bytes, extending file | ❌ |
| `F9` | Save changes | ❌ |
| `F10` | Exit and update timestamp | ❌ |
| `Ins` | Insert versus overwrite | ❌ |
| — | Inline assembler: instruction text to patched bytes | ❌ |

## Suggested order of work

Priorities favor useful inspection of common desktop/mobile binaries before
expanding less common CPU families or introducing file mutation.

1. **Deepen x86, ARM/Thumb/AArch64, and RISC-V coverage.** Add representative
   compiler output and executable variants, make decoder revision limits explicit,
   and address mixed-mode ARM and common hybrid/fat containers. Keep secondary
   profiles supported without presenting them as exhaustive ISA coverage.
2. **General navigation.** Add file-offset/VA goto and Home/End, then jump history
   and bookmarks. Reuse region mappings and report unmapped addresses.
3. **Search.** Add byte/string search, repeat, and direction controls; define
   behavior for large mapped files and encoding choices.
4. **Header and region browser.** Expose parsed headers, entry, sections/segments,
   and mapping information. Parsing exists; UI and broader format handling remain.
   Symbols, imports, exports, and relocations need their own parsing/navigation.
5. **Branch following and explicit resynchronization.** Preserve target metadata,
   map targets back to file offsets, and add jump history. Account for indirect
   branches and instruction-mode changes; formatted operands alone are not enough.
6. **Block selection and export.** Add read-only range marking, range navigation,
   and export to a separate file before introducing in-place edits.
7. **Device and mapping-window support.** Add platform-specific read-only device
   access if needed and sliding mappings for files that cannot fit one mapping.
8. **Editing and assembly.** Define overwrite/insert/delete, undo, safe save, and
   failure recovery before connecting an encoder to an instruction-input UI.
   This changes LHiew's current read-only contract.

## Regression evidence and limits

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
  opening, read-only mapping, and empty files.

These are targeted regressions using bounded fixtures. They do not establish
complete ISA/container coverage, all-file-size support, editing support, or
Hiew equivalence. Run the full CTest suite for the current result; this document
does not freeze a test count or pass percentage.

## Notes on fidelity

- Status normally shows decimal cursor offset/file size; the gutter uses hex file
  offsets, and a narrow status may fall back to hex.
- x86 disassembly uses AT&T syntax; there is no Intel/AT&T preference yet.
- Decoders receive mapped runtime addresses where available, with native operand
  formatting retained. Cursor, gutter, paging, and selection still use file bytes.
- `Ctrl-M` commonly arrives as the same byte as Enter. Outside the architecture
  menu it cycles backward through views; inside it applies the menu choice.
- Shift-F1 terminal encodings vary. Supported complete CSI forms are accepted;
  `a` is the fallback. Escape only cancels the architecture menu.
- `DEL_KEY` is decoded but has no dispatched action. Keyboard recognition alone
  does not imply an editing feature.

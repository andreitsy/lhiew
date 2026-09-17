# LHiew vs. Hiew — Feature Comparison

Comparison of LHiew against the [Hiew Documentation Project](https://taviso.github.io/hiewdocs/index.htm),
which documents 48 top-level shortcuts plus a number of sub-commands referenced inline.

LHiew state was read from `src/` at the time of writing (`input.c` key dispatch,
`file_buffer.c` mapping flags, `disassembler.c` Zydis setup, `render.c` drawing).

## Summary

| | Hiew | LHiew |
|---|---|---|
| Role | Binary **editor** | Binary **viewer** (read-only `mmap`, `PROT_READ`) |
| Views | Hex, Text, Code | Hex, Text, Code |
| Architectures | x86 16/32/64 + others via `Shift-F1` | x86 only (Zydis), 4 decode modes |
| Disassembly syntax | Intel | AT&T |
| Max file size | Unlimited (sliding window) | Unlimited for viewing (demand-paged `mmap`, 8 GiB verified) |
| Assembler | Built in, AVX | None (Zydis encoder linked but unused) |
| Physical / logical drives | View and edit | Not supported — opens as empty |
| Container formats parsed | NE, LE, LX, PE/PE32+, ELF/ELF64, Mach-O, TE/TE64 | None |
| Files open at once | Many (history, `Tab`, `Ctrl-BkSp`) | One (`argv[1]`) |
| Addressing | File offset **and** virtual address | File offset only |
| Documented shortcuts covered | 48 | 2 equivalents (~4%) |
| Extensibility | HEM plugins, macros, Crypt mini-assembler | None |

The headline gap is not any single shortcut: **Hiew is an editor with search,
navigation and executable-format awareness; LHiew is a three-mode read-only dumper.**

## Product-level capabilities

The shortcut tables below come from the community docs. Hiew's own marketing feature
list makes four broader architectural claims that cut across every shortcut. They are
worth assessing separately, because they describe *what the program fundamentally is*
rather than what any key does.

| Claim | LHiew | One-line verdict |
|---|---|---|
| View **and edit** files of unlimited size in text, hex, disassembler modes | ⚠️ half | Viewing is genuinely unlimited; editing does not exist |
| x86-64 disassembler **& assembler**, AVX supported | ⚠️ half | Disassembler complete incl. AVX-512; no assembler |
| Physical & logical drive view & edit | ❌ | Devices silently open as an empty file |
| NE, LE, LX, PE/PE32+, ELF/ELF64, Mach-O, TE/TE64 formats | ❌ | No container is parsed; every file is flat bytes |

### 1. Unlimited file size

**What the claim is about.** This is an I/O architecture statement, not a limit constant.
It means the program never loads the file into RAM — it keeps a window onto the file and
pages content in as the cursor moves, so a 50 GB file opens as fast as a 50 KB one, and
the same is true in all three view modes.

**Where LHiew stands — viewing: already there.** `read_file_in_editor()` maps the file with
`mmap(..., PROT_READ, MAP_PRIVATE, fd, 0)` and never copies it. The kernel demand-pages it,
which buys the whole property for free on 64-bit. Measured on an 8 GiB sparse file:

```
num_bytes    = 8589934592 (8.00 GiB)
offset_width = 9 hex digits     # editor_offset_width() grows automatically
numrows      = 536870913
open + read first and last byte: 0.5 s wall (almost all process startup)
```

Rendering scales too: `draw_row_hex`/`draw_row_disassembler` only ever touch the rows on
screen, and offsets widen past 8 digits on their own.

Real ceilings are the address space (a 32-bit build dies above ~2–3 GB, since the whole
file must fit one mapping) and the lack of `MAP_NORESERVE`/windowing — Hiew's sliding
window has neither limit.

**Where LHiew stands — editing: absent, and it is the hard half.** Overwriting bytes in a
huge file is easy (`PROT_WRITE` + `msync`). What the claim really promises is *insert and
delete* in the middle of a multi-gigabyte file (Hiew's `Shift-F3`, `Ctrl-F2`, `Shift-F2`),
which cannot be done by rewriting the file — it needs a **piece table / extent list** that
describes the result as an ordered set of spans over the original mapping plus an edit
buffer, flattened only on save. That data structure is the single largest architectural
change on this list, and it would replace `global_cfg.file` as the way every module reads
bytes.

### 2. x86-64 disassembler & assembler, AVX

**What the claim is about.** Two independent engines. *Decode* (bytes → text) is what a
viewer needs; *encode* (text → bytes) is what makes patching practical — you type
`jmp 0x401000` or `nop` and the editor writes the opcodes. AVX matters because VEX and
EVEX prefixes are variable-length and heavily re-use opcode space; a decoder that predates
them mis-decodes modern code and then desynchronizes for the rest of the screen.

**Disassembler: complete, AVX included.** Verified by driving `disassemble_block()` directly
with hand-encoded instructions in 64-bit mode:

| Bytes | Extension | LHiew output |
|---|---|---|
| `c5 ec 58 d9` | AVX (VEX.256) | `vaddps %ymm1, %ymm2, %ymm3` |
| `c4 e2 7d 58 c1` | AVX2 | `vpbroadcastd %xmm1, %ymm0` |
| `c4 e2 6d 98 d9` | FMA | `vfmadd132ps %ymm1, %ymm2, %ymm3` |
| `62 f1 6c 48 58 d9` | AVX-512 (EVEX) | `vaddps %zmm1, %zmm2, %zmm3` |
| `62 f1 7e c9 6f c1` | AVX-512 masking | `vmovdqu32 %zmm1, %zmm0 {%k1} {z}` |
| `62 f2 7d 49 92 14 08` | AVX-512 VSIB gather | `vgatherdpsl (%rax,%zmm1,1), %zmm2 {%k1}` |

Instruction lengths came back correct in every case, so the row boundaries that paging and
cursor movement depend on stay in sync. Zydis is also strict about encoding rules — a gather
using the same register as destination and index (`#UD`) is correctly rejected as `db 62`
rather than silently decoded.

Two deliberate divergences remain: LHiew formats **AT&T** where Hiew uses Intel
(`ZYDIS_FORMATTER_STYLE_ATT` in `disassembler.c`, a one-line change to expose as a toggle),
and LHiew is x86-only, with no equivalent of Hiew's `Shift-F1` architecture switch.

**Assembler: missing, but the engine is already linked in.** Zydis 4.0.0 ships an encoder
(`deps/zydis/include/Zydis/Encoder.h`, compiled into the `libZydis.a` LHiew already links).
Nothing calls it. The gap is therefore not the encoding engine but everything around it:
an input line, edit mode, and a writable buffer. It only becomes useful after §1's edit
path exists.

### 3. Physical & logical drive view & edit

**What the claim is about.** Treating a raw block device as a file — *physical* drive means
the whole disk including partition table and boot sector (`/dev/sda`, `/dev/nvme0n1`),
*logical* means a single filesystem volume (`/dev/sda1`, an LVM or device-mapper node).
This is what makes an editor usable for boot sectors, partition recovery, and filesystem
forensics.

**Where LHiew stands: not supported, and it fails quietly.** `read_file_in_editor()` checks
`S_ISREG` — but on failure it only prints and then carries on:

```c
if ((fstat(fd, &stbuf) != 0) || (!S_ISREG(stbuf.st_mode))) {
    printf("Cannot open file!\n");     /* no return */
}
global_cfg.num_bytes = stbuf.st_size;
```

Because `fstat` reports `st_size == 0` for block and character devices, execution reaches the
`if (!global_cfg.num_bytes)` early return and the editor comes up showing its empty-file
welcome screen. Observed on `/dev/zero`:

```
Cannot open file!
num_bytes = 0    mmap ptr = 0x0    numrows = 0
```

Two separate defects are visible here: the guard does not stop execution, and the message
goes to `stdout` with `printf` while the terminal is in raw mode — violating the
"all drawing goes through `append_buffer`" invariant in `CLAUDE.md`, so it corrupts the frame.

Supporting drives properly needs: accepting `S_ISBLK`, getting the real size from
`ioctl(fd, BLKGETSIZE64, &sz)` instead of `st_size`, sector-aligned access, and — for the
edit half — write-back at sector granularity. It also implies running as root, which is a
UX and safety question (a stray keypress on `/dev/sda` is unrecoverable) worth settling
before any of it is built.

### 4. Executable format support

**What the claim is about.** Parsing the *container* around the code, which yields four
things a flat hex viewer cannot: the **entry point** (where to start disassembling), the
**section/segment table** (which ranges are code vs data), **imports and exports** (which
library functions are called), and above all the **virtual-address ↔ file-offset mapping**.
That last one is what makes disassembly correct rather than merely plausible.

The named formats span forty years of executable history:

| Format | Origin | Relevance to a Linux clone |
|---|---|---|
| **NE** | New Executable — 16-bit Windows / OS2 | legacy |
| **LE / LX** | Linear Executable — OS/2 2.x, VxD drivers, DOS extenders | legacy |
| **PE / PE32+** | Portable Executable, 32- and 64-bit Windows | high — malware and cross-platform RE |
| **ELF / ELF64** | Unix, Linux, BSD | **highest** — the native case |
| **Mach-O** | macOS, iOS | medium |
| **TE / TE64** | Terse Executable — UEFI firmware images | niche but uniquely poorly served elsewhere |

**Where LHiew stands: nothing is parsed.** There is no format detection anywhere in `src/`.
A file is a byte array, the disassembler always starts from a heuristic lookback point
rather than a known entry point, and offsets are the only addressing.

The cost of this shows up concretely in branch targets. `disassemble_block()` passes the
**file offset** as Zydis's `runtime_address` argument:

```c
ZydisFormatterFormatInstruction(..., format_buffer, sizeof(format_buffer),
                                read_offset, ZYAN_NULL);
```

so a `call` at file offset 0 prints:

```
offset 0: call 0x0000000000000005
offset 5: jmp  0x000000000000001A
```

For a typical `x86-64` ELF loaded at base `0x400000`, every absolute target shown is wrong
by the image base, and none of them can be cross-referenced against a symbol table or a
disassembly from any other tool. Adding just an ELF64 parser — program headers for the
`p_vaddr`/`p_offset` mapping, `e_entry`, plus the section table — fixes the addressing for
the native platform and is the prerequisite for xrefs (`F6`), branch following (`1`–`9`),
and the Names window (`F12`).

## What LHiew already has

| Capability | Hiew key | LHiew key | Notes |
|---|---|---|---|
| Switch Hex / Text / Code view | `Enter`, `F4` menu | `m` (next), `Ctrl-M` (prev) | Equivalent behaviour, different binding; no `F4`-style picker menu |
| Change opcode size (16/32/64) | `Ctrl-F1` | `o` | LHiew cycles 4 modes: real-16, compat-16, compat-32, long-64 |
| Cursor movement | arrows | arrows + `h` `j` `k` `l` | vi-style keys are an LHiew addition |
| Page up/down | `PgUp` / `PgDn` | `PgUp` / `PgDn` | Instruction-aware paging in code view |
| Hex dump with ASCII pane | — | built-in | Offset + hex + ASCII, selected byte highlighted |
| Disassembly view | — | built-in | Zydis, AT&T, invalid bytes shown as `db XX` |
| Status bar (file, mode, offset) | partly `Ctrl-Alt` | built-in | Offsets shown in **decimal** unless narrow; Hiew is hex-first |
| Quit | `Esc` / `F10` | `Ctrl-Q` | |
| Live terminal resize | n/a (DOS/console) | built-in | Min 24x5, reflows and preserves `cur_byte` |

LHiew extras with no Hiew counterpart: vi-style `hjkl`, automatic reflow on terminal resize,
and a partially automatic disassembly re-sync (bounded lookback in `disassemble_block`).

## Category 1 — Working with blocks

**Implemented: 0 / 13.** LHiew has no selection concept at all.

| Key | Feature | Status |
|---|---|---|
| `*` | Mark/unmark block (visual-mode style selection) | ❌ |
| `Ctrl-*` | Add whole file to block | ❌ |
| `Alt-*` | Resize block to current offset | ❌ |
| `Alt-M` | Assign colour to block (persisted in `.cmarkers`) | ❌ |
| `Shift-Alt-M` | Assign random colour | ❌ |
| `Alt-N` | Jump to next/previous marked block | ❌ |
| `[` / `]` | Move to block start / end | ❌ |
| `Ins` | Toggle insert/overwrite mode | ❌ |
| `F2` | Write block to file | ❌ |
| `Ctrl-F2` | Read block from file (insert or overwrite) | ❌ |
| `Alt-F2` | Fill block with pattern / NOP current instruction | ❌ |
| `Shift-F2` | Delete block and truncate file | ❌ |
| `Shift-F5` / `Shift-F6` | Copy / move block | ❌ |
| `Shift-F4` | Print block to file or clipboard | ❌ |

## Category 2 — Navigating around files

**Implemented: 1 / 20** (mode switching). The two most-missed for a viewer are `F5` goto and `F7` search.

| Key | Feature | Status |
|---|---|---|
| `F5` | **Goto offset** — absolute, `+`/`-` relative, `.address` virtual, hex default with `t` decimal suffix | ❌ |
| `F7` | **Search** — hex bytes, string, or assembled instruction; `Tab` toggles entry mode | ❌ |
| `Ctrl-Enter` / `Shift-F7` | Repeat last search | ❌ |
| `Alt-F7` | Toggle search direction | ❌ |
| `Alt-F8` | Translation table / string encoding (ASCII vs UTF-16) | ❌ |
| `Alt-F6` | Strings dialog (min length, encoding, offsets, filter) | ❌ |
| `F6` / `Ctrl-F6` | Find code references (xrefs) to current location | ❌ |
| `Ctrl-Home` / `Ctrl-End` | Jump to start / end of file | ❌ — no `Home`/`End` decoding in `editor_read_key` |
| `BkSp` | Return to previous location (jump history) | ❌ |
| `+` `-` `Alt--` `Alt-0` `Alt-1..8` | Bookmark stack (8 slots, per-view) | ❌ |
| `Ctrl-.` / `Ctrl-0..8` | Record and play macros | ❌ |
| `;` | Comment current location | ❌ |
| `F12` / `Shift-F12` | Names window; name a location; import/export symbols | ❌ |
| `Enter`, `F4` | Cycle / pick view mode | ✅ as `m`, `Ctrl-M` (no menu) |
| `Esc` | Exit without touching timestamp | ❌ (`Ctrl-Q` only) |
| `Tab`, `Ctrl-BkSp`, `Ctrl-F11`/`F12`, `F9` | Multi-file: history, file manager, next/prev argv file, open file | ❌ — single `argv[1]` |
| `Alt-=` | 64-bit programmer calculator with `@B/@W/@D/@Q/@o/@O` cursor reads | ❌ |
| `Ctrl-Alt` | File/system info (full path, size, last error) | ❌ |
| `Alt-P` | Text screenshot of current screen | ❌ |
| `Alt-B` | Toggle beeps | ❌ |

## Category 3 — Features specific to executables

**Implemented: 1 / 7.** LHiew treats every file as a flat byte stream — it never parses a container format.

| Key | Feature | Status |
|---|---|---|
| `F8` | Header viewer/editor for PE / ELF / Mach-O, with `F5` entry point, `F6` sections, `F7` imports, `F9` exports | ❌ |
| `1`–`9`, `A` | Follow `jmp`/`call` branch targets, with ↑/↓ direction markers | ❌ |
| `/` | Re-synchronize disassembly from cursor | ⚠️ automatic lookback heuristic only, no user-forced resync |
| `Ctrl-F1` | Cycle opcode size | ✅ as `o` |
| `Shift-F1` | Change architecture (e.g. ARM) | ❌ — x86 only |
| `Alt-F3` / `Ctrl-F7` | Crypt dialog (mini x86 interpreter to transform data) | ❌ |
| `F11` | HEM plugin menu | ❌ |
| — | Virtual address / RVA display alongside file offsets | ❌ |

## Editing — the structural gap

Hiew's edit path has no LHiew counterpart: `src/file_buffer.c` opens with `"rb"` and maps
`PROT_READ, MAP_PRIVATE`, so nothing can be written back.

| Key | Feature | Status |
|---|---|---|
| `F3` | Enter edit mode (caret cursor, `Tab` between hex/char, or opcode/assembler in code view) | ❌ |
| `Shift-F3` | Insert N zero bytes, extending the file | ❌ |
| `F9` | Save changes | ❌ |
| `F10` | Exit and update timestamp | ❌ |
| `Ins` | Insert vs. overwrite | ❌ |
| — | Inline assembler (type an instruction, get bytes) | ❌ |

## Suggested order of work

Ranked by value-per-effort for a read-only viewer, before any editing work is attempted:

1. **`Ctrl-Home` / `Ctrl-End`** — currently unreachable; needs `Home`/`End` cases in `editor_read_key`
   alongside the existing `[3~`/`[5~`/`[6~` handling. Smallest possible win.
2. **`F5` goto offset** — requires a prompt/input line in `render.c` (the message bar can host it)
   and a number parser. Unlocks the whole "type a number, go there" workflow.
3. **`F7` search + `Ctrl-Enter` repeat** — hex and ASCII over the existing mmap; the single
   biggest functional gap for inspecting binaries.
4. **`BkSp` jump history + `+`/`-` bookmarks** — cheap once 2 and 3 exist, since both are just
   a stack of `cur_byte` values.
5. **ELF64 parsing (`F8`)** — promoted, because it is not just a header viewer: the
   `p_vaddr`/`p_offset` mapping is what makes disassembly addresses correct at all
   (see §4 above), and it is the prerequisite for items 6, `F6` xrefs and `F12` names.
6. **Branch following (`1`–`9`) and `/` resync** — Zydis already yields branch targets in the
   decoded operands, so this is mostly plumbing in `disassembler.c`/`render.c`.
7. **Block marking (`*`, `[`, `]`, `F2` write block)** — read-only subset of Hiew's block commands;
   no file mutation required.
8. **Block-device support** — accept `S_ISBLK`, size via `BLKGETSIZE64`, sector-aligned reads.
   Small in code, but settle the root-privileges and accidental-write questions first.
9. **Edit mode (`F3`/`F9`) and the assembler** — the largest change: replaces the plain mmap
   with a piece table if insert/delete are wanted, plus save/undo semantics. The Zydis
   encoder needed for the assembler half is already linked in.

Items 1–8 fit LHiew's existing read-only architecture. Item 9 changes it.

### Defects found while comparing

Two bugs surfaced during this analysis, both in `read_file_in_editor()` (`src/file_buffer.c:13`):

- The `S_ISREG`/`fstat` guard **prints but does not return**, so a rejected file keeps going
  and is treated as empty rather than refused.
- That diagnostic uses `printf` to `stdout` while the terminal is in raw mode, which breaks
  the single-`write()` frame invariant documented in `CLAUDE.md`.

## Notes on fidelity

Small divergences from Hiew's conventions worth deciding on deliberately:

- **Offsets are decimal** in the wide status bar (`"%s %zu:%zu"` in `editor_draw_status_bar`);
  Hiew is hex-first everywhere, with `t` marking decimal.
- **AT&T syntax** where Hiew uses Intel. A toggle would be a one-line Zydis formatter change
  (`ZYDIS_FORMATTER_STYLE_ATT` → `..._INTEL` in `disassembler.c`).
- **Branch targets are file offsets, not virtual addresses** — `disassemble_block()` passes
  `read_offset` as Zydis's `runtime_address`. Correct only for flat binaries; wrong by the
  image base for any ELF. Fixed by item 5 above.
- **`DEL_KEY` is decoded but never dispatched** (`terminal.c:61`, unused in `input.c`) — a free
  keybinding slot already wired up.

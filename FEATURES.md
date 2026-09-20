# Missing features: LHiew compared to Hiew

This file lists only what [Hiew](http://www.hiew.ru/) does and LHiew does not.
For what LHiew already does, see the [keybindings](README.md#keybindings),
[hex editing](docs/hex-editing.md), [executable browsing](docs/executable-imports.md)
and [architecture support](docs/architectures.md).

Keys shown are Hiew's, from the [Hiew key reference](https://taviso.github.io/hiewdocs/index.htm),
the [release VIII feature list](http://www.hiew.ru/) and
[Tavis Ormandy's walkthrough](https://lock.cmpxchg8b.com/hiew.html). Items marked
*(6.03)* come from Kris Kaspersky's historical
[Hiew 6.03 article](http://unicornix.spb.ru/docs/prog/heap/hiew.htm).
Audited against `eb956d9` on 2026-09-20.

## Blocks

Nothing in this group exists. LHiew highlights the cursor byte only, and every
block command needs a marked range first.

- **Mark a range** — `*` start and extend, `Ctrl-*` add the whole file, `Alt-*` resize to the cursor, `[` / `]` jump to the ends.
- **Block colors** — `Alt-M` assign, `Shift-Alt-M` random, `Alt-N` / `Shift-Alt-N` step between marked blocks. Colors persist in a `.cmarkers` file reloaded at startup.
- **Block file operations** — `F2` write to a file, `Ctrl-F2` read from a file, `Alt-F2` fill with a pattern, `Shift-F2` delete and truncate, `Shift-F5` copy, `Shift-F6` move, `Shift-F4` print to a file or the clipboard.
- **Offset-based block I/O and transcoding** *(6.03)* — the write-block dialog takes a destination `Offset` and a `Table` (As Is, Windows-1251, Koi-8) that converts the encoding on the way.

## Editing

- **Insert and delete bytes** — `Shift-F3` inserts NUL bytes, `Shift-F2` deletes a block and truncates, `Ins` toggles insert/overwrite. LHiew [overwrites in place](docs/hex-editing.md) and never changes file length.
- **Inline assembler** — in code view, `F3` then `Tab` switches from opcode bytes to assembler input. LHiew has no instruction input; it builds Zydis with `ZYDIS_FEATURE_ENCODER OFF` and patches opcode bytes by hand.
- **NOP an instruction** — `Alt-F2` over the marked instruction. In LHiew you type `90` over each byte.
- **Timestamp control** — `Esc` leaves without updating the file timestamp, `F10` updates it. LHiew does not manage timestamps.

## Navigation

- **Relative, decimal and virtual-address goto** — `F5` accepts `+off` / `-off`, `.address` for a virtual address and a `t` suffix for decimal; `PgDn` shows the entry history. LHiew's `F5` / `g` takes an absolute hexadecimal file offset only.
- **Return to the previous location** — `BkSp`, after a goto or a followed branch.
- **Bookmarks** — `+` push, `-` restore, `Alt--` delete, `Alt-0` clear all, `Alt-1`…`Alt-8` recall by number.
- **Keyboard macros** — `Ctrl-.` records, `Ctrl-0`…`Ctrl-8` play, with a manager for saving, loading and organizing them.
- **View-mode menu** — `F4` picks hex, code or text from a menu. LHiew only cycles with `m` / `Ctrl-M`.

## Search

- **Wildcards** — `Alt-?` types a single-character wildcard into the search pattern.
- **Instruction patterns** — `F7` inside the search dialog assembles an instruction and searches for that. LHiew matches raw bytes or literal ASCII only.
- **Replace, and multi-file scope** — Hiew searches and replaces across several files. LHiew's search is read-only and scoped to the one open file.
- **String encodings** — `Alt-F8` selects the translation table, for example UTF-16; `F6` toggles UTF-16 even/odd offsets. LHiew matches bytes and printable ASCII, case-sensitively.
- **Block scope and direction toggle** — `F4` restricts the scan to the marked block and `Alt-F7` flips the next direction, both from the dialog. LHiew always scans the whole file and takes direction from `n` / `N` / `Shift-F7`.
- **Strings dialog** — `Alt-F6` lists the strings in the file with a minimum length, ASCII/Unicode cycling, offsets and filtering.

## Code view

- **Follow branches** — Hiew labels every branch on screen with `1`–`9` and `A`, and `BkSp` returns. LHiew formats the target in the operand text but cannot jump to it.
- **Code cross-references** — `F6` finds a caller or jump to the current location, `Ctrl-F6` the next one.
- **Forced resynchronization** — `/` restarts disassembly at the cursor. LHiew's backward decoding is heuristic, and `/` is deliberately left unbound for this.
- **Import names on operands** — Hiew resolves imports onto indirect calls and cycles through their references. LHiew reads the tables in the `F8` browser only.
- **Intel syntax** — Hiew prints x86 in Intel syntax. LHiew prints AT&T with no toggle.
- **Virtual-address display** — LHiew's gutter and cursor stay on file offsets. Decoders receive mapped runtime addresses, but nothing shows or accepts a VA/RVA.

## Executables

- **ELF, Mach-O and TE header browsing** — Hiew's `F8` covers those formats too. LHiew detects them for disassembly, but the browser handles [PE, NE, LE, LX and NLM](docs/executable-imports.md#format-coverage) only.
- **Exports and directory tables** — `F8` `F9` browses exports. LHiew has no export table, no decoded delay imports (raw directory rows only), no PE directory/flag editor, and bound imports are read-only.
- **Import table rebuilding** — LHiew edits existing names and ordinals at their current byte width; it never adds or removes entries, and regenerates no checksums or signatures.
- **Netware module variants** — Hiew handles NLM, DSK, LAN and similar. LHiew parses i386 NLM v4 and does not distinguish module types.
- **Hybrid and fat containers** — ARM64EC/ARM64X PE images and universal Mach-O slices are recognized and reported as unsupported; there is no slice chooser. See the [container limits](docs/architectures.md#explicit-limits-and-follow-up-work).
- **Manual rebasing** *(6.03)* — in the file view, `Ctrl-F5` takes an image base, and pressing it twice rebases relative to the cursor (the article's `*0`). Raw firmware and ROM dumps need this; LHiew has no address prompt.
- **VxD/VMM service annotations** *(6.03)* — `hiew.vmm` names the services behind LE `VMMcall` / `VxDcall`, so a call renders as `VxDcall VDD.Get_DISPLAYINFO`. Generic x86 decoding cannot supply those names.

## Names and comments

- **Names window** — `F12` browses symbols and comments, `Shift-F12` names the location under the cursor, and the same keys import and export a plain-text symbol format (for example from `nm`). Not implemented.
- **Comments** — `;` attaches a comment to an instruction or location, shown when the cursor reaches it. Not implemented.

## Data tools

- **64-bit calculator** — `Alt-=` opens a programmer's calculator with C operators, several input bases and `@b` / `@w` / `@d` / `@Q` reads of the data under the cursor. LHiew has none; fields are read by eye from the hex pane.
- **Crypt interpreter** — `Alt-F3`, or `Ctrl-F7` while editing, runs a tiny x86 subset over block data for XOR masks and similar transforms, and can save those programs. Not implemented.

## Files, sessions and environment

- **Multiple files** — `F9` opens another file, `Tab` moves through the file history, `Ctrl-BkSp` lists open files, `Ctrl-F11` / `Ctrl-F12` step through command-line arguments. LHiew opens `argv[1]` and nothing else.
- **File navigator** *(6.03)* — a directory browser with sorting, filters, hidden files, filename completion and new-file creation. `Ctrl-F1` / `F3` / `F5` / `F7` remember four directories and `Ctrl-F2` / `F4` / `F6` / `F8` return to them. LHiew shows an empty viewer when started without a path.
- **Session and startup configuration** *(6.03)* — `Ctrl-F10` saves the session and `/SAV` names the `hiew.sav` reloaded at startup, covering cursor position, bookmarks and file history; `/INI` picks the config file, where `StartMode = Text | Hex | Code` sets the opening view. LHiew saves nothing and always starts in text view.
- **Physical and logical drives** — Hiew views and edits raw devices. LHiew rejects anything that is not a regular file; disk images open normally, with no partition or filesystem browser.
- **HEM plugins** — `F11` loads Hiew External Modules. LHiew has no plugin interface.
- **Information panel** — `Ctrl-Alt` shows the full path, file size, free memory and the last error. LHiew has a one-line status bar.
- **Screenshots and beeps** — `Alt-P` saves a text screenshot to a file or the clipboard; `Alt-B` toggles beeps.

## Display

- **Unicode/UTF-8** — release VIII advertises Unicode and UTF-8 support. LHiew renders one terminal cell per byte: non-ASCII prints as `?`, control bytes as `.` or `@`.
- **Text-mode preferences** *(6.03)* — `[Wrap]` and `[Tab]` handling (on, off or auto), `Ctrl-Left` / `Ctrl-Right` horizontal shift by `[StepCtrlRight]` columns, and translation tables from `hiew.xlt`, with `[XlatTableIndex]` choosing the default. LHiew reflows bytes to the terminal width with no line-oriented interpretation and no horizontal viewport.
- **Color configuration** — Hiew persists block colors, and Hiewix 1.20 adds a general palette and hex-mode byte coloring. LHiew marks the selection with inverse video and offers no theme.

## Where the gaps bite first

Block marking is the single biggest gap: it gates block export, fill, copy,
delete, color marking and block-scoped search, and it is the first failing step
in two of the three [workflow scenarios](https://lock.cmpxchg8b.com/hiew.html).
After that, relative and virtual-address goto with a jump history, then branch
following, remove the most hand-counted offsets.

This is a record of gaps, not a plan to reproduce DOS-era behavior, file
formats or editing constraints.

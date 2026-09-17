# Hexadecimal editing

LHiew can overwrite existing bytes in a regular file using hexadecimal or
printable ASCII input. Edits stay in a private mapping until an explicit save.
The file length does not change. There is no application-defined file-size cap;
the file must fit the platform's file-offset types and virtual-address space.

This is byte editing, not an assembler. Instruction-text input, insertion,
deletion, file resizing, new-file creation, blocks, and general undo are not
implemented. The [feature comparison](../FEATURES.md) records these limits.

## Open, navigate, edit, and save

1. Open an existing file: `./build/lhiew /path/to/file`.
2. Press **F3** from any view. LHiew enters the hex editor after checking that
   the same nonempty file can be opened for writing. Read-only or empty files
   remain viewable, but cannot enter this overwrite editor.
3. Press **F5**, enter an **absolute hexadecimal file offset**, and press
   **Enter**. `g` also opens goto while viewing, but is ordinary text in the
   ASCII editor. Goto does not accept virtual addresses or relative expressions.
   An optional `0x` prefix is accepted; Backspace corrects the prompt and Escape
   cancels it without moving.
4. Type two hexadecimal digits to replace a byte. The first digit changes the
   high nibble; the second changes the low nibble and advances to the next byte.
   Each digit changes the pending byte immediately when its value differs.
5. Press **Tab** to switch to printable ASCII input. Each character overwrites
   one byte and advances. Press Tab again for hex input. ASCII accepts characters
   from space through `~`; use hex input for arbitrary byte values.
6. Press **F9** to save changed bytes and keep editing. The file stays the same
   length; LHiew does not rewrite the complete file.
7. Press **Escape** or **F10** to leave editing and remain in hex view.
   **Ctrl-Q** quits the application. With unsaved edits, these actions offer
   **`s` save**, **`d` discard**, or **Escape continue editing**.

Use arrows and Page Up/Down to edit anywhere in the file, including beyond the
current screen. **Home/End** select the first/last byte of the current row;
**Ctrl-Home/Ctrl-End** select the first/last existing file byte. Moving, switching
input panes, or going to another offset resets hex input to the high nibble.
Typing after the final byte does not extend the file. Goto can select that EOF
position, but Ctrl-End selects the last existing byte. Backspace in the editor
moves one byte left; it does not undo a change.

In view mode, `m` and Ctrl-M cycle text, hex, and assembly views. While editing,
printable keys are input rather than view/navigation shortcuts: in particular,
ASCII `m`, `a`, `e`, `g`, and `h/j/k/l` are file contents. Use function keys and
arrows for editor commands. Escape leaves the editor before view shortcuts apply.

## A reproducible editing walkthrough

Create a disposable file outside LHiew, then open it:

```sh
python3 -c 'from pathlib import Path; Path("/tmp/lhiew-edit-demo.bin").write_bytes(bytes(range(16)))'
./build/lhiew /tmp/lhiew-edit-demo.bin
```

Press **F3**, then **F5**, type **`8`**, and press **Enter**. Type **`4142`**
in the hex pane, then **Tab** and **`CD`** in the ASCII pane. Press **F9**,
**Escape**, and **Ctrl-Q**. Inspect the saved file:

```sh
python3 -c 'from pathlib import Path; print(Path("/tmp/lhiew-edit-demo.bin").read_bytes().hex(" "))'
```

The result should be:

```text
00 01 02 03 04 05 06 07 41 42 43 44 0c 0d 0e 0f
```

To exercise discard, reopen the file, press F3, change a byte, press Escape,
then `d`. The pending edit is discarded and hex viewing resumes. To cancel
leaving instead, press Escape again at the unsaved-edits prompt.

The same navigation is available at large offsets. For example, F5 followed by
`100000000` selects offset 4 GiB when that offset exists in the opened file.
Creating a large sparse file is an external setup operation, not an LHiew command.

## Storage, conflicts, and limits

File data is demand-paged through a private mapping. Pending change records are
stored for modified bytes; memory use does not require a second complete file
copy. Dirty pages and change records still consume memory, so editing large
ranges has a cost even when opening the file is inexpensive. A whole-file mapping
must fit available address space; no sliding mapping window is implemented.

Saving verifies the opened file's identity, length, and recorded modification
metadata before writing changed byte ranges with positioned writes and flushing
them. An externally replaced, resized, or modified file causes save to fail,
leaving pending edits available. File permissions are checked when entering the
editor; merely viewing a file does not require write access.

Save is **not atomic**. An I/O or flush failure may leave some or all intended
bytes on disk while pending edits remain. Discard reloads the opened file's disk
contents; it does not undo bytes already written by an unsuccessful save. If
another process replaced the pathname, reopen the path to view that replacement.
There is no transaction log, automatic backup, or general undo history. These
checks also do not provide exclusive access against concurrent writers.

## Relationship to the Hiew 6.03 article

[Kris Kaspersky's article](http://unicornix.spb.ru/docs/prog/heap/hiew.htm) is a
third-party historical discussion of **Hiew 6.03**, not a current specification.
It describes F3 entering an editor, F9 writing changes, hexadecimal offset
navigation, and editing within a larger inspection workflow. LHiew implements
the existing-file overwrite path below; it does not claim every action in the
article is now available.

| Workflow action | LHiew support |
|---|---|
| Open an existing file and inspect bytes | Supported through the command line and text/hex/assembly views |
| Enter the editor | F3 opens hex editing from any view |
| Reach a file location | F5 absolute hex file offset; arrows, pages, row/file boundaries |
| Overwrite bytes or printable text | Hex nibbles or Tab-selected ASCII input; file length unchanged |
| Save pending changes | F9 saves and continues editing |
| Leave or quit with pending edits | Explicit save/discard/continue prompt; this is LHiew behavior, not a copied historical shortcut contract |
| Edit beyond one visible screen | Supported; navigation is not limited to a screen-sized edit buffer |
| Create a file using the navigator | Not implemented; the walkthrough creates its file externally |
| Use relative/virtual goto or manual rebasing | Not implemented; detected executable mappings do not supply a general address prompt |
| Insert/delete bytes, resize files, or operate on blocks | Not implemented |
| Assemble instructions, search instruction patterns, or apply Crypt/XOR transforms | Not implemented |
| Undo arbitrary edits or repair structured headers | Not implemented; whole pending-change discard is not general undo |

The article also discusses import names, executable browsers, file history,
configuration, and calculator functions. They are separate features, not
prerequisites for this byte-overwrite workflow. Its context-dependent and
occasionally conflicting historical bindings are documented separately in
[FEATURES.md](../FEATURES.md#historical-additions-hiew-603).

## Verification

[`tests/test_hex_edit.c`](../tests/test_hex_edit.c) checks storage persistence,
discard, failures/conflicts,
boundary handling, and overwrites in a sparse file beyond 4 GiB. Terminal tests
in [`tests/test_hex_editor.py`](../tests/test_hex_editor.py) exercise the visible
open → goto → edit → save → leave/reopen workflow, both
input panes, unsaved-change prompts, navigation, read-only failures, and resize.
Run the repository's full suite with:

```sh
ctest --test-dir build --output-on-failure
```

These checks demonstrate the implemented overwrite path; they do not establish
support for every possible file size or complete Hiew editing compatibility.

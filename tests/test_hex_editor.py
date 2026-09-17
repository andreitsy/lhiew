"""Exercise the documented hex editing workflow through a real terminal.

Usage: python3 tests/test_hex_editor.py build/lhiew [--snapshot-dir DIR]
Fixtures include a sparse file larger than 4 GiB; no large allocation is needed.
"""

import argparse
from pathlib import Path
import re
import tempfile
import unittest

from test_terminal_resize import Viewer


F3 = b"\x1bOR"
F5 = b"\x1b[15~"
F9 = b"\x1b[20~"
F10 = b"\x1b[21~"
LEFT = b"\x1b[D"
RIGHT = b"\x1b[C"
HOME = b"\x1b[H"
END = b"\x1b[F"
FILE_HOME = b"\x1b[1;5H"
FILE_END = b"\x1b[1;5F"


class HexEditorTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix="lhiew-hex-pty-")
        self.addCleanup(self.directory.cleanup)
        self.fixture = Path(self.directory.name) / "editable.bin"
        self.original = bytes(range(256)) * 3
        self.fixture.write_bytes(self.original)

    def viewer(self, filename=None, columns=80, rows=24):
        return Viewer(self.binary, filename or self.fixture, columns, rows)

    def snapshot(self, label, frame):
        if self.snapshot_dir:
            self.snapshot_dir.mkdir(parents=True, exist_ok=True)
            (self.snapshot_dir / f"{label}.txt").write_text(frame.dump() + "\n")

    def expect_edit(self, viewer, offset, pane="hex", resized=False):
        def matches(frame):
            status = frame.lines[-2].lower()
            position = re.search(rf"\b{offset}:[0-9]+\b", status)
            position = position or re.search(rf"@{offset:x}\b", status)
            return re.search(rf"\bedit {pane}\b", status) and position
        return viewer.expect(matches, f"{pane} editing at {offset}",
                             allow_old_geometry=resized)

    def expect_prompt(self, viewer):
        return viewer.expect(
            lambda frame: "save" in frame.lines[-1].lower()
            and "discard" in frame.lines[-1].lower(), "unsaved changes prompt")

    def goto(self, viewer, offset, mode="edit", pane="hex", key=F5):
        viewer.press(key)
        viewer.expect(lambda frame: "Go to file offset" in frame.lines[0],
                      "absolute hexadecimal offset prompt")
        viewer.press(f"{offset:x}".encode("ascii") + b"\r")
        if mode == "edit":
            return self.expect_edit(viewer, offset, pane)
        return viewer.expect_mode(mode, offset, self.fixture.stat().st_size)

    def save(self, viewer):
        viewer.press(F9)
        return viewer.expect(lambda frame: frame.lines[-1].lower().startswith("saved ")
                             and re.search(r"\bEDIT (HEX|ASCII)\b", frame.lines[-2]),
                             "saved edits")

    def test_exact_documented_sixteen_byte_example(self):
        self.fixture.write_bytes(bytes(range(16)))
        with self.viewer() as viewer:
            viewer.expect_mode("text", 0, 16)
            viewer.press(F3)
            self.expect_edit(viewer, 0)
            self.goto(viewer, 8)
            viewer.press(b"4142\tCD")
            self.expect_edit(viewer, 12, "ascii")
            self.save(viewer)
            viewer.press(b"\x1b")
            viewer.expect_mode("hex", 12, 16)
            viewer.quit()
        self.assertEqual(self.fixture.read_bytes(), bytes(range(8)) + b"ABCD\x0c\x0d\x0e\x0f")

    def test_documented_hex_ascii_save_and_reopen_workflow(self):
        with self.viewer() as viewer:
            viewer.expect_mode("text", 0, len(self.original))
            viewer.press(b"m")
            viewer.expect_mode("hex", 0, len(self.original))
            viewer.press(F3)
            self.expect_edit(viewer, 0)
            self.goto(viewer, 0x20)
            viewer.press(b"deadbeef")
            self.expect_edit(viewer, 0x24)
            viewer.press(b"\tLHiew!")
            frame = self.expect_edit(viewer, 0x2a, "ascii")
            self.assertIn("LHiew!", "\n".join(frame.lines))
            self.snapshot("hex-editor-pending-80x24", frame)
            # Edits are staged until the user invokes Save.
            self.assertEqual(self.fixture.read_bytes(), self.original)
            self.save(viewer)
            expected = self.original[:0x20] + bytes.fromhex("deadbeef") + b"LHiew!"
            expected += self.original[0x2a:]
            self.assertEqual(self.fixture.read_bytes(), expected)
            viewer.press(b"\x1b")
            viewer.expect_mode("hex", 0x2a, len(self.original))
            viewer.quit()

        with self.viewer() as reopened:
            reopened.expect_mode("text", 0, len(self.original))
            self.goto(reopened, 0x20, mode="text", key=b"g")
            reopened.press(b"m")
            frame = reopened.expect_mode("hex", 0x20, len(self.original))
            self.assertIn("de ad be ef", "\n".join(frame.lines))
            self.assertIn("LHiew!", "\n".join(frame.lines))
            reopened.quit()

    def test_edit_key_sequences_are_reachable_from_all_views(self):
        for mode, keys, function_key in (("text", b"", F3),
                                         ("hex", b"m", b"\x1b[13~"),
                                         ("asm", b"mm", F3)):
            with self.subTest(mode=mode, function_key=function_key):
                with self.viewer() as viewer:
                    viewer.expect_mode("text", 0, len(self.original))
                    if keys:
                        viewer.press(keys)
                        viewer.expect_mode(mode, 0, len(self.original))
                    viewer.press(function_key)
                    self.expect_edit(viewer, 0)
                    viewer.press(F10)
                    viewer.expect_mode("hex", 0, len(self.original))
                    viewer.quit()

    def test_half_byte_navigation_and_ascii_shortcuts_are_literal(self):
        with self.viewer() as viewer:
            viewer.expect_mode("text", 0, len(self.original))
            viewer.press(F3 + b"f")
            self.expect_edit(viewer, 0)
            # A move starts a new byte at its high nibble; it must not carry
            # the previous byte's half-completed nibble into the next byte.
            viewer.press(RIGHT + b"ab\t")
            self.expect_edit(viewer, 2, "ascii")
            literals = b"maeoglhjk !~"
            viewer.press(literals)
            self.expect_edit(viewer, 2 + len(literals), "ascii")
            self.save(viewer)
            expected = b"\xf0\xab" + literals + self.original[2 + len(literals):]
            self.assertEqual(self.fixture.read_bytes(), expected)
            viewer.quit()

    def test_cancel_then_discard_unsaved_changes(self):
        with self.viewer() as viewer:
            viewer.expect_mode("text", 0, len(self.original))
            viewer.press(F3 + b"aa\x1b")
            self.expect_prompt(viewer)
            viewer.press(b"\x1b")
            self.expect_edit(viewer, 1)
            viewer.press(b"bb" + F10)
            self.expect_prompt(viewer)
            viewer.press(b"d")
            frame = viewer.expect_mode("hex", 2, len(self.original))
            self.assertIn("00 01 02 03", frame.lines[0])
            self.assertEqual(self.fixture.read_bytes(), self.original)
            viewer.quit()

    def test_save_and_leave_from_unsaved_confirmation(self):
        with self.viewer() as viewer:
            viewer.expect_mode("text", 0, len(self.original))
            viewer.press(F3 + b"aa" + F10)
            self.expect_prompt(viewer)
            viewer.press(b"s")
            viewer.expect_mode("hex", 1, len(self.original))
            self.assertEqual(self.fixture.read_bytes(), b"\xaa" + self.original[1:])
            viewer.quit()

    def test_quit_confirmation_cancel_save_and_discard(self):
        for action in (b"s", b"d"):
            with self.subTest(action=action):
                self.fixture.write_bytes(self.original)
                with self.viewer() as viewer:
                    viewer.expect_mode("text", 0, len(self.original))
                    viewer.press(F3 + b"fe\x11")
                    self.expect_prompt(viewer)
                    viewer.press(b"\x1b")
                    self.expect_edit(viewer, 1)
                    self.assertIsNone(viewer.process.poll())
                    viewer.press(b"\x11")
                    self.expect_prompt(viewer)
                    viewer.press(action)
                    viewer.wait_for_exit()
                    self.assertEqual(viewer.process.returncode, 0)
                expected = b"\xfe" + self.original[1:] if action == b"s" else self.original
                self.assertEqual(self.fixture.read_bytes(), expected)

    def test_page_row_and_file_navigation_with_pending_changes(self):
        with self.viewer(columns=40, rows=10) as viewer:
            viewer.expect_mode("text", 0, len(self.original))
            viewer.press(F3 + b"ff")
            self.expect_edit(viewer, 1)
            # At 40 columns hex mode displays four bytes on each of eight rows.
            for key, offset in ((b"\x1b[6~", 33), (b"\x1b[5~", 1),
                                (END, 3), (HOME, 0),
                                (FILE_END, len(self.original) - 1),
                                (FILE_HOME, 0)):
                viewer.press(key)
                self.expect_edit(viewer, offset)
            self.save(viewer)
            self.assertEqual(self.fixture.read_bytes(), b"\xff" + self.original[1:])
            viewer.quit()

    def test_go_to_cancel_and_invalid_range_preserve_position(self):
        with self.viewer() as viewer:
            viewer.expect_mode("text", 0, len(self.original))
            viewer.press(F3)
            self.expect_edit(viewer, 0)
            self.goto(viewer, 0x31)
            viewer.press(F5)
            viewer.expect(lambda frame: "offset" in frame.lines[0].lower(), "offset prompt")
            viewer.press(b"12\x1b")
            self.expect_edit(viewer, 0x31)
            viewer.press(F5)
            viewer.expect(lambda frame: "offset" in frame.lines[0].lower(), "offset prompt")
            viewer.press(b"ffffffffffffffffffffffffffffffff\r")
            viewer.expect(lambda frame: re.search(r"invalid|range|outside|large|overflow",
                                                  frame.lines[-1], re.I),
                          "invalid offset feedback")
            viewer.press(b"\x1b")
            self.expect_edit(viewer, 0x31)
            viewer.quit()

    def test_resize_preserves_pending_bytes_and_edit_pane(self):
        with self.viewer() as viewer:
            viewer.expect_mode("text", 0, len(self.original))
            viewer.press(F3 + b"aa\tZ")
            self.expect_edit(viewer, 2, "ascii")
            for columns, rows in ((40, 10), (120, 40), (24, 5), (80, 24)):
                viewer.resize(columns, rows)
                frame = self.expect_edit(viewer, 2, "ascii", resized=True)
                self.snapshot(f"hex-editor-{columns}x{rows}", frame)
            self.save(viewer)
            self.assertEqual(self.fixture.read_bytes(), b"\xaaZ" + self.original[2:])
            viewer.quit()

    def test_sparse_file_beyond_four_gib_can_be_edited_and_saved(self):
        filename = Path(self.directory.name) / "large-sparse.bin"
        length = (1 << 32) + 4097
        with filename.open("wb") as stream:
            stream.write(b"HEAD")
            stream.truncate(length)
            stream.seek(length - 4)
            stream.write(b"TAIL")
        with self.viewer(filename, columns=120) as viewer:
            viewer.expect_mode("text", 0, length)
            viewer.press(F3)
            self.expect_edit(viewer, 0)
            target = (1 << 32) + 0x100
            frame = self.goto(viewer, target)
            self.assertIn("100000", "\n".join(frame.lines[:-2]))
            viewer.press(b"deadbeef")
            self.expect_edit(viewer, target + 4)
            viewer.press(FILE_END)
            self.expect_edit(viewer, length - 1)
            viewer.press(b"21")
            # At EOF, replacements stay within the existing file.
            self.expect_edit(viewer, length)
            viewer.press(b"ab")
            viewer.expect(lambda frame: "end of file" in frame.lines[-1].lower(),
                          "replacement past EOF rejected")
            self.save(viewer)
            self.assertEqual(filename.stat().st_size, length)
            with filename.open("rb") as stream:
                self.assertEqual(stream.read(8), b"HEAD\0\0\0\0")
                stream.seek(1 << 31)
                self.assertEqual(stream.read(16), bytes(16))
                stream.seek(target - 2)
                self.assertEqual(stream.read(8), b"\0\0\xde\xad\xbe\xef\0\0")
                stream.seek(length - 4)
                self.assertEqual(stream.read(), b"TAI!")
            # Saving a few changed bytes must not turn holes into gigabytes of data.
            self.assertLess(filename.stat().st_blocks * 512, 2 * 1024 * 1024)
            viewer.quit()
        with self.viewer(filename, columns=120) as viewer:
            viewer.expect_mode("text", 0, length)
            viewer.press(F3)
            self.expect_edit(viewer, 0)
            frame = self.goto(viewer, target)
            self.assertIn("de ad be ef", "\n".join(frame.lines))
            viewer.quit()

    def test_empty_file_does_not_offer_nonexistent_bytes_for_editing(self):
        self.fixture.write_bytes(b"")
        with self.viewer() as viewer:
            viewer.expect_mode("text", 0, 0)
            viewer.press(F3)
            viewer.expect(lambda frame: re.search(r"empty|no bytes|no file", frame.lines[-1], re.I),
                          "empty file editing explanation")
            self.assertNotRegex(viewer.latest.lines[-2], r"\bEDIT (HEX|ASCII)\b")
            self.assertEqual(self.fixture.stat().st_size, 0)
            viewer.quit()

    def test_external_truncate_keeps_rendering_save_and_discard_safe(self):
        contents = bytes(range(256)) * 64
        self.fixture.write_bytes(contents)
        with self.viewer() as viewer:
            viewer.expect_mode("text", 0, len(contents))
            viewer.press(F3 + b"aa")
            self.expect_edit(viewer, 1)
            # Render a page away from the privately modified byte so that
            # truncation invalidates the page the viewport previously used.
            self.goto(viewer, 0x2000)
            with self.fixture.open("r+b") as stream:
                stream.truncate(0)
            viewer.press(RIGHT)
            viewer.expect(lambda frame: frame.lines[0].startswith("File changed"),
                          "safe rendering after external truncation")
            self.assertIsNone(viewer.process.poll())
            viewer.press(F9)
            frame = viewer.expect(
                lambda frame: "changed externally" in frame.lines[-1].lower(),
                "saving externally truncated file rejected")
            self.assertIn("EDIT HEX*", frame.lines[-2])
            self.assertEqual(self.fixture.stat().st_size, 0)
            viewer.press(b"\x1b")
            self.expect_prompt(viewer)
            viewer.press(b"d")
            viewer.expect_mode("hex", 0, 0)
            self.assertEqual(self.fixture.read_bytes(), b"")
            viewer.quit()

    def test_dirty_quit_confirmation_works_below_minimum_terminal_size(self):
        for action in (b"s", b"d"):
            with self.subTest(action=action):
                self.fixture.write_bytes(self.original)
                with self.viewer() as viewer:
                    viewer.expect_mode("text", 0, len(self.original))
                    viewer.press(F3 + b"aa")
                    self.expect_edit(viewer, 1)
                    viewer.resize(20, 4)
                    viewer.expect_warning(allow_old_geometry=True)
                    viewer.press(b"\x11")
                    viewer.expect(lambda frame: "s save d discard" in frame.lines[0],
                                  "visible quit choices in a small terminal")
                    viewer.press(action)
                    viewer.wait_for_exit()
                    self.assertEqual(viewer.process.returncode, 0)
                expected = b"\xaa" + self.original[1:] if action == b"s" else self.original
                self.assertEqual(self.fixture.read_bytes(), expected)

    def test_read_only_file_cannot_enter_edit_mode(self):
        self.fixture.chmod(0o444)
        self.addCleanup(self.fixture.chmod, 0o644)
        with self.viewer() as viewer:
            viewer.expect_mode("text", 0, len(self.original))
            viewer.press(F3)
            viewer.expect(lambda frame: re.search(r"fail|read.only|permission|denied|cannot",
                                                  frame.lines[-1], re.I),
                          "write permission failure")
            self.assertEqual(self.fixture.read_bytes(), self.original)
            self.assertNotRegex(viewer.latest.lines[-2], r"\bEDIT (HEX|ASCII)\b")
            viewer.quit()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("--snapshot-dir", type=Path)
    options = parser.parse_args()
    HexEditorTests.binary = options.binary.resolve()
    HexEditorTests.snapshot_dir = options.snapshot_dir
    unittest.main(argv=[__file__], verbosity=2)

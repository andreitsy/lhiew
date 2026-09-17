"""Exercise executable import browsing and editing through a real terminal.

Usage: python3 tests/test_executable_browser.py build/lhiew [--snapshot-dir DIR]
All fixtures are created locally; no proprietary executables are downloaded.
"""

import argparse
from pathlib import Path
import re
import tempfile
import unittest

from import_fixtures import all_fixtures, pe_fixture, u16, u32
from test_terminal_resize import Viewer


F3 = b"\x1bOR"
F8 = b"\x1b[19~"
F9 = b"\x1b[20~"
UP = b"\x1b[A"
DOWN = b"\x1b[B"
HOME = b"\x1b[H"
END = b"\x1b[F"
PAGE_UP = b"\x1b[5~"
PAGE_DOWN = b"\x1b[6~"
ESC = b"\x1b"
CLEAR = b"\x15"


class ExecutableBrowserTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix="lhiew-import-pty-")
        self.addCleanup(self.directory.cleanup)

    def fixture_file(self, fixture, suffix=""):
        path = Path(self.directory.name) / (fixture.format.replace("+", "plus") + suffix + ".bin")
        path.write_bytes(fixture.data)
        return path

    def viewer(self, path, columns=100, rows=24):
        return Viewer(self.binary, path, columns, rows)

    def browser(self, viewer, format, selected=0, imports=True, incomplete=False, resized=False):
        title = f"{format} {'Imports' if imports else 'Headers/regions'} {selected + 1}/"
        return viewer.expect(
            lambda frame: frame.lines[0].startswith(title)
            and (not incomplete or "incomplete" in frame.lines[0]),
            f"{format} browser row {selected + 1}", allow_old_geometry=resized)

    def select(self, viewer, fixture, choice):
        viewer.press(HOME + DOWN * choice)
        return self.browser(viewer, fixture.format, choice)

    def open_browser(self, viewer, fixture, key=F8):
        viewer.expect_mode("text", 0, len(fixture.data))
        viewer.press(key)
        return self.browser(viewer, fixture.format)

    def edit_prompt(self, viewer, old, ordinal=False):
        viewer.press(F3)
        return viewer.expect(
            lambda frame: frame.lines[0].startswith("Edit ordinal" if ordinal else "Edit name")
            and str(old) in frame.lines[1], "prefilled import editing prompt")

    def stage(self, viewer, value):
        viewer.press(CLEAR + value + b"\r")
        return viewer.expect(lambda frame: "Import edit staged" in frame.lines[-1],
                             "staged import edit")

    def save(self, viewer):
        viewer.press(F9)
        return viewer.expect(lambda frame: frame.lines[-1].startswith("Saved ")
                             and "Imports" in frame.lines[0], "saved import edits")

    def pending(self, viewer):
        return viewer.expect(lambda frame: "save" in frame.lines[-1].lower()
                             and "discard" in frame.lines[-1].lower(),
                             "save/discard prompt")

    def snapshot(self, name, frame):
        if self.snapshot_dir:
            self.snapshot_dir.mkdir(parents=True, exist_ok=True)
            (self.snapshot_dir / f"{name}.txt").write_text(frame.dump() + "\n")

    def test_every_format_browse_headers_and_jump_to_import_bytes(self):
        for fixture in all_fixtures():
            with self.subTest(format=fixture.format):
                path = self.fixture_file(fixture)
                with self.viewer(path) as viewer:
                    frame = self.open_browser(viewer, fixture, b"b")
                    text = "\n".join(frame.lines)
                    self.assertIn(fixture.module.decode(), text)
                    self.assertIn(fixture.symbol.decode(), text)
                    self.snapshot(f"imports-{fixture.format}", frame)
                    viewer.press(b"\t")
                    frame = self.browser(viewer, fixture.format, imports=False)
                    self.assertRegex("\n".join(frame.lines), r"Section|Segment|Object|Code")
                    viewer.press(DOWN)
                    self.browser(viewer, fixture.format, 1, imports=False)
                    viewer.press(UP + b"\r")
                    viewer.expect_mode("hex", fixture.header_offset, len(fixture.data))
                    viewer.press(F8)
                    self.browser(viewer, fixture.format)
                    self.select(viewer, fixture, fixture.symbol_choice)
                    viewer.press(b"\r")
                    viewer.expect_mode("hex", fixture.symbol_record, len(fixture.data))
                    self.assertFalse(Path(str(path) + ".backup").exists())
                    viewer.quit()

    def test_every_format_rename_save_reopen_and_backup(self):
        for fixture in all_fixtures():
            with self.subTest(format=fixture.format):
                path = self.fixture_file(fixture)
                backup = Path(str(path) + ".backup")
                with self.viewer(path) as viewer:
                    self.open_browser(viewer, fixture)
                    self.edit_prompt(viewer, fixture.module.decode())
                    self.assertEqual(backup.read_bytes(), fixture.data)
                    self.stage(viewer, fixture.new_module)
                    self.select(viewer, fixture, fixture.symbol_choice)
                    self.edit_prompt(viewer, fixture.symbol.decode())
                    viewer.press(CLEAR + b"x\r")
                    viewer.expect(lambda frame: "exactly" in frame.lines[-1],
                                  "different-length import name rejected")
                    self.assertEqual(path.read_bytes(), fixture.data)
                    viewer.press(ESC)
                    self.browser(viewer, fixture.format, fixture.symbol_choice)
                    self.edit_prompt(viewer, fixture.symbol.decode())
                    frame = self.stage(viewer, fixture.new_symbol)
                    self.snapshot(f"edited-imports-{fixture.format}", frame)
                    ordinal = None
                    if fixture.ordinal_choice is not None:
                        self.select(viewer, fixture, fixture.ordinal_choice)
                        self.edit_prompt(viewer, 7 if fixture.format in ("LE", "LX") else 123,
                                         ordinal=True)
                        viewer.press(CLEAR + str(fixture.ordinal_max + 1).encode() + b"\r")
                        viewer.expect(lambda frame: "Ordinal must be decimal" in frame.lines[-1],
                                      "out-of-range ordinal rejected")
                        ordinal = 42
                        self.stage(viewer, b"42")
                    self.assertEqual(path.read_bytes(), fixture.data)
                    self.save(viewer)
                    expected = fixture.expected(module=True, symbol=True, ordinal=ordinal)
                    self.assertEqual(path.read_bytes(), expected)
                    self.assertEqual(backup.read_bytes(), fixture.data)
                    viewer.press(ESC)
                    viewer.expect(lambda frame: "EDIT HEX" in frame.lines[-2]
                                  and "Imports" not in frame.lines[0], "return to hex editing")
                    viewer.press(ESC)
                    viewer.expect_mode("hex", 0, len(fixture.data))
                    viewer.quit()
                with self.viewer(path) as viewer:
                    viewer.expect_mode("text", 0, len(fixture.data))
                    viewer.press(F8)
                    frame = self.browser(viewer, fixture.format)
                    self.assertIn(fixture.new_module.decode(), "\n".join(frame.lines))
                    self.assertIn(fixture.new_symbol.decode(), "\n".join(frame.lines))
                    if ordinal is not None:
                        self.assertIn("#42", "\n".join(frame.lines))
                    # A later session preserves the first complete backup.
                    self.edit_prompt(viewer, fixture.new_module.decode())
                    self.stage(viewer, fixture.module)
                    self.save(viewer)
                    self.assertEqual(backup.read_bytes(), fixture.data)
                    self.assertEqual(path.read_bytes(), fixture.expected(symbol=True, ordinal=ordinal))
                    viewer.quit()

    def test_every_format_cancel_and_discard_pending_import_edit(self):
        for fixture in all_fixtures():
            with self.subTest(format=fixture.format):
                path = self.fixture_file(fixture)
                with self.viewer(path) as viewer:
                    self.open_browser(viewer, fixture)
                    self.edit_prompt(viewer, fixture.module.decode())
                    viewer.press(CLEAR + b"wrong" + ESC)
                    frame = self.browser(viewer, fixture.format)
                    self.assertIn(fixture.module.decode(), "\n".join(frame.lines))
                    self.edit_prompt(viewer, fixture.module.decode())
                    self.stage(viewer, fixture.new_module)
                    viewer.press(ESC)
                    viewer.expect(lambda frame: "EDIT HEX*" in frame.lines[-2],
                                  "dirty bytes remain after leaving browser")
                    viewer.press(ESC)
                    self.pending(viewer)
                    viewer.press(b"d")
                    viewer.expect_mode("hex", 0, len(fixture.data))
                    viewer.press(F8)
                    frame = self.browser(viewer, fixture.format)
                    self.assertIn(fixture.module.decode(), "\n".join(frame.lines))
                    self.assertNotIn(fixture.new_module.decode(), "\n".join(frame.lines))
                    self.assertEqual(path.read_bytes(), fixture.data)
                    viewer.quit()

    def test_every_format_readonly_files_remain_browsable(self):
        for fixture in all_fixtures():
            with self.subTest(format=fixture.format):
                path = self.fixture_file(fixture)
                path.chmod(0o444)
                self.addCleanup(path.chmod, 0o644)
                with self.viewer(path) as viewer:
                    self.open_browser(viewer, fixture)
                    viewer.press(F3)
                    frame = viewer.expect(lambda frame: re.search(
                        r"fail|read.only|permission|denied|cannot", frame.lines[-1], re.I),
                        "readonly import editing rejected")
                    self.assertIn("Imports", frame.lines[0])
                    self.assertNotIn("EDIT", frame.lines[-2])
                    self.assertFalse(Path(str(path) + ".backup").exists())
                    self.assertEqual(path.read_bytes(), fixture.data)
                    viewer.quit()

    def test_every_format_malformed_tables_cannot_edit_valid_prefix(self):
        for fixture in all_fixtures():
            with self.subTest(format=fixture.format):
                bad = bytearray(fixture.data)
                if fixture.format.startswith("PE"):
                    bad[0x280:0x280 + fixture.ordinal_width] = (0x1ffffff0).to_bytes(
                        fixture.ordinal_width, "little")
                elif fixture.format == "NE":
                    u16(bad, 0x216, 3)
                elif fixture.format in ("LE", "LX"):
                    bad[fixture.symbol_record + 4] = 3
                else:
                    u32(bad, 347, 0x40000040)
                path = self.fixture_file(fixture)
                path.write_bytes(bad)
                with self.viewer(path) as viewer:
                    viewer.expect_mode("text", 0, len(bad))
                    viewer.press(F8)
                    frame = self.browser(viewer, fixture.format, incomplete=True)
                    self.assertIn(fixture.module.decode(), "\n".join(frame.lines))
                    viewer.press(F3)
                    viewer.expect(lambda frame: "not available" in frame.lines[-1],
                                  "incomplete executable is browse-only")
                    self.assertEqual(path.read_bytes(), bad)
                    self.assertFalse(Path(str(path) + ".backup").exists())
                    viewer.quit()

    def test_every_format_minimum_size_navigation_and_live_resize(self):
        for fixture in all_fixtures():
            with self.subTest(format=fixture.format):
                path = self.fixture_file(fixture)
                with self.viewer(path) as viewer:
                    self.open_browser(viewer, fixture)
                    viewer.resize(24, 5)
                    self.browser(viewer, fixture.format, resized=True)
                    viewer.press(PAGE_DOWN)
                    self.browser(viewer, fixture.format, 2)
                    viewer.press(PAGE_UP)
                    self.browser(viewer, fixture.format)
                    last = 2 if fixture.format.startswith("PE") or fixture.format == "NLM" else 3
                    viewer.press(END)
                    self.browser(viewer, fixture.format, last)
                    viewer.press(HOME)
                    self.browser(viewer, fixture.format)
                    self.edit_prompt(viewer, fixture.module.decode())
                    viewer.press(CLEAR + fixture.new_module)
                    for columns, rows in ((40, 10), (120, 40), (24, 5), (80, 24)):
                        viewer.resize(columns, rows)
                        viewer.expect(lambda frame: frame.lines[0].startswith("Edit name")
                                      and fixture.new_module.decode() in frame.lines[1],
                                      "import prompt survives resizing", allow_old_geometry=True)
                    viewer.press(b"\r")
                    viewer.expect(lambda frame: "Import edit staged" in frame.lines[-1], "staged edit")
                    viewer.press(b"\x11")
                    self.pending(viewer)
                    viewer.press(ESC)
                    self.browser(viewer, fixture.format)
                    viewer.press(b"\x11")
                    self.pending(viewer)
                    viewer.press(b"d")
                    viewer.wait_for_exit()
                    self.assertEqual(path.read_bytes(), fixture.data)

    def test_browser_is_reachable_from_views_and_ascii_b_stays_literal(self):
        fixture = pe_fixture()
        for mode, keys in (("text", b""), ("hex", b"m"), ("asm", b"mm")):
            with self.subTest(mode=mode):
                path = self.fixture_file(fixture, mode)
                with self.viewer(path) as viewer:
                    viewer.expect_mode("text", 0, len(fixture.data))
                    if keys:
                        viewer.press(keys)
                        viewer.expect_mode(mode, 0, len(fixture.data))
                    viewer.press(F8)
                    self.browser(viewer, fixture.format)
                    viewer.press(ESC)
                    viewer.expect_mode(mode, 0, len(fixture.data))
                    viewer.quit()
        path = self.fixture_file(fixture, "ascii")
        with self.viewer(path) as viewer:
            viewer.expect_mode("text", 0, len(fixture.data))
            viewer.press(F3 + b"\t")
            viewer.expect(lambda frame: "EDIT ASCII" in frame.lines[-2], "ASCII editing")
            viewer.press(b"\x1b[15~")
            viewer.expect(lambda frame: "Go to file offset" in frame.lines[0], "goto prompt")
            viewer.press(b"20\rb")  # A literal shortcut byte in unused DOS-header space.
            viewer.expect(lambda frame: "EDIT ASCII*" in frame.lines[-2], "literal ASCII b")
            viewer.press(F8)
            self.browser(viewer, fixture.format)
            viewer.press(b"\x11")
            self.pending(viewer)
            viewer.press(b"d")
            viewer.wait_for_exit()
            self.assertEqual(path.read_bytes(), fixture.data)

    def test_external_truncation_keeps_browser_until_quit(self):
        fixture = pe_fixture()
        for key, name in ((b"\r", "enter"), (ESC, "escape"), (F8, "f8")):
            with self.subTest(key=name):
                path = self.fixture_file(fixture, name)
                with self.viewer(path) as viewer:
                    self.open_browser(viewer, fixture)
                    path.write_bytes(b"")
                    viewer.press(key)
                    frame = viewer.expect(
                        lambda frame: "File size changed" in frame.lines[-1],
                        "truncated file cannot return to mapped-byte rendering")
                    self.assertIn("Imports", frame.lines[0])
                    self.assertIsNone(viewer.process.poll())
                    viewer.quit()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("--snapshot-dir", type=Path)
    options = parser.parse_args()
    ExecutableBrowserTests.binary = options.binary.resolve()
    ExecutableBrowserTests.snapshot_dir = options.snapshot_dir
    unittest.main(argv=[__file__], verbosity=2)

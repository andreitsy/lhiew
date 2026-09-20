"""Exercise Ctrl-C exit and Ctrl-Z job control through a real terminal.

Usage: python3 tests/test_job_control.py build/lhiew
The suspend tests give the viewer its own process group, as a shell does. An
orphaned process group discards stop signals, which would leave nothing to
observe.
"""

import argparse
import os
from pathlib import Path
import signal
import tempfile
import time
import unittest

from test_terminal_resize import Viewer


F3 = b"\x1bOR"
CTRL_C = b"\x03"
CTRL_Z = b"\x1a"
# Attributes reset, screen cleared, cursor shown: a cooked terminal for the shell.
SUSPEND_RESTORE = b"\x1b[m\x1b[2J\x1b[H\x1b[?25h"


class JobControlTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix="lhiew-job-pty-")
        self.addCleanup(self.directory.cleanup)
        self.fixture = Path(self.directory.name) / "editable.bin"
        self.original = bytes(range(256))
        self.fixture.write_bytes(self.original)

    def viewer(self, columns=80, rows=24, own_process_group=False):
        return Viewer(self.binary, self.fixture, columns, rows,
                      own_process_group=own_process_group)

    def stopped(self, viewer):
        """Report a stopped viewer without consuming its pending wait status."""
        try:
            return os.waitid(os.P_PID, viewer.process.pid,
                             os.WSTOPPED | os.WNOHANG | os.WNOWAIT) is not None
        except ChildProcessError:
            return False

    def expect_stopped(self, viewer):
        deadline = time.monotonic() + 2
        while time.monotonic() < deadline:
            viewer.read_frames(0.02)
            if self.stopped(viewer):
                return
        raise AssertionError("viewer did not stop on Ctrl-Z")

    def expect_quiet(self, viewer, seconds=0.4):
        """A stopped viewer reads nothing, so no further frame can arrive."""
        self.assertEqual(viewer.read_frames(seconds), [])

    def resume(self, viewer):
        os.kill(viewer.process.pid, signal.SIGCONT)

    def test_ctrl_c_exits(self):
        with self.viewer() as viewer:
            viewer.expect_mode("text", 0, len(self.original))
            viewer.press(CTRL_C)
            viewer.wait_for_exit()
            self.assertEqual(viewer.process.returncode, 0)

    def test_ctrl_c_prompts_before_discarding_pending_edits(self):
        with self.viewer() as viewer:
            viewer.expect_mode("text", 0, len(self.original))
            viewer.press(F3 + b"fe" + CTRL_C)
            viewer.expect(lambda frame: "save" in frame.lines[-1].lower()
                          and "discard" in frame.lines[-1].lower(),
                          "unsaved changes prompt")
            self.assertIsNone(viewer.process.poll())
            viewer.press(b"d")
            viewer.wait_for_exit()
            self.assertEqual(viewer.process.returncode, 0)
        self.assertEqual(self.fixture.read_bytes(), self.original)

    def test_ctrl_z_stops_after_restoring_the_terminal(self):
        with self.viewer(own_process_group=True) as viewer:
            viewer.expect_mode("text", 0, len(self.original))
            viewer.press(CTRL_Z)
            self.expect_stopped(viewer)
            self.assertIn(SUSPEND_RESTORE, viewer.buffer)
            # Keys typed at the shell reach the terminal but not the stopped editor.
            viewer.press(b"m")
            self.expect_quiet(viewer)
            self.resume(viewer)
            # Raw mode is re-entered with TCSAFLUSH, so that key is discarded.
            viewer.expect_mode("text", 0, len(self.original))
            viewer.press(b"m")
            viewer.expect_mode("hex", 0, len(self.original))

    def test_ctrl_z_picks_up_a_resize_made_while_stopped(self):
        with self.viewer(columns=80, rows=24, own_process_group=True) as viewer:
            viewer.expect_mode("text", 0, len(self.original))
            viewer.press(CTRL_Z)
            self.expect_stopped(viewer)
            viewer.resize(100, 30)
            self.expect_quiet(viewer)
            self.resume(viewer)
            frame = viewer.expect_mode("text", 0, len(self.original))
            self.assertEqual(len(frame.lines), 30)
            self.assertEqual(len(frame.lines[0]), 100)

    def test_ctrl_z_keeps_pending_edits(self):
        with self.viewer(own_process_group=True) as viewer:
            viewer.expect_mode("text", 0, len(self.original))
            viewer.press(F3 + b"fe")
            viewer.expect(lambda frame: "edit hex*" in frame.lines[-2].lower(),
                          "pending change")
            viewer.press(CTRL_Z)
            self.expect_stopped(viewer)
            self.resume(viewer)
            viewer.expect(lambda frame: "edit hex*" in frame.lines[-2].lower(),
                          "pending change retained across the stop")
            viewer.press(CTRL_C)
            viewer.expect(lambda frame: "discard" in frame.lines[-1].lower(),
                          "unsaved changes prompt")
            viewer.press(b"d")
            viewer.wait_for_exit()
        self.assertEqual(self.fixture.read_bytes(), self.original)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("arguments", nargs="*")
    options = parser.parse_args()
    JobControlTests.binary = options.binary.resolve()
    unittest.main(argv=["test_job_control.py"] + options.arguments)


if __name__ == "__main__":
    main()

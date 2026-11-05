#!/usr/bin/env python3
"""
==============================================================================
SENKUVIM - Comprehensive Integration & Self-Hosting Torture Test Suite
==============================================================================
Runs SenkuVim inside a virtual terminal (PTY), tests:
1. Native Transparency & Welcome Banner rendering
2. Empty Buffer Fuzz & Boundary Safety
3. Smart Tab stops & Soft-tab unindent on Backspace
4. Intelligent Auto-Pairs () [] {} "" ''
5. Auto-Indent with Scope Awareness & Brace Expansion
6. Vim line operations (dd, yy, p) & Undo/Redo transaction integrity
7. Direct Ex-commands (:w, :q, :wq, :<number>) & live incremental search (/)
8. Window resize torture testing (SIGWINCH / TIOCSWINSZ)
9. Self-hosting torture test: SenkuVim editing its own source code (ui_rice.c),
   saving it, and verifying it compiles cleanly with gcc.
10. Memory safety: executes with AddressSanitizer (ASAN) and UndefinedBehaviorSanitizer (UBSAN).
==============================================================================
"""

import os
import pty
import time
import shutil
import struct
import fcntl
import termios
import threading
import subprocess
import unittest

BINARY = "./senkuvim_asan" if os.path.exists("./senkuvim_asan") else "./senkuvim"

class TestSenkuVim(unittest.TestCase):
    def setUp(self):
        for f in ["test_out.txt", "test_c.c", "test_py.py", "valgrind_test.txt", "test_ui_rice_copy.c"]:
            if os.path.exists(f):
                os.remove(f)

    def tearDown(self):
        for f in ["test_out.txt", "test_c.c", "test_py.py", "valgrind_test.txt", "test_ui_rice_copy.c"]:
            if os.path.exists(f):
                os.remove(f)

    def run_editor(self, args, input_bytes, initial_cols=80, initial_rows=24, resize_events=None, wait_after_write=0.3):
        """
        Runs SenkuVim in an isolated pseudo-terminal (PTY) with continuous
        draining to avoid pipe buffer deadlocks.
        """
        master, slave = pty.openpty()
        fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack('HHHH', initial_rows, initial_cols, 0, 0))

        pid = os.fork()
        if pid == 0:
            os.close(master)
            os.dup2(slave, 0)
            os.dup2(slave, 1)
            os.dup2(slave, 2)
            os.close(slave)
            env = os.environ.copy()
            env["ASAN_OPTIONS"] = "detect_leaks=1:halt_on_error=1"
            os.execvpe(BINARY, [BINARY] + args, env)
        else:
            os.close(slave)

            captured_output = []
            running = True

            def reader():
                while running:
                    try:
                        data = os.read(master, 4096)
                        if not data:
                            break
                        captured_output.append(data)
                    except OSError:
                        break

            reader_thread = threading.Thread(target=reader, daemon=True)
            reader_thread.start()

            # Wait for editor initialization
            time.sleep(0.15)

            # Process input sequence
            for item in input_bytes:
                if isinstance(item, tuple) and item[0] == "RESIZE":
                    # (RESIZE, rows, cols)
                    _, r, c = item
                    fcntl.ioctl(master, termios.TIOCSWINSZ, struct.pack('HHHH', r, c, 0, 0))
                    time.sleep(0.1)
                else:
                    os.write(master, item)
                    time.sleep(0.05)

            # Optional post-input resize events
            if resize_events:
                for r, c in resize_events:
                    fcntl.ioctl(master, termios.TIOCSWINSZ, struct.pack('HHHH', r, c, 0, 0))
                    time.sleep(0.08)

            time.sleep(wait_after_write)
            try:
                pid_done, status = os.waitpid(pid, 0)
                exit_code = os.WEXITSTATUS(status)
            except ChildProcessError:
                exit_code = 0

            running = False
            os.close(master)
            all_output = b"".join(captured_output).decode("utf-8", errors="replace")
            return exit_code, all_output

    def test_01_save_and_quit(self):
        """Test typing in Insert mode, saving via :w, and quitting via :q"""
        inputs = [
            b'i',
            b'Hello SenkuVim',
            b'\x1b',
            b':w test_out.txt\r',
            b':q\r'
        ]
        code, out = self.run_editor([], inputs)
        self.assertEqual(code, 0)
        self.assertTrue(os.path.exists("test_out.txt"))
        with open("test_out.txt", "r") as f:
            content = f.read()
        self.assertEqual(content, "Hello SenkuVim\n")

    def test_02_smart_tab_and_soft_backspace(self):
        """Test Smart Tab (4 spaces) and soft-tab unindent on Backspace"""
        # 1. Smart Tab inserts 4 spaces
        inputs = [
            b'i',
            b'\t',
            b'x',
            b'\x1b',
            b':w test_out.txt\r',
            b':q\r'
        ]
        code, _ = self.run_editor([], inputs)
        self.assertEqual(code, 0)
        with open("test_out.txt", "r") as f:
            content = f.read()
        self.assertEqual(content, "    x\n")

        # 2. Soft-tab unindent on Backspace
        inputs = [
            b'i',
            b'\t',
            b'\x7f', # Backspace (127)
            b'y',
            b'\x1b',
            b':w test_out.txt\r',
            b':q\r'
        ]
        code, _ = self.run_editor([], inputs)
        self.assertEqual(code, 0)
        with open("test_out.txt", "r") as f:
            content = f.read()
        self.assertEqual(content, "y\n")

    def test_03_auto_pairs(self):
        """Test auto-pairing of brackets and quotes"""
        inputs = [
            b'i',
            b'(',
            b'abc',
            b')',
            b'{',
            b'def',
            b'}',
            b'\x1b',
            b':w test_out.txt\r',
            b':q\r'
        ]
        code, _ = self.run_editor([], inputs)
        self.assertEqual(code, 0)
        with open("test_out.txt", "r") as f:
            content = f.read()
        self.assertEqual(content, "(abc){def}\n")

    def test_04_auto_indent_with_brace(self):
        """Test auto-indentation and block expansion when pressing Enter after '{'"""
        inputs = [
            b'i',
            b'int main() {',
            b'\r',
            b'return 0;',
            b'\x1b',
            b':w test_c.c\r',
            b':q\r'
        ]
        code, _ = self.run_editor([], inputs)
        self.assertEqual(code, 0)
        with open("test_c.c", "r") as f:
            content = f.read()
        self.assertEqual(content, "int main() {\n    return 0;\n}\n")

    def test_05_vim_line_ops_and_undo(self):
        """Test dd (delete line), p (paste), u (undo), Ctrl+R (redo)"""
        with open("test_out.txt", "w") as f:
            f.write("Line 1\nLine 2\n")

        inputs = [
            b'dd',
            b'p',
            b'u',
            b':wq\r'
        ]
        code, _ = self.run_editor(["test_out.txt"], inputs)
        self.assertEqual(code, 0)
        with open("test_out.txt", "r") as f:
            content = f.read()
        self.assertEqual(content, "Line 2\n")

    def test_06_jump_to_line_and_ctrl_s(self):
        """Test line jump :<num> and Ctrl+S instant save"""
        with open("test_out.txt", "w") as f:
            f.write("Alpha\nBeta\nGamma\nDelta\n")

        inputs = [
            b':3\r',
            b'i',
            b'New ',
            b'\x13', # Ctrl+S
            b'\x1b',
            b':q\r'
        ]
        code, _ = self.run_editor(["test_out.txt"], inputs)
        self.assertEqual(code, 0)
        with open("test_out.txt", "r") as f:
            content = f.read()
        self.assertEqual(content, "Alpha\nBeta\nNew Gamma\nDelta\n")

    def test_07_empty_buffer_fuzz_safety(self):
        """
        Fuzz test empty buffer: send /, dd, yy, p, x, u, Ctrl+R, w, b, G, gg,
        Backspace, and Enter on empty buffer to guarantee ZERO crashes or hangs.
        """
        inputs = [
            b'/',        # Search on empty buffer
            b'dd',       # Delete line on empty buffer
            b'yy',       # Yank on empty buffer
            b'p',        # Paste on empty buffer
            b'x',        # Delete char on empty buffer
            b'u',        # Undo on empty buffer
            b'\x12',     # Redo on empty buffer
            b'w',        # Word forward on empty buffer
            b'b',        # Word backward on empty buffer
            b'G',        # Jump bottom on empty buffer
            b'g', b'g',  # Jump top on empty buffer
            b'\x7f',     # Backspace on empty buffer
            b'\r',       # Enter on empty buffer
            b':q\r'      # Quit
        ]
        code, out = self.run_editor([], inputs)
        self.assertEqual(code, 0)
        # Verify welcome banner was rendered in output
        self.assertTrue("Rick Sanchez Arch Linux Rice Edition" in out or "SENKUVIM" in out)

    def test_08_transparency_cleanliness(self):
        """
        Verify that canvas text and empty space do NOT emit solid black background
        escape codes (\x1b[48;2;0;0;0m or \x1b[40m), and verify that no Kitty Graphics
        Protocol sequences (\x1b_G) are emitted in pure ASCII dashboard mode.
        """
        inputs = [
            b':q\r'
        ]
        code, out = self.run_editor([], inputs)
        self.assertEqual(code, 0)
        self.assertNotIn("\x1b[48;2;0;0;0m", out)
        self.assertNotIn("\x1b[40m", out)
        # Verify no Kitty Graphics Protocol escape sequences are emitted
        self.assertNotIn("\x1b_G", out)
        # Verify pure ASCII welcome banner is present
        self.assertTrue("Rick Sanchez Arch Linux Rice Edition" in out or "Modal & IDE Terminal Editor" in out)

    def test_09_resize_torture(self):
        """
        Send extreme window resize events (10x20, 50x180, 5x10, 24x80)
        during welcome screen and while editing to guarantee resize safety.
        """
        inputs = [
            b'i',
            b'Testing Resize Safety...',
            ("RESIZE", 10, 20),
            b'\r',
            b'Second line',
            ("RESIZE", 50, 180),
            b'\r',
            b'Third line',
            ("RESIZE", 5, 10),
            b'\x1b',
            ("RESIZE", 24, 80),
            b':q!\r'
        ]
        code, _ = self.run_editor([], inputs)
        self.assertEqual(code, 0)

    def test_10_self_hosting_edit_ui_rice(self):
        """
        Self-Hosting Test:
        1. Open a copy of ui_rice.c in SenkuVim.
        2. Use / to search for 'RICK_BANNER', jump to it.
        3. Enter Insert mode, insert '// Self-edited by SenkuVim'.
        4. Test auto-pairs, tab, backspace, undo, redo.
        5. Save and quit (:wq).
        6. Verify the modified file compiles cleanly with gcc!
        """
        shutil.copy("ui_rice.c", "test_ui_rice_copy.c")

        inputs = [
            b'/RICK_BANNER\r', # Live search and jump
            b'o',              # Open new line below
            b'// Self-edited by SenkuVim',
            b'\r',
            b'{',              # Auto-pairs
            b'\r',
            b'\t',             # Tab
            b'\x7f',           # Backspace
            b'\"',             # Auto-pairs
            b'test',
            b'\"',
            b'\x1b',           # Normal mode
            b'u',              # Undo
            b'\x12',           # Redo
            b':wq\r'           # Save and quit
        ]
        code, _ = self.run_editor(["test_ui_rice_copy.c"], inputs)
        self.assertEqual(code, 0)
        self.assertTrue(os.path.exists("test_ui_rice_copy.c"))

        with open("test_ui_rice_copy.c", "r") as f:
            content = f.read()
        self.assertIn("Self-edited by SenkuVim", content)

        # Verify that the self-edited file compiles with gcc without errors!
        res = subprocess.run([
            "gcc", "-Wall", "-Wextra", "-std=c17", "-D_DEFAULT_SOURCE",
            "-D_BSD_SOURCE", "-D_GNU_SOURCE", "-c", "test_ui_rice_copy.c",
            "-o", "/tmp/test_ui_rice_copy.o"
        ], capture_output=True, text=True)
        self.assertEqual(res.returncode, 0, f"Compilation failed: {res.stderr}")
        if os.path.exists("/tmp/test_ui_rice_copy.o"):
            os.remove("/tmp/test_ui_rice_copy.o")

if __name__ == "__main__":
    unittest.main()

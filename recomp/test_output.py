#!/usr/bin/env python3
"""Unit tests for content-stable recomp output: python3 test_output.py"""
import os
import sys
import tempfile
import time
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import recomp


class Output(unittest.TestCase):
    def test_same_text_leaves_mtime(self):
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "code_000.c")
            with recomp._Output(path) as f:
                f.write("hello\n")
            first = os.stat(path).st_mtime_ns
            time.sleep(0.02)
            with recomp._Output(path) as f:
                f.write("hello\n")
            self.assertEqual(os.stat(path).st_mtime_ns, first)

    def test_changed_text_rewrites(self):
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "code_000.c")
            with recomp._Output(path) as f:
                f.write("hello\n")
            first = os.stat(path).st_mtime_ns
            time.sleep(0.02)
            with recomp._Output(path) as f:
                f.write("world\n")
            self.assertGreater(os.stat(path).st_mtime_ns, first)
            with open(path, encoding="utf-8") as f:
                self.assertEqual(f.read(), "world\n")


if __name__ == "__main__":
    unittest.main(verbosity=2)

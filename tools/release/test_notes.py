#!/usr/bin/env python3
"""Release notes: a release takes its own changelog block, a development build takes [Unreleased]."""
import os
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
CHANGELOG = """# Changelog

## [Unreleased]

### Changed

- Rolling change.

## [0.2.0] - 2026-01-02

### Added

- Released change.

## [0.1.0] - 2026-01-01

- Older change.
"""


class Notes(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.readme = os.path.join(self.tmp.name, "README.md")
        open(self.readme, "w").close()
        with open(os.path.join(self.tmp.name, "CHANGELOG.md"), "w", encoding="utf-8") as f:
            f.write(CHANGELOG)

    def tearDown(self):
        self.tmp.cleanup()

    def notes(self, *args):
        out = subprocess.run([sys.executable, "-B", os.path.join(HERE, "notes.py"), self.readme, *args],
                             capture_output=True, text=True, check=True)
        return out.stdout

    def test_release_takes_its_own_block(self):
        text = self.notes("v0.2.0")
        self.assertIn("Released change.", text)
        self.assertNotIn("Rolling change.", text)
        self.assertNotIn("Development build", text)

    def test_development_build_takes_the_unreleased_block(self):
        text = self.notes("v0.2.0-devel.0123abcd", "--devel", "0123abcd" + "0" * 32)
        self.assertIn("Rolling change.", text)
        self.assertNotIn("Released change.", text)
        self.assertIn("Development build of main @ 0123abc: not a release.", text)
        self.assertIn("native PC port, v0.2.0-devel.0123abcd**", text)

    def test_named_test_build_takes_the_unreleased_block(self):
        text = self.notes("v0.2.0-test.issue-60.0123abcd", "--test", "issue-60", "0123abcd" + "0" * 32)
        self.assertIn("Rolling change.", text)
        self.assertNotIn("Released change.", text)
        self.assertIn("Test build `issue-60` @ 0123abc: not a release.", text)
        self.assertIn("does not replace the development build", text)
        self.assertNotIn("Development build of main", text)

    def test_windows_arm64_is_named(self):
        self.assertIn("`windows-arm64`", self.notes("v0.2.0"))


if __name__ == "__main__":
    unittest.main()

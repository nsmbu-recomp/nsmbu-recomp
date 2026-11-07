#!/usr/bin/env python3
import importlib.util
import unittest
from pathlib import Path

_path = Path(__file__).resolve().parents[3] / ".githooks" / "pre_push.py"
_spec = importlib.util.spec_from_file_location("pre_push", _path)
pre = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(pre)

SHA_A = "a" * 40
SHA_B = "b" * 40


class PrePushTest(unittest.TestCase):
    def test_skips_deletes_and_duplicates(self):
        text = "\n".join([
            f"refs/heads/dev {SHA_A} refs/heads/dev {SHA_B}",
            f"refs/heads/old {'0' * 40} refs/heads/old {SHA_B}",
            f"refs/heads/dev {SHA_A} refs/heads/dev {SHA_B}",
            f"refs/heads/sha256 {'0' * 64} refs/heads/sha256 {SHA_B}",
            "",
        ])
        self.assertEqual(pre.parse_push_lines(text), [SHA_A])

    def test_rejects_a_short_ref_line(self):
        with self.assertRaises(ValueError):
            pre.parse_push_lines("refs/heads/dev abc")

    def test_in_place_only_when_the_checkout_is_that_commit(self):
        self.assertTrue(pre.tests_in_checkout(SHA_A, SHA_A, True))
        self.assertFalse(pre.tests_in_checkout(SHA_A, SHA_A, False))
        self.assertFalse(pre.tests_in_checkout(SHA_B, SHA_A, True))

    def test_commands_are_the_three_unit_suites(self):
        commands = [" ".join(cmd) for cmd in pre.test_commands("build/debug")]
        self.assertEqual(
            commands,
            [
                "ctest --test-dir build/debug --output-on-failure",
                f"{pre.sys.executable} -m unittest discover -s tests/py -p test_*.py",
                "npm test --prefix tools/cli",
            ],
        )


if __name__ == "__main__":
    unittest.main()

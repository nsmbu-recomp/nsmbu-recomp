#!/usr/bin/env python3
import os
import sys
import unittest
from pathlib import Path
from types import SimpleNamespace

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "recomp"))
import switches


class Switches(unittest.TestCase):
    def setUp(self):
        switches.S.reset()

    def test_plain_env_ignored(self):
        os.environ["NSMBU_RECOMP_PLAIN"] = "1"
        self.assertTrue(switches.pass_on("NSMBU_RECOMP_CRLIVE"))

    def test_apply_args_plain(self):
        switches.apply_args(SimpleNamespace(plain=True))
        self.assertFalse(switches.pass_on("NSMBU_RECOMP_CRLIVE"))
        switches.S.reset()
        switches.apply_args(SimpleNamespace(plain=False))
        self.assertTrue(switches.pass_on("NSMBU_RECOMP_CRLIVE"))


if __name__ == "__main__":
    unittest.main()
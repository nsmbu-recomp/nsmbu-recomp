#!/usr/bin/env python3
"""Sequential fallback when multiprocessing cannot run: python3 test_pool.py"""
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import recomp
import singleflow


def _missing_mp():
    raise ImportError("No module named '_multiprocessing'")


class Pool(unittest.TestCase):
    def test_missing_multiprocessing_uses_one_process(self):
        recomp_import = recomp._import_mp
        single_import = singleflow._import_mp
        try:
            recomp._import_mp = _missing_mp
            singleflow._import_mp = _missing_mp
            pool, path = recomp._open_pool(4, object())
            self.assertIsInstance(pool, recomp._SeqPool)
            self.assertIsNone(path)
            self.assertFalse(singleflow._fork_ok())
        finally:
            recomp._import_mp = recomp_import
            singleflow._import_mp = single_import

    def test_missing_extension_uses_one_process(self):
        import builtins
        real_import = builtins.__import__

        def guarded(name, globals=None, locals=None, fromlist=(), level=0):
            if name == "_multiprocessing" or name.startswith("_multiprocessing."):
                raise ImportError("No module named '_multiprocessing'")
            return real_import(name, globals, locals, fromlist, level)

        builtins.__import__ = guarded
        try:
            pool, path = recomp._open_pool(4, object())
            self.assertIsInstance(pool, recomp._SeqPool)
            self.assertIsNone(path)
            self.assertFalse(recomp._fork_ok())
            self.assertFalse(singleflow._fork_ok())
        finally:
            builtins.__import__ = real_import


if __name__ == "__main__":
    unittest.main(verbosity=2)

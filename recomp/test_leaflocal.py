#!/usr/bin/env python3
"""Unit tests for non-leaf register locals: python3 test_leaflocal.py
"""
import os
import re
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import leaflocal

_SWITCHES = (
    "NSMBU_RECOMP_NONLEAF_LIVE",
    "NSMBU_RECOMP_PLAIN",
    "NSMBU_RECOMP_LEAF_POISON",
)


def gpr_reloads(text):
    return {int(m.group(1)) for m in re.finditer(r"(?<![\w])r(\d+) = c->r\[\1\];", text)}


def fpr_reloads(text):
    return {int(m.group(1)) for m in re.finditer(
        r"(?<![\w])f(\d+)_0 = c->f\[\1\]\.ps0; f\1_1 = c->f\[\1\]\.ps1;", text)}


class Nonleaf(unittest.TestCase):
    def setUp(self):
        self._env = dict(os.environ)
        for key in _SWITCHES:
            os.environ.pop(key, None)

    def tearDown(self):
        os.environ.clear()
        os.environ.update(self._env)

    def nonleaf(self, stmts, addrs=None):
        if addrs is None:
            addrs = [0x02000000 + 4 * i for i in range(len(stmts))]
        done = leaflocal.transform_nonleaf(stmts, addrs)
        self.assertIsNotNone(done)
        return done

    def test_straight_line_skips_overwritten_registers(self):
        _, out, _ = self.nonleaf([
            "c->r[3] = 1;",
            "c->r[4] = 2;",
            "f_02000000(c);",
            "c->r[3] = 3;",
            "c->r[6] = c->r[3];",
        ])
        self.assertEqual(gpr_reloads(out[2]), set())

    def test_use_after_call_in_the_same_statement(self):
        _, out, _ = self.nonleaf(["site_02000000(c); c->r[4] = c->r[3];"])
        self.assertEqual(gpr_reloads(out[0]), {3})

    def test_loop_reloads_the_branch_register(self):
        _, out, _ = self.nonleaf([
            "c->r[3] = 1;",
            "f_02000000(c);",
            "if (c->r[4]) goto L_02000000;",
        ], [0x02000000, 0x02000004, 0x02000008])
        self.assertEqual(gpr_reloads(out[1]), {4})

    def test_two_calls_fallthrough(self):
        _, out, _ = self.nonleaf([
            "f_02000000(c); goto L_0200000C;",
            "c->r[6] = c->r[5];",
            "f_02000004(c);",
            "c->r[6] = c->r[7];",
        ], [0x02000000, 0x02000004, 0x02000008, 0x0200000C])
        self.assertEqual(gpr_reloads(out[0]), {5, 7})
        self.assertEqual(gpr_reloads(out[2]), {7})

    def test_assign_after_call_kills_the_return_store(self):
        _, out, _ = self.nonleaf([
            "c->r[3] = 1;",
            "f_02000000(c);",
            "c->r[3] = 2;",
            "return;",
        ])
        self.assertEqual(gpr_reloads(out[1]), set())

    def test_call_reloads_the_register_the_join_stores(self):
        _, out, _ = self.nonleaf([
            "c->r[3] = c->r[3] + 1;",
            "if (c->r[4]) goto L_0200000C;",
            "f_02000000(c);",
            "return;",
        ], [0x02000000, 0x02000004, 0x02000008, 0x0200000C])
        self.assertIn("r3 = c->r[3];", out[2])

    def test_branch_reloads_the_register_read_after_the_call(self):
        _, out, _ = self.nonleaf([
            "if (c->r[3]) goto L_0200000C;",
            "f_02000000(c);",
            "c->r[6] = c->r[5];",
            "return;",
        ], [0x02000000, 0x02000004, 0x02000008, 0x0200000C])
        self.assertEqual(gpr_reloads(out[1]), {5})

    def test_call_writes_back_a_dirty_r0(self):
        _, out, _ = self.nonleaf(
            ["c->r[0] = 1; c->r[3] = 2; f_02000000(c); return;"])
        pre, _ = out[0].split("f_02000000(c);", 1)
        self.assertIn("c->r[0] = r0;", pre)

    def test_call_writes_back_a_dirty_r12(self):
        _, out, _ = self.nonleaf(
            ["c->r[12] = 1; c->r[3] = 2; f_02000000(c); return;"])
        pre, _ = out[0].split("f_02000000(c);", 1)
        self.assertIn("c->r[12] = r12;", pre)

    def test_call_reloads_a_live_r0(self):
        _, out, _ = self.nonleaf(
            ["c->r[0] = c->r[0] + 1; f_02000000(c); c->r[4] = c->r[0];"])
        _, post = out[0].split("f_02000000(c);", 1)
        self.assertIn("r0 = c->r[0];", post)

    def test_poison_fills_a_dead_local_after_the_call(self):
        os.environ["NSMBU_RECOMP_LEAF_POISON"] = "1"
        _, out, _ = self.nonleaf([
            "c->r[0] = 1; c->r[3] = 2; c->r[4] = 3; f_02000000(c); c->r[5] = c->r[4] + c->r[0];",
        ])
        text = out[0]
        self.assertIn("r3 = 0xDEADBEEFu;", text)
        self.assertNotIn("r4 = 0xDEADBEEFu;", text)
        self.assertNotIn("r0 = 0xDEADBEEFu;", text)
        self.assertIn("r4 = c->r[4];", text)

    def test_live_switch_and_plain(self):
        stmts = ["c->r[3] = 1; f_02000000(c); c->r[3] = 2;"]
        os.environ["NSMBU_RECOMP_NONLEAF_LIVE"] = "0"
        _, out, _ = self.nonleaf(stmts)
        self.assertEqual(gpr_reloads(out[0]), {3})
        os.environ.pop("NSMBU_RECOMP_NONLEAF_LIVE")
        os.environ["NSMBU_RECOMP_PLAIN"] = "1"
        _, out, _ = self.nonleaf(stmts)
        self.assertEqual(gpr_reloads(out[0]), {3})
        os.environ["NSMBU_RECOMP_NONLEAF_LIVE"] = "1"
        _, out, _ = self.nonleaf(stmts)
        self.assertEqual(gpr_reloads(out[0]), set())

    def test_fpr_read_reloads_both_halves(self):
        _, out, _ = self.nonleaf(["f_02000000(c); c->r[3] = c->f[1].ps0;"])
        self.assertEqual(fpr_reloads(out[0]), {1})
        self.assertNotIn("r3 = c->r[3];", out[0])

    def test_one_float_half_does_not_kill_the_other(self):
        os.environ["NSMBU_RECOMP_LEAF_POISON"] = "1"
        _, out, _ = self.nonleaf([
            "f_02000000(c);",
            "c->f[1].ps0 = c->f[2].ps0;",
            "c->f[3].ps0 = c->f[1].ps1;",
        ])
        self.assertEqual(fpr_reloads(out[0]), {1, 2})
        self.assertIn("f1_0 = c->f[1].ps0; f1_1 = c->f[1].ps1;", out[0])
        self.assertNotIn("f1_0 = f1_1 = __builtin_nan", out[0])

    def test_a_later_call_stores_only_the_float_half_it_wrote(self):
        _, out, _ = self.nonleaf([
            "f_02000000(c);",
            "c->f[1].ps0 = c->f[2].ps0;",
            "f_02000004(c);",
            "c->f[3].ps0 = c->f[1].ps1;",
        ])
        self.assertNotIn("c->f[1].ps1 = f1_1;", out[2])
        self.assertIn("c->f[1].ps0 = f1_0;", out[2])
        self.assertIn("f1_0 = c->f[1].ps0; f1_1 = c->f[1].ps1;", out[2])


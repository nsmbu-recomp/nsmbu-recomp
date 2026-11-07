"""Single-precision register tracking in ppc2c.fp_transfer."""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "recomp"))
import ppc2c


def word(op, d=0, a=0, b=0, xo=0, lk=0):
    return (op << 26) | (d << 21) | (a << 16) | (b << 11) | (xo << 1) | lk


def marked(state):
    found = []
    for reg in range(32):
        for half in (0, 1):
            if state >> (2 * reg + half) & 1:
                found.append((reg, half))
    return found


class SinglePrecision(unittest.TestCase):
    def test_lfs_marks_both_halves(self):
        state = ppc2c.fp_transfer(0, word(48, d=3), True)
        self.assertEqual(marked(state), [(3, 0), (3, 1)])

    def test_fmuls_marks_both_halves(self):
        state = ppc2c.fp_transfer(0, word(59, d=4), True)
        self.assertEqual(marked(state), [(4, 0), (4, 1)])

    def test_fmr_copies_the_source_half(self):
        source = 1 << (2 * 1)
        state = ppc2c.fp_transfer(source, word(63, d=2, b=1, xo=72), True)
        self.assertIn((2, 0), marked(state))
        self.assertNotIn((2, 1), marked(state))

    def test_call_keeps_only_saved_registers(self):
        before = (1 << (2 * 3)) | (1 << (2 * 14))
        state = ppc2c.fp_transfer(before, word(18, lk=1), True)
        self.assertEqual(marked(state), [(14, 0)])

    def test_call_drops_saved_registers_when_the_callee_does_not_restore_them(self):
        before = 1 << (2 * 14)
        state = ppc2c.fp_transfer(before, word(18, lk=1), False)
        self.assertEqual(marked(state), [])


if __name__ == "__main__":
    unittest.main()

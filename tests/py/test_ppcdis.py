"""ppcdis queries that replace one-off scans of .text."""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools/rpx"))
import ppcdis


def branch(addr, dest, lk=0, absolute=0):
    off = (dest - addr) & 0x03FFFFFC
    return (18 << 26) | off | (absolute << 1) | lk


def dform(op, rd, ra, disp):
    return (op << 26) | (rd << 21) | (ra << 16) | (disp & 0xFFFF)


class Callers(unittest.TestCase):
    def test_direct_branch_and_call(self):
        words = [
            branch(0x1000, 0x2000, lk=1),
            branch(0x1004, 0x2000, lk=0),
            branch(0x1008, 0x2004, lk=1),
            branch(0x100C, 0x2000, lk=1, absolute=1),
        ]
        self.assertEqual(ppcdis.callers_of(words, 0x1000, 0x2000), [
            (0x1000, "bl"),
            (0x1004, "b"),
        ])

    def test_format_lists_each_site(self):
        words = [branch(0x1000, 0x1800, lk=1)]
        self.assertEqual(ppcdis.format_callers(words, 0x1000, 0x1800), "00001000 bl")


class FunctionSpan(unittest.TestCase):
    def test_address_inside_a_function(self):
        entries = [0x1000, 0x1100, 0x1200]
        self.assertEqual(ppcdis.format_func(entries, 0x1040, 0x1300),
                         "start 00001000 end 00001100 size 256 insns 64")

    def test_address_outside_the_entries(self):
        self.assertEqual(ppcdis.format_func([0x1000], 0x900, 0x1100), "outside")
        self.assertIsNone(ppcdis.function_span([0x1000, 0x1100], 0x1100, 0x1100))


class Access(unittest.TestCase):
    def test_load_and_store_of_one_offset(self):
        words = [
            dform(36, 3, 31, 0x251),
            dform(34, 4, 31, 0x250),
            dform(32, 5, 30, 0x251),
        ]
        text = ppcdis.format_accesses(words, 0x2000, 0x251)
        self.assertEqual(text, "00002000 stw r3, 0x251(r31)\n00002008 lwz r5, 0x251(r30)")

    def test_range_excludes_the_end(self):
        words = [dform(38, 3, 4, 0x10), 0, dform(38, 3, 4, 0x10)]
        hits = ppcdis.accesses(words, 0x3000, 0x10, 0x3000, 0x3008)
        self.assertEqual([hit[0] for hit in hits], [0x3000])


class Xref(unittest.TestCase):
    def test_cstring_and_lis_addi(self):
        blob = b"\0PauseGame.bflyt\0other\0"
        found = ppcdis.find_strings(blob, 0x80000000, "Pause")
        self.assertEqual(found, {0x80000001: "PauseGame.bflyt"})
        lis = dform(15, 3, 0, 0x8000)
        addi = dform(14, 3, 3, 0x0001)
        sites = ppcdis.lis_sites([lis, addi], 0x4000, set(found))
        self.assertEqual(sites, [0x4000])

    def test_negative_addi_fixes_the_high_half(self):
        lis = dform(15, 3, 0, 0x8001)
        addi = dform(14, 3, 3, 0xF000)
        sites = ppcdis.lis_sites([lis, addi], 0x5000, {0x8000F000})
        self.assertEqual(sites, [0x5000])

    def test_reloc_and_missing_string(self):
        self.assertEqual(ppcdis.reloc_sites([(0x6000, 1, 0x80000001, 0)], {0x80000001}), [0x6000])
        self.assertEqual(ppcdis.format_xref({}, [], []), "none")

    def test_addi_past_the_window_is_ignored(self):
        lis = dform(15, 3, 0, 0x1000)
        gap = [0] * 9
        addi = dform(14, 3, 3, 0x20)
        sites = ppcdis.lis_sites([lis] + gap + [addi], 0x7000, {0x10000020})
        self.assertEqual(sites, [])

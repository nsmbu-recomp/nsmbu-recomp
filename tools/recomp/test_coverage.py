# SPDX-License-Identifier: MPL-2.0
"""Guard runtime game-address uses against the USA identity map.

This is a source audit, not a C++ parser or proof of matching field layouts.
Keep non-address exceptions narrow and documented; new game literals must use
GC/GD.
"""
from pathlib import Path
import re
import unittest

import builds

ROOT = Path(__file__).resolve().parents[2]
NON_ADDRESS = {
    ("core/runtime.h", 0x02800000): "foreground bucket size",
    ("core/runtime.h", 0x02000000): "MEM1 size",
    ("mods/lua_guest.cpp", 0x02800000): "foreground bucket size",
    ("mods/lua_guest.cpp", 0x02000000): "MEM1 size",
    ("core/core.cpp", 0x02000000): "executable memory lower bound",
    ("gfx/metal/metal_main.mm", 0x02000000): "executable memory lower bound",
    ("hle/system_stubs.cpp", 0x02000000): "executable memory lower bound",
    ("mods/mod_archive.cpp", 0x02014B50): "ZIP central directory signature",
}
NON_ADDRESS.update({("core/espresso_fp.c", value): "frsqrte mantissa lookup table"
                    for value in (0x02FB7000, 0x02D26000, 0x02AC0000, 0x02881000,
                                  0x02665000, 0x02468000, 0x02287000, 0x020C1000)})
TRIVIA = re.compile(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'', re.S)
LITERAL = re.compile(r'\b0[xX]([0-9a-fA-F]{1,8})(?![0-9a-fA-F])[uUlL]*\b')
WRAPPED = re.compile(r'\b(GC|GD)\s*\([^;]*?\)', re.S)
FIXED_GAME_ALLOCATIONS = {0x145AC92C, 0x3BB9DE00, 0x44BDF900, 0x44BDFD00}

def address_errors(source, name, mapping):
    text = TRIVIA.sub(lambda m: re.sub(r"[^\n]", " ", m[0]), source)
    spans = [(m.start(), m.end(), "code" if m[1] == "GC" else "data")
             for m in WRAPPED.finditer(text)]
    errors = []
    for m in LITERAL.finditer(text):
        a = int(m[1], 16)
        if a in FIXED_GAME_ALLOCATIONS:
            line = text.count("\n", 0, m.start()) + 1
            errors.append(f"{name}:{line}: fixed game allocation; use a mapped global or program identity")
            continue
        kind = "code" if 0x02000000 <= a < 0x03000000 else "data" if 0x10000000 <= a < 0x10500000 else None

        if kind is None or a == 0x10000000 or (name, a) in NON_ADDRESS:
            continue
        line = text.count("\n", 0, m.start()) + 1
        if not any(lo <= m.start() < hi and wrapper == kind for lo, hi, wrapper in spans):
            errors.append(f"{name}:{line}: {a:08X} bypasses GC/GD")
        if not valid_address(mapping, kind, a):
            errors.append(f"{name}:{line}: {a:08X} has no mapping")

        tail = re.match(r'\s*\+\s*(0x[0-9a-fA-F]+)', text[m.end():])
        if tail and not valid_address(mapping, kind, a + int(tail[1], 16)):
            errors.append(f"{name}:{line}: derived address has no mapping")
    return errors

def valid_address(mapping, kind, address):
    try:
        getattr(mapping, kind)(address)
    except ValueError:
        return False
    return True

class CoverageTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.mapping = builds.canonical_build()

    def test_every_active_hook_is_mapped(self):
        entries, skipped = builds.read_hooks(builds.hook_files(), builds.canonical_build())
        self.assertEqual(skipped, [])
        self.assertEqual(
            [(site, canon) for site, canon, _, _ in entries],
            [(False, 0x02ACD218), (False, 0x024D752C), (True, 0x022A79C4),
             (False, 0x02994164), (False, 0x029CBBB4), (False, 0x024BAD4C),
             (False, 0x024BD86C), (True, 0x024BD96C), (True, 0x024BE280),
             (False, 0x025D8B28), (False, 0x02672C90), (False, 0x02681BFC),
             (False, 0x0267CB3C), (False, 0x0252F900), (False, 0x025CF020)],
        )
        for site, canon, native, where in entries:
            self.assertEqual(canon, native, where)

    def test_runtime_game_literals_are_wrapped_and_mapped(self):
        errors = []
        root = ROOT / "runtime/src"
        for path in sorted(root.rglob("*")):
            if path.suffix not in (".cpp", ".h", ".c", ".mm"):
                continue
            name = path.relative_to(root).as_posix()
            errors.extend(address_errors(path.read_text(encoding="utf-8"), name, self.mapping))
        self.assertEqual(errors, [], "\n".join(errors))

    def test_portable_play_fields_keep_their_offsets(self):
        source = (ROOT / "runtime/src/save/savestate.cpp").read_text(encoding="utf-8")
        match = re.search(r'kPlay\s*=\s*GD\(0x([0-9a-fA-F]+)\)', source)
        if not match:
            self.skipTest("no kPlay GD base in savestate.cpp")
        base = int(match[1], 16)
        offsets = {int(m[1], 16) for m in re.finditer(r'\bkPlay\s*\+\s*0x([0-9a-fA-F]+)', source)}
        self.assertGreaterEqual(len(offsets), 1)
        for offset in offsets:
            self.assertEqual(self.mapping.data(base + offset), self.mapping.data(base) + offset,
                             f"portable-state field +{offset:X} crosses a data shift")

    def test_guard_rejects_raw_addresses(self):
        self.assertTrue(address_errors("ld32(0x101F84DC);", "new.cpp", self.mapping))

if __name__ == "__main__":
    unittest.main(verbosity=2)

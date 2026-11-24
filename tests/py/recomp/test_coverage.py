# SPDX-License-Identifier: MPL-2.0
"""Guard runtime game-address uses against the USA identity map.

This is a source audit, not a C++ parser or proof of matching field layouts.
Keep non-address exceptions narrow and documented; new game literals must use
GC/GD.
"""
from pathlib import Path
import re
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "recomp"))
import builds

ROOT = Path(__file__).resolve().parents[3]
NON_ADDRESS = {
    ("core/runtime.h", 0x02800000): "foreground bucket size",
    ("core/runtime.h", 0x02000000): "MEM1 size",
    ("mods/guest_mem.cpp", 0x02800000): "foreground bucket size",
    ("mods/guest_mem.cpp", 0x02000000): "MEM1 size",
    ("core/core.cpp", 0x02000000): "executable memory lower bound",
    ("gfx/metal/metal_main.mm", 0x02000000): "executable memory lower bound",
    ("hle/system_stubs.cpp", 0x02000000): "executable memory lower bound",
    ("hooks/interp.cpp", 0x02000000): "pointer lower bound",
    ("gx2/gx2_core.cpp", 0x02000000): "display list pointer lower bound",
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
        self.assertEqual(skipped, [("base_hud.jsonc", "USA_V0 EU_V0")])
        self.assertEqual(
            sorted((site, canon) for site, canon, *_ in entries),
            sorted(
            [(False, 0x02ACD218), (False, 0x024D752C), (True, 0x022674E8), (True, 0x02267C48),
             (True, 0x02267C4C), (True, 0x02267C70), (True, 0x0228E0CC), (True, 0x0228E228),
             (True, 0x0228E234), (True, 0x024DE2D0),
             (True, 0x022A79C4), (False, 0x022A7760),
             (False, 0x029A6170), (False, 0x029B85C8), (False, 0x02A06818), (False, 0x02A061AC),
             (False, 0x02A062EC), (False, 0x02994164), (False, 0x029CBBB4), (False, 0x024BD86C),
             (True, 0x024B814C), (True, 0x024B8324), (False, 0x0226FBC4), (True, 0x0226FD14), (True, 0x0226FECC),
             (True, 0x02270310), (True, 0x022703A4), (True, 0x0227CCF4), (True, 0x0227E2A8),
             (False, 0x0252F900), (False, 0x025CF020), (False, 0x024E25F4), (False, 0x024E29B8),
             (False, 0x024EFE58), (False, 0x024F009C),
             (False, 0x029C52CC), (False, 0x024FBF6C), (False, 0x024B47A8), (False, 0x024B47C8),
             (False, 0x026904D4), (False, 0x026905C0), (False, 0x02690680), (False, 0x0269083C),
             (False, 0x02691690), (False, 0x0269179C), (False, 0x026917E4), (False, 0x02691868),
             (False, 0x0269196C), (False, 0x02691970), (False, 0x026919A4), (False, 0x02691A8C),
             (False, 0x02691B74), (False, 0x02691DF0), (False, 0x02691EAC), (False, 0x0269206C),
             (False, 0x026920B0), (False, 0x02692194), (False, 0x0269222C), (False, 0x02692748),
             (False, 0x0269282C), (False, 0x026928C4), (False, 0x0269325C), (False, 0x0268B70C),
             (False, 0x029C4FE4),
             (False, 0x029FD94C), (False, 0x024F30DC), (False, 0x029D9E30),
             (True, 0x029D94C0),
             (False, 0x029D9560), (True, 0x029D9CB8), (True, 0x029D9E2C), (False, 0x029D8270),
             (False, 0x029D8304), (False, 0x029D82D4), (False, 0x029D8394), (False, 0x029872BC),
             (False, 0x029D8478), (False, 0x029D7648), (False, 0x029D8C10), (True, 0x029D8C48),
             (True, 0x029D71A4), (True, 0x02631BE4), (True, 0x0263ADD8), (True, 0x02641A4C),
             (True, 0x026730C4), (True, 0x02AB7040), (True, 0x02682674),
             (False, 0x025A4218), (False, 0x025A42CC), (False, 0x025A4B98), (True, 0x025A4C5C),
             (False, 0x02ABC054), (False, 0x02ABCAE8), (True, 0x02ABCB34), (True, 0x02ABCB38)]),
        )
        for site, canon, native, where, _same in entries:
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

    def test_death_course_out_seeds_the_directed_map(self):
        hooks = (ROOT / "recomp/hooks/course.jsonc").read_text(encoding="utf-8")
        self.assertIn('"02267C48"', hooks)
        limited = (ROOT / "recomp/hooks/course_return.jsonc").read_text(encoding="utf-8")
        self.assertIn('"024B814C"', limited)
        self.assertIn('"USA"', limited)
        self.assertNotIn("USA_V0", limited)
        source = (ROOT / "runtime/src/hooks/cs_probe.cpp").read_text(encoding="utf-8")
        start = source.index("void site_024B814C")
        end = source.index("void site_024B8324")
        body = source[start:end]
        self.assertIn("g_have_pack", body)
        self.assertIn("nsmbu_mark_course_left", body)
        self.assertIn("seed_map_return_pose", body)
        sync = source[source.index("void sync_directed_controller() {"):source.index("static void release_directed_map(uint32_t task) {")]
        self.assertIn("kDrcOff", sync)
        self.assertIn("game + 0x14u, 0", sync)
        self.assertNotIn("set_pro_controller", sync)
        self.assertIn("sync_directed_controller", source[source.index("void nsmbu_mark_course_left"):source.index("void site_024B814C")])
        save = (ROOT / "runtime/src/mods/sdk/nsmbu.cpp").read_text(encoding="utf-8")
        apply = save[save.index("bool apply_save_file"):save.index("bool leave_course_impl")]
        self.assertIn("pro_controller() ? 0 : 1", apply)
        self.assertNotIn("kDrcOff", apply)
        self.assertNotIn("drc = 0", apply)
        start = source.index("void site_02267C48")
        end = source.index("void site_02267C4C")
        iris = source[start:end]
        self.assertIn("g_map_return_open", iris)
        self.assertIn("release_directed_map", iris)
        self.assertIn("GD(0x101FD3A4)", iris)
        flags = source[source.index("void release_directed_map"):source.index("void nsmbu_mark_course_left")]
        self.assertIn("0xDC8u, 0", flags)
        self.assertIn("0xDC9u, 0", flags)
        self.assertIn("0xDCBu, 1", flags)
        self.assertIn("0xDCDu, 0", flags)
        self.assertIn("0xDCEu, 0", flags)
        model = source[source.index("void site_022674E8"):source.index("void site_02267C70")]
        self.assertIn("release_directed_map", model)
        stick = source[source.index("void site_0228E228"):source.index("void site_0228E234")]
        self.assertIn("r[26] = 0", stick)
        gate = source[source.index("void site_0228E234"):source.index("void site_024DE2D0")]
        self.assertIn("0x623u, 1", gate)
        mesh = source[source.index("void site_024DE2D0"):source.index("void site_022674E8")]
        self.assertIn("r[6] = 0", mesh)
        hold = source[source.index("void site_02267C70"):]
        self.assertIn("0xDCAu, 1", hold)
        self.assertIn("0xDCDu, 0", hold)

if __name__ == "__main__":
    unittest.main(verbosity=2)

#!/usr/bin/env python3
"""Unit tests for the build registry: python3 test_builds.py

No game files: the registry holds title ids and SHA-256 digests only.
"""
import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import builds

HERE = os.path.dirname(os.path.abspath(__file__))

class Registry(unittest.TestCase):
    def test_canonical_is_usa_and_identity(self):
        usa = builds.canonical_build()
        self.assertEqual(usa.name, "USA")
        self.assertEqual(usa.title_id, "0005000010101d00")
        self.assertEqual(
            usa.sha256,
            "ebd147ce4cdedb563db3ac6278186ca5dea282ad315271925b8bb5ac16c7dc8e",
        )
        self.assertTrue(usa.canonical)
        for a in (0x02000020, 0x025F172C, 0x028F8250, 0x101F4BAC, 0x1046F0B0):
            self.assertEqual(usa.code(a), a)
            self.assertEqual(usa.data(a), a)
            self.assertFalse(usa.body_differs(a))

    def test_known_builds(self):
        names = {b.name for b in builds.all_builds()}
        self.assertEqual(names, {"USA", "USA_V0", "EU", "EU_V0", "NSMBU_NSLU_USA", "NSMBU_NSLU_EU"})
        self.assertEqual(builds.by_title("0005000010101d00").name, "USA")
        self.assertEqual(builds.by_title_version("0005000010101d00", 0).name, "USA_V0")
        self.assertEqual(builds.by_title("0005000010101e00").name, "EU")
        self.assertEqual(builds.by_title_version("0005000010101e00", 0).name, "EU_V0")
        self.assertEqual(
            builds.by_sha256("2f4f2da59f99111546b89ecf512a4a0ecf6a2e292046ac500b1fb72ca19105c9").name,
            "EU_V0",
        )
        self.assertEqual(builds.by_title("000500001014b700").name, "NSMBU_NSLU_USA")
        self.assertEqual(builds.by_title("000500001014b800").name, "NSMBU_NSLU_EU")
        self.assertEqual(builds.by_name("eu").title_version, 80)
        self.assertEqual(builds.by_name("USA_V0").title_version, 0)
        self.assertFalse(builds.by_name("USA_V0").mods)
        self.assertEqual(builds.by_name("EU_V0").title_version, 0)
        self.assertFalse(builds.by_name("EU_V0").mods)
        self.assertTrue(builds.canonical_build().mods)
        self.assertEqual(builds.by_name("NSMBU_NSLU_USA").title_version, 0)
        self.assertEqual(builds.by_name("NSMBU_NSLU_EU").title_version, 0)
        self.assertEqual(builds.canonical_build().title_version, 64)
        self.assertEqual(builds.canonical_build().issue_label(), "USA 1.3.0")
        self.assertEqual(builds.by_name("USA_V0").issue_label(), "USA base disc")
        self.assertEqual(builds.by_name("EU").issue_label(), "Europe 1.3.0")
        self.assertEqual(builds.by_name("EU_V0").issue_label(), "Europe base disc")
        self.assertEqual(builds.by_name("NSMBU_NSLU_USA").issue_label(), "NSMBU + NSLU (USA)")
        self.assertEqual(builds.by_name("NSMBU_NSLU_EU").issue_label(), "NSMBU + NSLU (Europe)")
        self.assertIsNone(builds.by_title("0005000010143500"))

    def test_titles_and_hashes_are_distinct(self):
        all_builds = builds.all_builds()
        self.assertEqual(len({(b.title_id, b.title_version) for b in all_builds}), len(all_builds))
        self.assertEqual(len({b.sha256 for b in all_builds}), len(all_builds))
        for b in all_builds:
            self.assertRegex(b.sha256, r"^[0-9a-f]{64}$")
            self.assertRegex(b.title_id, r"^00050000[0-9a-f]{8}$")

class Identify(unittest.TestCase):
    def test_a_file_that_is_no_build(self):
        with open(__file__, "rb"):
            self.assertIsNone(builds.identify(__file__))

    def test_by_sha256(self):
        usa = builds.canonical_build()
        self.assertEqual(builds.by_sha256(usa.sha256).name, "USA")
        self.assertIsNone(builds.by_sha256("0" * 64))

class Hooks(unittest.TestCase):
    def test_every_file_is_read_for_the_canonical_build(self):
        entries, skipped = builds.read_hooks(builds.hook_files(), builds.canonical_build())
        self.assertEqual(skipped, [("hooks_base_hud.txt", "USA_V0 EU_V0")])
        self.assertEqual(
            [(site, addr) for site, _, addr, *_ in entries],
            [(False, 0x02ACD218), (False, 0x024D752C), (True, 0x022A79C4), (False, 0x022A7760),
             (False, 0x029A6170), (False, 0x029B85C8), (False, 0x02A06818), (False, 0x02A061AC),
             (False, 0x02A062EC), (False, 0x02994164), (False, 0x029CBBB4), (False, 0x024BD86C),
             (True, 0x024B8324), (False, 0x0226FBC4), (True, 0x0226FD14), (True, 0x0226FECC),
             (True, 0x02270310), (True, 0x022703A4), (True, 0x0227CCF4), (True, 0x0227E2A8),
             (False, 0x0252F900), (False, 0x025CF020), (False, 0x029C52CC), (False, 0x024FBF6C),
             (False, 0x029C4FE4), (False, 0x024F30DC), (False, 0x029D9E30), (True, 0x029D94C0),
             (True, 0x029D97B8), (True, 0x029D9CB8), (True, 0x029D9E2C), (False, 0x029D8270),
             (False, 0x029D8304), (False, 0x029D82D4), (False, 0x029D8394), (False, 0x029872BC),
             (False, 0x029D8478), (False, 0x029D7648), (False, 0x029D8C10), (True, 0x029D8C48),
             (True, 0x029D71A4), (True, 0x02631BE4), (True, 0x0263ADD8), (True, 0x02641A4C),
             (False, 0x02ABC054), (False, 0x02ABCAE8), (True, 0x02ABCB34), (True, 0x02ABCB38)],
        )
        same_addrs = {0x024D752C, 0x029CBBB4, 0x024BD86C, 0x029872BC}
        for site, canon, addr, where, same in entries:
            self.assertEqual(canon, addr, where)
            self.assertEqual(same, canon in same_addrs, where)

    def test_build_directive_skips_a_file(self):
        with tempfile.TemporaryDirectory() as directory:
            path = os.path.join(directory, "hooks_language.txt")
            with open(path, "w", encoding="utf-8") as source:
                source.write("# builds: USA\n022A79C4\n")
            other = builds.Build({
                "name": "EU",
                "title_id": "0005000010101e00",
                "rpx_sha256": "0" * 64,
                "code_bounds": ["02000020", "028F87F4"],
                "data_bounds": ["10000000", "104DA1C8"],
            })
            entries, skipped = builds.read_hooks([path], other)
        self.assertEqual(skipped, [("hooks_language.txt", "USA")])
        self.assertEqual(entries, [])

    def test_europe_hooks_map(self):
        eu = builds.by_name("EU")
        self.assertIsNotNone(eu)
        entries, skipped = builds.read_hooks(builds.hook_files(), eu)
        self.assertEqual(skipped, [("hooks_base_hud.txt", "USA_V0 EU_V0")])
        by_canon = {canon: addr for _, canon, addr, *_ in entries}
        self.assertEqual(by_canon[0x024D752C], 0x024D752C)
        self.assertEqual(by_canon[0x02ACD218], 0x02ACD618)
        self.assertEqual(by_canon[0x029CBBB4], 0x029CBC58)
        self.assertTrue(eu.body_differs(0x029CBBB4))
        same = [canon for _, canon, _, _, checked in entries if checked]
        self.assertEqual(same, [0x024D752C, 0x029CBBB4, 0x024BD86C, 0x029872BC])

    def test_same_is_a_checked_wrap(self):
        self.assertEqual(builds.differing_hook(False, True, True, True), "keep")
        self.assertEqual(builds.differing_hook(False, False, True, True), "warn")
        self.assertEqual(builds.differing_hook(True, False, True, True), "refuse")
        self.assertEqual(builds.differing_hook(True, False, False, False), "skip")
        self.assertEqual(builds.differing_hook(False, True, False, True), "refuse")
        with tempfile.TemporaryDirectory() as directory:
            path = os.path.join(directory, "hooks.txt")
            with open(path, "w", encoding="utf-8") as source:
                source.write("029CBBB4 same\n@022A79C4 same\n")
            with self.assertRaises(ValueError):
                builds.read_hooks([path], builds.by_name("EU"))

    def test_differing_functions_are_sorted_for_body_differs(self):
        for name in ("USA_V0", "EU_V0", "NSMBU_NSLU_USA", "NSMBU_NSLU_EU"):
            build = builds.by_name(name)
            starts = [a for a, _, _, _ in build.differing]
            self.assertEqual(starts, sorted(starts), name)
        usa_v0 = builds.by_name("USA_V0")
        self.assertTrue(usa_v0.body_differs(0x0252F900))
        self.assertTrue(usa_v0.body_differs(0x029D7648))
        self.assertTrue(usa_v0.body_differs(0x029CBBB4))

    def test_combo_hooks_map(self):
        combo = builds.by_name("NSMBU_NSLU_USA")
        self.assertIsNotNone(combo)
        entries, skipped = builds.read_hooks(builds.hook_files(), combo)
        self.assertEqual(skipped, [("hooks_base_hud.txt", "USA_V0 EU_V0")])
        by_canon = {canon: addr for _, canon, addr, *_ in entries}
        self.assertNotEqual(by_canon[0x02ACD218], 0x02ACD218)
        self.assertEqual(by_canon[0x029CBBB4], combo.code(0x029CBBB4))
        self.assertTrue(combo.body_differs(0x029CBBB4))

    def test_europe_combo_hooks_map(self):
        combo = builds.by_name("NSMBU_NSLU_EU")
        self.assertIsNotNone(combo)
        entries, skipped = builds.read_hooks(builds.hook_files(), combo)
        self.assertEqual(skipped, [("hooks_base_hud.txt", "USA_V0 EU_V0")])
        by_canon = {canon: addr for _, canon, addr, *_ in entries}
        self.assertNotEqual(by_canon[0x02ACD218], 0x02ACD218)
        self.assertEqual(by_canon[0x029CBBB4], combo.code(0x029CBBB4))
        self.assertTrue(combo.body_differs(0x029CBBB4))

    def test_base_disc_stock_hud_hook(self):
        for name in ("USA_V0", "EU_V0"):
            build = builds.by_name(name)
            row = {a: (s1, other, s2) for a, s1, other, s2 in build.differing}
            usa_size, _other, base_size = row[0x0252F900]
            self.assertEqual(usa_size, 400)
            self.assertEqual(base_size, 0)
            draw_size_usa, draw_at, draw_size = row[0x0252D9C0]
            self.assertEqual(draw_size_usa, 732)
            self.assertEqual(draw_size, 400)
            missing_at = build.code(0x0252F900)
            self.assertNotEqual(missing_at, draw_at)
            self.assertTrue(draw_at <= missing_at < draw_at + draw_size)
            entries, skipped = builds.read_hooks(builds.hook_files(), build)
            self.assertEqual(skipped, [])
            by_canon = {canon: addr for _, canon, addr, *_ in entries}
            self.assertEqual(by_canon[0x0252D9C0], draw_at)
            self.assertEqual(by_canon[0x0252F900], missing_at)
            self.assertEqual(builds.differing_hook(False, False, True, build.mods), "warn")

    def test_hook_files_in_folder_with_brackets(self):
        with tempfile.TemporaryDirectory() as temporary:
            d = os.path.join(temporary, "tools [recomp]")
            os.makedirs(d)
            open(os.path.join(d, "hooks.txt"), "w").close()
            with open(os.path.join(d, "hooks_perf.txt"), "w", encoding="utf-8") as source:
                source.write("029C52CC\n")
            paths = builds.hook_files(d)
        self.assertEqual(paths[0], os.path.join(d, "hooks.txt"))
        self.assertEqual([os.path.basename(p) for p in paths[1:]], ["hooks_perf.txt"])

if __name__ == "__main__":
    unittest.main(verbosity=2)

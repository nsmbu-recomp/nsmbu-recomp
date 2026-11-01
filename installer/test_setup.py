#!/usr/bin/env python3
"""Unit tests for the installer's helpers (no game files, no network): python3 test_setup.py"""
import hashlib
import json
import os
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import setup

KEY_HEX = "0011223344556677"   "8899aabbccddeeff"
OTHER_HEX = "ffeeddccbbaa99887766554433221100"
TITLE_ID = "0005000010101d00"

class Keys(unittest.TestCase):
    def test_raw_and_hex(self):
        raw = bytes(range(16))
        self.assertEqual(setup.parse_key(raw), raw)
        self.assertEqual(setup.parse_key(KEY_HEX), bytes.fromhex(KEY_HEX))
        self.assertEqual(setup.parse_key(KEY_HEX.upper() + "\r\n"), bytes.fromhex(KEY_HEX))
        self.assertEqual(setup.parse_key("0x" + KEY_HEX), bytes.fromhex(KEY_HEX))
        self.assertEqual(setup.parse_key(" ".join(KEY_HEX[i:i + 8] for i in range(0, 32, 8))), bytes.fromhex(KEY_HEX))
        self.assertEqual(setup.parse_key(KEY_HEX.encode()), bytes.fromhex(KEY_HEX))
        self.assertEqual(setup.parse_key("\ufeff" + KEY_HEX), bytes.fromhex(KEY_HEX))
        self.assertEqual(setup.parse_key(b"\xef\xbb\xbf" + KEY_HEX.encode()), bytes.fromhex(KEY_HEX))

    def test_issue_11_windows_common_key_paste(self):
        want = bytes.fromhex(KEY_HEX)
        for pasted in (
            KEY_HEX,
            KEY_HEX.upper(),
            KEY_HEX + "\r\n",
            KEY_HEX.upper() + "\r",
            "0x" + KEY_HEX,
            " ".join(KEY_HEX[i:i + 2] for i in range(0, 32, 2)),
            "-".join(KEY_HEX[i:i + 8] for i in range(0, 32, 8)),
            "\ufeff" + KEY_HEX + "\r\n",
            b"\xef\xbb\xbf" + KEY_HEX.encode() + b"\r\n",
        ):
            self.assertEqual(setup.parse_key(pasted), want, repr(pasted))
        with tempfile.TemporaryDirectory() as d:
            notepad = os.path.join(d, "common.key")
            with open(notepad, "wb") as f:
                f.write(b"\xff\xfe" + (KEY_HEX + "\r\n").encode("utf-16-le"))
            self.assertEqual(setup.read_key_file(notepad, "common"), want)

    def test_malformed(self):
        for bad in ("", "1234", KEY_HEX[:-1], KEY_HEX + "0", "zz" * 16, b"\xff" * 15, b"\xff" * 17):
            self.assertIsNone(setup.parse_key(bad), bad)

    def test_key_file(self):
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, "k.key")
            for kind in ("disc", "common"):
                with open(p, "wb") as f:
                    f.write(bytes(range(16)))
                self.assertEqual(setup.read_key_file(p, kind), bytes(range(16)), kind)
                with open(p, "w") as f:
                    f.write(KEY_HEX + "\n")
                self.assertEqual(setup.read_key_file(p, kind), bytes.fromhex(KEY_HEX), kind)
            with open(p, "wb") as f:
                f.write(b"x" * 5000)
            self.assertIsNone(setup.read_key_file(p, "disc"))
            self.assertIsNone(setup.read_key_file(p, "common"))
            self.assertIsNone(setup.read_key_file(os.path.join(d, "missing"), "disc"))
            self.assertIsNone(setup.read_key_file(os.path.join(d, "missing"), "common"))

    def test_stdin_blob(self):
        k = setup.Keys()
        k.disc, k.common = bytes(16), bytes.fromhex(KEY_HEX)
        self.assertEqual(k.stdin_blob(), ("disc %s\ncommon %s\n" % ("00" * 16, KEY_HEX)).encode())

class KeyFiles(unittest.TestCase):
    def write(self, d, data, name="keys.txt"):
        p = os.path.join(d, name)
        with open(p, "wb") as f:
            f.write(data)
        return p

    def test_cemu_keys_txt(self):
        text = ("# Cemu keys\n"
                "common = %s\n"
                "disc = %s\n"
                "%s = %s\n" % (KEY_HEX, OTHER_HEX, TITLE_ID, OTHER_HEX))
        with tempfile.TemporaryDirectory() as d:
            p = self.write(d, text.encode())
            self.assertEqual(setup.read_key_file(p, "common"), bytes.fromhex(KEY_HEX))
            self.assertEqual(setup.read_key_file(p, "disc"), bytes.fromhex(OTHER_HEX))
            self.assertEqual(setup.keys_in_text(text)["title"], [(TITLE_ID, bytes.fromhex(OTHER_HEX))])
            self.assertIsNone(setup.key_file_problem(p, "common"))
            self.assertIsNone(setup.key_file_problem(p, "disc"))

    def test_encodings(self):
        text = "common = %s\r\ndisc = %s\r\n" % (KEY_HEX, OTHER_HEX)
        blobs = {
            "utf-8-sig": text.encode("utf-8-sig"),
            "utf-8-bom": b"\xef\xbb\xbf" + text.encode("utf-8"),
            "utf-16-le-bom": b"\xff\xfe" + text.encode("utf-16-le"),
            "utf-16-be-bom": b"\xfe\xff" + text.encode("utf-16-be"),
        }
        with tempfile.TemporaryDirectory() as d:
            for name, data in blobs.items():
                p = self.write(d, data, name + ".txt")
                self.assertEqual(setup.read_key_file(p, "common"), bytes.fromhex(KEY_HEX), name)
                self.assertEqual(setup.read_key_file(p, "disc"), bytes.fromhex(OTHER_HEX), name)

    def test_bare_crlf(self):
        with tempfile.TemporaryDirectory() as d:
            p = self.write(d, (KEY_HEX.upper() + "\r\n").encode())
            self.assertEqual(setup.read_key_file(p, "disc"), bytes.fromhex(KEY_HEX))
            self.assertEqual(setup.read_key_file(p, "common"), bytes.fromhex(KEY_HEX))

    def test_named_line_wins(self):
        text = "disc = %s\n%s\n" % (OTHER_HEX, KEY_HEX)
        with tempfile.TemporaryDirectory() as d:
            p = self.write(d, text.encode())
            self.assertEqual(setup.read_key_file(p, "disc"), bytes.fromhex(OTHER_HEX))
            self.assertEqual(setup.read_key_file(p, "common"), bytes.fromhex(KEY_HEX))

    def test_quoted_and_comments(self):
        text = '// keys\n\n"common" : "%s"\n' % KEY_HEX
        with tempfile.TemporaryDirectory() as d:
            p = self.write(d, text.encode())
            self.assertEqual(setup.read_key_file(p, "common"), bytes.fromhex(KEY_HEX))

    def test_title_only(self):
        text = "%s = %s\n" % (TITLE_ID, OTHER_HEX)
        with tempfile.TemporaryDirectory() as d:
            p = self.write(d, text.encode())
            self.assertIsNone(setup.read_key_file(p, "common"))
            self.assertIsNone(setup.read_key_file(p, "disc"))
            self.assertIn(setup.format_title_id(TITLE_ID), setup.key_file_problem(p, "common"))
            self.assertIn("not the Wii U common key", setup.key_file_problem(p, "common"))
            self.assertIn("not the disc key", setup.key_file_problem(p, "disc"))

    def test_no_key(self):
        with tempfile.TemporaryDirectory() as d:
            p = self.write(d, b"# nothing here\n")
            for kind in ("common", "disc"):
                self.assertIsNone(setup.read_key_file(p, kind))
                self.assertIn("keys.txt", setup.key_file_problem(p, kind))
                self.assertIn("16 raw bytes", setup.key_file_problem(p, kind))
            self.assertIn("cannot be read", setup.key_file_problem(os.path.join(d, "missing"), "common"))

    def test_key_bytes_without_a_file(self):
        text = ("common = %s\ndisc = %s\n" % (KEY_HEX, OTHER_HEX)).encode()
        self.assertEqual(setup.key_from_data(text, "disc"), bytes.fromhex(OTHER_HEX))
        self.assertEqual(setup.named_key_from_data(text, "common"), bytes.fromhex(KEY_HEX))
        self.assertEqual(setup.key_from_data(bytes(range(16)), "common"), bytes(range(16)))
        self.assertIsNone(setup.named_key_from_data(bytes(range(16)), "common"))
        self.assertIsNone(setup.key_from_data(b"garbage", "disc"))
        self.assertIn("16 raw bytes", setup.key_data_problem(b"garbage", "disc"))
        self.assertIn("larger than 4096", setup.key_data_problem(b"0" * 4097, "common"))
        self.assertIsNone(setup.key_data_problem(text, "common"))

    def test_discovery(self):
        if any(os.path.isfile(os.path.join(setup.PKG, n)) for n in ("common.key", "keys.txt")):
            self.skipTest("a key file is in the release folder")
        with tempfile.TemporaryDirectory() as d, mock.patch.dict(os.environ, {}, clear=False):
            os.environ.pop("WIIU_COMMON_KEY", None)
            image = os.path.join(d, "game.wux")
            open(image, "wb").close()
            p = self.write(d, ("disc = %s\n%s\n" % (OTHER_HEX, KEY_HEX)).encode())
            self.assertEqual(setup.find_common_key(image), (None, None))
            with open(p, "w") as f:
                f.write("common = %s\n" % KEY_HEX)
            self.assertEqual(setup.find_common_key(image), (p, bytes.fromhex(KEY_HEX)))
            side = os.path.join(d, "game.key")
            with open(side, "w") as f:
                f.write("disc = %s\n" % OTHER_HEX)
            self.assertEqual(setup.find_sidecar_disc_key(image), side)

class Paths(unittest.TestCase):
    def test_clean_path(self):
        self.assertEqual(setup.clean_path('"/tmp/a b/c.wux"'), os.path.abspath("/tmp/a b/c.wux"))
        self.assertEqual(setup.clean_path("'/tmp/x'"), os.path.abspath("/tmp/x"))
        self.assertEqual(setup.clean_path("  "), "")
        if not setup.IS_WIN:
            self.assertEqual(setup.clean_path("/tmp/My\\ Game.wux "), "/tmp/My Game.wux")

    def test_game_folder(self):
        with tempfile.TemporaryDirectory() as d:
            self.assertFalse(setup.valid_game_folder(d))
            os.makedirs(os.path.join(d, "code"))
            os.makedirs(os.path.join(d, "content"))
            os.makedirs(os.path.join(d, "meta"))
            open(os.path.join(d, "code", "red-pro2.rpx"), "wb").close()
            with open(os.path.join(d, "meta", "meta.xml"), "w") as f:
                f.write('<menu><title_id type="hexBinary" length="8">0005000010101d00</title_id></menu>')
            self.assertTrue(setup.valid_game_folder(d))
            self.assertEqual(setup.game_folder_title(d), "0005000010101d00")

class DataDir(unittest.TestCase):
    """default_data_dir: portable.txt next to the release means <release>/data; without it (a source
    build, or an AppImage whose mount is read-only, issue #55) the per-user folder of earlier
    releases. --data-dir overrides both (setup_gui.cpp data_dir_of mirrors this)."""

    def setUp(self):
        self._portable = setup.PORTABLE

    def tearDown(self):
        setup.PORTABLE = self._portable

    def test_portable_uses_the_release_folder(self):
        setup.PORTABLE = True
        self.assertEqual(setup.default_data_dir(), os.path.join(setup.PKG, "data"))

    def test_without_the_marker_uses_the_per_user_folder(self):
        setup.PORTABLE = False
        self.assertEqual(setup.default_data_dir(), setup.legacy_data_dir())

    @unittest.skipUnless(setup.IS_LINUX, "the XDG rule is the Linux one")
    def test_legacy_respects_xdg_data_home(self):
        saved = os.environ.get("XDG_DATA_HOME")
        os.environ["XDG_DATA_HOME"] = "/tmp/nsmbu-xdg"
        try:
            self.assertEqual(setup.legacy_data_dir(), os.path.join("/tmp/nsmbu-xdg", "nsmbu"))
        finally:
            if saved is None:
                os.environ.pop("XDG_DATA_HOME", None)
            else:
                os.environ["XDG_DATA_HOME"] = saved

    def test_legacy_folder_name(self):

        self.assertEqual(os.path.basename(setup.legacy_data_dir()), "NSMBU" if setup.IS_WIN else "nsmbu")

class Titles(unittest.TestCase):
    def test_supported_ok(self):
        setup.check_title("0005000010101d00")
        setup.check_title("0005000010101e00")
        setup.check_title("000500001014b700")
        setup.check_title("000500001014b800")

    def test_unsupported(self):
        with self.assertRaisesRegex(setup.SetupError, "Japan.*can be built from"):
            setup.check_title("0005000010101c00")
        with self.assertRaisesRegex(setup.SetupError, "not New Super Mario Bros. U"):
            setup.check_title("000500001010ec00")

def _title(tid, version, files=10, size=1000):
    return {"id": tid, "version": version, "folder": "%s_v%d" % (tid, version), "files": files, "bytes": size}

class ArchiveTitles(unittest.TestCase):
    """Which title of a Cemu archive (.wua) is used (info as nsmbu-extract --title 0005000010101d00 info prints it)."""

    BASE, UPDATE = _title("0005000010101d00", 64), _title("0005000e10101d00", 64)

    def info(self, titles, selected=None):
        i = {"format": "wua", "titles": titles}
        if selected:
            i.update(selected=selected["folder"], title_id=selected["id"], version=str(selected["version"]),
                     files=str(selected["files"]), bytes=str(selected["bytes"]))
        return i

    def test_base_only(self):
        self.assertEqual(setup.archive_choice(self.info([self.BASE], self.BASE)), ("0005000010101d00_v64", []))

    def test_update_also_present(self):
        folder, notes = setup.archive_choice(self.info([self.UPDATE, self.BASE, _title("0005000c10101d00", 3)], self.BASE))
        self.assertEqual(folder, "0005000010101d00_v64")
        self.assertEqual(len(notes), 2)
        self.assertIn("Also present: the update for New Super Mario Bros. U (USA), version 64", notes[0])
        self.assertIn("downloadable content", notes[1])

    def test_update_without_game(self):
        with self.assertRaisesRegex(setup.SetupError, "1.3.0 update but not the base game"):
            setup.archive_choice(self.info([self.UPDATE]))

    def test_european_archive(self):
        eu, eu_update = _title("0005000010101e00", 80), _title("0005000e10101e00", 80)
        folder, notes = setup.archive_choice(self.info([eu, eu_update], eu))
        self.assertEqual(folder, "0005000010101e00_v80")
        self.assertTrue(any("Also present: the update for New Super Mario Bros. U (Europe), version 80" in n
                            for n in notes))

    def test_european_wrong_version(self):
        eu = _title("0005000010101e00", 64)
        with self.assertRaisesRegex(setup.SetupError, "version 64.*title version 0 or 80"):
            setup.archive_choice(self.info([eu], eu))

    def test_european_base_disc(self):
        eu = _title("0005000010101e00", 0)
        folder, notes = setup.archive_choice(self.info([eu], eu))
        self.assertEqual(folder, "0005000010101e00_v0")
        self.assertEqual(notes, [])

    def test_other_region(self):
        with self.assertRaisesRegex(setup.SetupError, "archive contains the Japan version"):
            setup.archive_choice(self.info([_title("0005000010101c00", 64), _title("0005000e10101c00", 64)]))

    def test_combo_pack(self):
        combo = _title("000500001014b700", 0)
        folder, notes = setup.archive_choice(self.info([combo], combo))
        self.assertEqual(folder, "000500001014b700_v0")
        self.assertEqual(notes, [])

    def test_europe_combo_pack(self):
        combo = _title("000500001014b800", 0)
        folder, notes = setup.archive_choice(self.info([combo], combo))
        self.assertEqual(folder, "000500001014b800_v0")
        self.assertEqual(notes, [])

    def test_other_game(self):
        with self.assertRaisesRegex(setup.SetupError, "does not contain New Super Mario Bros. U.*title 00050000-1010EC00"):
            setup.archive_choice(self.info([_title("000500001010ec00", 0)]))
        with self.assertRaisesRegex(setup.SetupError, "no Wii U titles"):
            setup.archive_choice(self.info([]))

    def test_other_version(self):
        v2 = _title("0005000010101d00", 2)
        with self.assertRaisesRegex(setup.SetupError, "version 2.*title version 0 or 64"):
            setup.archive_choice(self.info([v2], v2))

    def test_title_desc(self):
        self.assertEqual(setup.title_desc("0005000010101d00", 64), "New Super Mario Bros. U (USA), version 64")
        self.assertEqual(setup.title_desc("0005000e10101c00"), "the update for New Super Mario Bros. U (Japan)")

    def test_format_title_id(self):
        self.assertEqual(setup.format_title_id("0005000010101d00"), "00050000-10101D00")
        self.assertEqual(setup.format_title_id(""), "")

    def test_plan(self):
        self.assertEqual(setup.plan_steps("archive"),
                         ["archive", "compiler", "extract", "translate", "compile", "app", "shaders"])
        self.assertEqual(setup.plan_steps("installed"),
                         ["compiler", "translate", "compile", "app", "shaders"])
        self.assertEqual(setup.plan_steps("gen"), ["compiler", "compile", "app"])
        self.assertEqual(setup.EXTRACT_ERRORS[10], "wrong_title")

class GameVersion(unittest.TestCase):
    """code/red-pro2.rpx must be one of the builds the port knows (version 64 of a region,
    tools/recomp/builds.py); synthetic files, made-up bytes."""

    def make(self, d, rpx=b"made-up rpx", app_tid="0005000010101d00", app_ver="0040"):
        os.makedirs(os.path.join(d, "code"), exist_ok=True)
        with open(os.path.join(d, "code", "red-pro2.rpx"), "wb") as f:
            f.write(rpx)
        with open(os.path.join(d, "code", "app.xml"), "w") as f:
            f.write('<app><title_id type="hexBinary" length="8">%s</title_id>\n'
                    '<title_version type="hexBinary" length="2">%s</title_version></app>' % (app_tid, app_ver))

    def setUp(self):
        self.saved = (setup.SUPPORTED_BUILDS, setup.game_builds.by_sha256)
        fake = [setup.game_builds.Build({"name": "USA", "title_id": "0005000010101d00",
                                         "title_version": 64,
                                         "rpx_sha256": hashlib.sha256(b"made-up rpx").hexdigest()}),
                setup.game_builds.Build({"name": "EU", "title_id": "0005000010101e00",
                                         "title_version": 80,
                                         "code_bounds": ["02000000", "03000000"],
                                         "data_bounds": ["10000000", "10500000"],
                                         "rpx_sha256": hashlib.sha256(b"made-up eu rpx").hexdigest()})]
        setup.SUPPORTED_BUILDS = {b.title_id: b for b in fake}
        setup.game_builds.by_sha256 = lambda d: next((b for b in fake if b.sha256 == d), None)

    def tearDown(self):
        setup.SUPPORTED_BUILDS, setup.game_builds.by_sha256 = self.saved

    def test_expected_file(self):
        with tempfile.TemporaryDirectory() as d:
            self.make(d)
            self.assertEqual(setup.check_game_version(d).name, "USA")

    def test_other_build(self):
        with tempfile.TemporaryDirectory() as d:
            self.make(d, rpx=b"made-up eu rpx", app_tid="0005000010101e00", app_ver="0050")
            self.assertEqual(setup.check_game_version(d).name, "EU")

    def test_wrong_version_or_update_without_known_rpx(self):
        with tempfile.TemporaryDirectory() as d:
            self.make(d, rpx=b"other code", app_ver="0010")
            with self.assertRaisesRegex(setup.SetupError, "need title version 0 or 64"):
                setup.check_game_version(d)
            self.make(d, rpx=b"other code", app_tid="0005000e10101d00", app_ver="0040")
            with self.assertRaisesRegex(setup.SetupError, "not a file the port knows"):
                setup.check_game_version(d)

    def test_unknown_build(self):
        with tempfile.TemporaryDirectory() as d:
            self.make(d, rpx=b"damaged")
            with self.assertRaisesRegex(setup.SetupError, "not a file the port knows \\(SHA-256 [0-9a-f]{16}\\.\\.\\.\\)"):
                setup.check_game_version(d)
            os.remove(os.path.join(d, "code", "app.xml"))
            with self.assertRaisesRegex(setup.SetupError, "not a file the port knows"):
                setup.check_game_version(d)

    def test_unsupported_region(self):
        with tempfile.TemporaryDirectory() as d:
            self.make(d, rpx=b"jp", app_tid="0005000010101c00")
            with self.assertRaisesRegex(setup.SetupError, "New Super Mario Bros. U \\(Japan\\)"):
                setup.check_game_version(d)

    def test_missing(self):
        with tempfile.TemporaryDirectory() as d:
            with self.assertRaisesRegex(setup.SetupError, "cannot read"):
                setup.check_game_version(d)

    @unittest.skipUnless(os.environ.get("NSMBU_GAME_DIR"), "NSMBU_GAME_DIR (your own extracted game) not set")
    def test_real_game(self):
        setup.SUPPORTED_BUILDS, setup.game_builds.by_sha256 = self.saved
        build = setup.check_game_version(os.environ["NSMBU_GAME_DIR"])
        self.assertIn(build.title_id, setup.SUPPORTED_BUILDS)

class UpdateFolder(unittest.TestCase):
    """Disc images may use an optional matching 1.3.0 update/ folder (checked then applied after extract)."""

    def make_folder(self, d, rpx=b"made-up rpx", app_tid="0005000e10101d00", app_ver="0040"):
        os.makedirs(os.path.join(d, "code"), exist_ok=True)
        os.makedirs(os.path.join(d, "content"), exist_ok=True)
        os.makedirs(os.path.join(d, "meta"), exist_ok=True)
        with open(os.path.join(d, "code", "red-pro2.rpx"), "wb") as f:
            f.write(rpx)
        with open(os.path.join(d, "code", "app.xml"), "w") as f:
            f.write('<app><title_id type="hexBinary" length="8">%s</title_id>\n'
                    '<title_version type="hexBinary" length="2">%s</title_version></app>' % (app_tid, app_ver))
        with open(os.path.join(d, "meta", "meta.xml"), "w") as f:
            f.write('<menu><title_id type="hexBinary" length="8">%s</title_id></menu>' % app_tid)

    def setUp(self):
        self.saved = (setup.SUPPORTED_BUILDS, setup.game_builds.by_sha256)
        fake = [
            setup.game_builds.Build({"name": "USA", "title_id": "0005000010101d00", "title_version": 64,
                                     "rpx_sha256": hashlib.sha256(b"made-up rpx").hexdigest()}),
            setup.game_builds.Build({"name": "EU", "title_id": "0005000010101e00", "title_version": 80,
                                     "code_bounds": ["02000000", "03000000"],
                                     "data_bounds": ["10000000", "10500000"],
                                     "rpx_sha256": hashlib.sha256(b"made-up eu rpx").hexdigest()}),
        ]
        setup.SUPPORTED_BUILDS = {b.title_id: b for b in fake}
        setup.game_builds.by_sha256 = lambda d: next((b for b in fake if b.sha256 == d), None)

    def tearDown(self):
        setup.SUPPORTED_BUILDS, setup.game_builds.by_sha256 = self.saved

    def test_check_and_apply(self):
        with tempfile.TemporaryDirectory() as d:
            upd = os.path.join(d, "update")
            base = os.path.join(d, "base")
            self.make_folder(upd)
            self.make_folder(base, rpx=b"base disc", app_tid="0005000010101d00", app_ver="0000")
            self.assertEqual(setup.check_update_folder(upd).name, "USA")
            setup.apply_update_to(base, upd)
            self.assertEqual(setup.check_game_version(base).name, "USA")
            with open(os.path.join(base, "code", "red-pro2.rpx"), "rb") as f:
                self.assertEqual(f.read(), b"made-up rpx")

    def test_sidecar_next_to_image(self):
        with tempfile.TemporaryDirectory() as d:
            image = os.path.join(d, "game.wux")
            open(image, "wb").close()
            self.assertIsNone(setup.find_sidecar_update(image))
            self.make_folder(os.path.join(d, "update"))
            self.assertEqual(setup.find_sidecar_update(image), os.path.join(d, "update"))

    def test_wrong_version(self):
        with tempfile.TemporaryDirectory() as d:
            self.make_folder(d, app_ver="0010")
            with self.assertRaisesRegex(setup.SetupError, "title version 16; need 64"):
                setup.check_update_folder(d)

    def test_europe_update(self):
        with tempfile.TemporaryDirectory() as d:
            self.make_folder(d, rpx=b"made-up eu rpx", app_tid="0005000e10101e00", app_ver="0050")
            self.assertEqual(setup.check_update_folder(d).name, "EU")

    def test_missing_layout(self):
        with tempfile.TemporaryDirectory() as d:
            with self.assertRaisesRegex(setup.SetupError, "not an update folder"):
                setup.check_update_folder(d)

class PlayableGameFolder(unittest.TestCase):
    """The run/install game root must be the full game, not an update-only dump."""

    def make(self, d, tid="0005000010101d00", with_course=True):
        os.makedirs(os.path.join(d, "code"), exist_ok=True)
        os.makedirs(os.path.join(d, "content"), exist_ok=True)
        os.makedirs(os.path.join(d, "meta"), exist_ok=True)
        open(os.path.join(d, "code", "red-pro2.rpx"), "wb").close()
        with open(os.path.join(d, "meta", "meta.xml"), "w") as f:
            f.write('<menu><title_id type="hexBinary" length="8">%s</title_id></menu>' % tid)
        if with_course:
            course = os.path.join(d, "content", "Common", "course_res_pack")
            os.makedirs(course, exist_ok=True)
            open(os.path.join(course, "1-1.szs"), "wb").close()

    def test_update_title_rejected(self):
        with tempfile.TemporaryDirectory() as d:
            self.make(d, tid="0005000e10101d00", with_course=False)
            with self.assertRaisesRegex(setup.SetupError, "only the update for New Super Mario Bros. U \\(USA\\)"):
                setup.require_playable_game_folder(d)

    def test_missing_course_rejected(self):
        with tempfile.TemporaryDirectory() as d:
            self.make(d, with_course=False)
            with self.assertRaisesRegex(setup.SetupError, "1-1\\.szs"):
                setup.require_playable_game_folder(d)

    def test_full_folder_ok(self):
        with tempfile.TemporaryDirectory() as d:
            self.make(d)
            setup.require_playable_game_folder(d)

    def test_course_path_case_insensitive(self):
        with tempfile.TemporaryDirectory() as d:
            self.make(d, with_course=False)
            course = os.path.join(d, "content", "common", "COURSE_RES_PACK")
            os.makedirs(course, exist_ok=True)
            open(os.path.join(course, "1-1.SZS"), "wb").close()
            setup.require_playable_game_folder(d)

    def test_check_title_update_message(self):
        with self.assertRaisesRegex(setup.SetupError, "only the update for.*not the full game"):
            setup.check_title("0005000e10101d00")

class Recipe(unittest.TestCase):
    def test_substitution(self):
        m = {"sdk": "/p/sdk", "gamecode": "/w/libgamecode.a", "out": "/d/bin/nsmbu"}
        self.assertEqual(setup.sub("{sdk}/obj/a.o", m), "/p/sdk/obj/a.o")
        self.assertEqual(setup.sub("-I{sdk}/include", m), "-I/p/sdk/include")
        self.assertEqual(setup.sub("{gamecode}", m), "/w/libgamecode.a")
        self.assertEqual(setup.sub("-O3", m), "-O3")
        self.assertEqual(setup.fwd("C:\\a\\b"), "C:/a/b")

    def test_link_recipe_and_glue(self):
        tc = setup.Toolchain(["cc"], ["c++"], ["ar"], rsp=False)
        calls = []
        with tempfile.TemporaryDirectory() as d, \
             mock.patch.object(setup, "run_logged", side_effect=lambda cmd, **k: calls.append(cmd)), \
             mock.patch.object(setup, "IS_LINUX", False):
            out = os.path.join(d, "libgame.so")
            open(out, "wb").close()
            setup.link_game(tc, {"link": ["unused"]}, ["a.o"], d, out,
                            recipe=["-shared", "{glue}", "{gamecode}", "-o", "{out}"], glue=["x.o", "y.o"])
            self.assertEqual(calls[-1], ["c++", "-shared", "x.o", "y.o", setup.fwd(os.path.join(d, "libgamecode.a")),
                                         "-o", setup.fwd(out)])
            setup.link_game(tc, {"link": ["{gamecode}", "-o", "{out}"]}, ["a.o"], d, out)
            self.assertEqual(calls[-1][1:], [setup.fwd(os.path.join(d, "libgamecode.a")), "-o", setup.fwd(out)])

    def test_toolchains_pinned(self):
        tcs = setup.load_toolchains()
        for name, tc in tcs["toolchains"].items():
            if tc["kind"] in ("xcode-clt", "host-clang"):
                continue
            self.assertRegex(tc["sha256"], r"^[0-9a-f]{64}$", name)
            self.assertTrue(tc["url"].startswith("https://"), name)
        for name, py in tcs["python"].items():
            self.assertRegex(py["sha256"], r"^[0-9a-f]{64}$", name)

    def test_jobs(self):
        self.assertGreaterEqual(setup.default_jobs(), 1)

class Arch(unittest.TestCase):
    def test_normalize(self):
        for a, want in (("x86_64", "x86_64"), ("AMD64", "x86_64"), ("aarch64", "aarch64"), ("arm64", "aarch64")):
            self.assertEqual(setup.normalize_arch(a), want)
        self.assertIn(setup.host_arch(), ("x86_64", "aarch64"))

    def test_linux_pins_per_arch(self):
        tcs = setup.load_toolchains()
        self.assertEqual(tcs["toolchains"]["zig-0.16.0"]["target"].split("-")[0], "x86_64")
        self.assertEqual(tcs["toolchains"]["zig-0.16.0-aarch64"]["target"].split("-")[0], "aarch64")
        self.assertIn("aarch64", tcs["toolchains"]["zig-0.16.0-aarch64"]["url"])
        self.assertIn("aarch64", tcs["python"]["linux-aarch64"]["url"])

class NoScriptHost(unittest.TestCase):
    """Antivirus heuristics read "unsigned program starts PowerShell" as a dropper (issue #58): the Windows setup
    uses the Windows API instead (the release ships Python, setup.py uses ctypes)."""

    def test_no_powershell(self):
        here = os.path.dirname(os.path.abspath(__file__))
        for name in ("setup.py", "install-windows.bat"):
            with open(os.path.join(here, name), encoding="utf-8") as f:
                self.assertNotIn("powershell", f.read().lower(), name)
        self.assertFalse(os.path.exists(os.path.join(here, "bootstrap-windows.ps1")))

    @unittest.skipUnless(setup.IS_WIN, "Windows only")
    def test_shortcut(self):
        self.assertTrue(os.path.isdir(setup.win_known_folder(0x02)))
        self.assertTrue(os.path.isdir(setup.win_known_folder(0x10)))
        with tempfile.TemporaryDirectory() as d:
            link = os.path.join(d, "NSMBU test.lnk")
            setup.win_shortcut(link, sys.executable, "--game game --save save", d, sys.executable)
            with open(link, "rb") as f:
                head = f.read(20)
            self.assertEqual(head[:4], b"\x4c\x00\x00\x00")
            self.assertEqual(head[4:20], bytes.fromhex("0114020000000000c000000000000046"))
            setup.win_shortcut(link, sys.executable, workdir=d)

class BundledPython(unittest.TestCase):
    """The Windows release ships the pinned embeddable Python in tools/python; guard.py allows exactly its files."""

    def setUp(self):
        sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tools", "release"))
        import guard
        self.guard = guard
        with open(guard.PYTHON_FILES) as f:
            self.expected = json.load(f)

    def test_manifest_matches_pin(self):
        pin = setup.load_toolchains()["python"]["windows"]
        self.assertEqual(self.expected["sha256"], pin["sha256"])
        self.assertEqual(self.expected["url"], pin["url"])
        for name in ("python.exe", "pythonw.exe", "python3.dll", "LICENSE.txt"):
            self.assertIn(name, self.expected["files"])

    def test_guard_rejects_other_files(self):
        with tempfile.TemporaryDirectory() as d:
            py = os.path.join(d, "NSMBU-x", "tools", "python")
            os.makedirs(py)
            with open(os.path.join(py, "python.exe"), "wb") as f:
                f.write(b"MZ not the real one")
            with open(os.path.join(py, "dropper.exe"), "wb") as f:
                f.write(b"MZ")
            problems, _ = self.guard.scan(d)
            text = "\n".join(problems)
            self.assertIn("python.exe: differs", text)
            self.assertIn("dropper.exe: not a file", text)
            self.assertIn("lacks files", text)

    def test_windows_finds_bundled_python(self):

        with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "gui", "console_setup_win.cpp")) as f:
            self.assertIn('"tools\\\\python\\\\python.exe"', f.read())

class ShaderWarm(unittest.TestCase):
    def test_progress_line(self):
        self.assertIsNone(setup.parse_warm_progress("boot"))
        self.assertIsNone(setup.parse_warm_progress("[warm] done"))
        self.assertEqual(setup.parse_warm_progress("[warm] progress 12 0 12 shaders compiled\n"),
                         (12, 0, "12 shaders compiled"))
        self.assertEqual(setup.parse_warm_progress("[warm] progress 3 10 pixel"), (3, 10, "pixel"))

    def test_cancel_line(self):
        self.assertTrue(setup.shader_cancel_request('{"cmd":"shaders_cancel","id":0}\n'))
        self.assertFalse(setup.shader_cancel_request('{"cmd":"shaders"}'))
        self.assertFalse(setup.shader_cancel_request("not json"))

    def test_failure_message(self):
        text = setup.shader_warm_failure(1, 4, ["[warm] progress 4 0 4 shaders compiled", "FATAL: no device"])
        self.assertIn("exit code 1", text)
        self.assertIn("FATAL: no device", text)
        self.assertNotIn("[warm] progress", text)
        self.assertIn("Repair", text)
        none = setup.shader_warm_failure(0, 0, [])
        self.assertIn("did not compile any shaders", none)
        self.assertIn("Repair", none)

    def test_precompile_requires_exe(self):
        with tempfile.TemporaryDirectory() as d:
            with self.assertRaisesRegex(setup.SetupError, "game program is missing"):
                setup.precompile_shaders(os.path.join(d, "missing"), d, d)

    def test_precompile_runs_warm(self):
        with tempfile.TemporaryDirectory() as d:
            exe = os.path.join(d, "nsmbu")
            with open(exe, "w") as f:
                f.write("x")
            game = os.path.join(d, "game")
            os.makedirs(os.path.join(game, "code"))
            os.makedirs(os.path.join(game, "content"))
            os.makedirs(os.path.join(game, "meta"))
            with open(os.path.join(game, "code", "red-pro2.rpx"), "wb") as f:
                f.write(b"rpx")
            with mock.patch.object(setup, "valid_game_folder", return_value=True), \
                 mock.patch.object(setup, "run_shader_warm", return_value=(0, 12, False, [])) as warm:
                setup.precompile_shaders(exe, game, d)
            warm.assert_called_once()
            self.assertEqual(warm.call_args[0][0], exe)
            self.assertEqual(warm.call_args[0][1], game)

    def test_precompile_fails_when_warm_fails(self):
        with tempfile.TemporaryDirectory() as d:
            exe = os.path.join(d, "nsmbu")
            with open(exe, "w") as f:
                f.write("x")
            with mock.patch.object(setup, "valid_game_folder", return_value=True), \
                 mock.patch.object(setup, "run_shader_warm", return_value=(1, 0, False, ["FATAL"])):
                with self.assertRaisesRegex(setup.SetupError, "shader precompile"):
                    setup.precompile_shaders(exe, d, d)

class LongPaths(unittest.TestCase):
    """Windows' "Extract All" puts the release folder into a folder of the same name, which can put the
    deepest files of llvm-mingw beyond MAX_PATH. Setup's file operations on the compiler use extended-length
    paths (longpath); a data folder too deep for the game and its tools stops setup before any download."""

    RELEASE = "C:\\Users\\player\\Downloads\\" + "\\".join(["NSMBU-v0.7.0-windows-x86_64"] * 2)

    def test_longpath_windows(self):
        lp = lambda p: setup.longpath(p, windows=True)
        self.assertEqual(lp("C:\\Games\\NSMBU\\data"), "\\\\?\\C:\\Games\\NSMBU\\data")
        self.assertEqual(lp("C:/Games/NSMBU/./x/../data/"), "\\\\?\\C:\\Games\\NSMBU\\data")
        self.assertEqual(lp("\\\\server\\share\\NSMBU"), "\\\\?\\UNC\\server\\share\\NSMBU")
        self.assertEqual(lp("\\\\?\\C:\\already"), "\\\\?\\C:\\already")
        self.assertEqual(lp("\\\\.\\pipe\\x"), "\\\\.\\pipe\\x")
        from pathlib import PureWindowsPath
        self.assertEqual(lp(PureWindowsPath("C:\\a\\b")), "\\\\?\\C:\\a\\b")
        self.assertEqual(lp("D:/a/_temp/msys64/tmp/x/code-builds/bbbb"), "\\\\?\\D:\\a\\_temp\\msys64\\tmp\\x\\code-builds\\bbbb")
        self.assertEqual(lp("D:\\a/b\\\\c//d"), "\\\\?\\D:\\a\\b\\c\\d")
        self.assertEqual(lp("//server/share/NSMBU/../x"), "\\\\?\\UNC\\server\\share\\x")
        for root in ("D:/", "D:\\", "D:", "D:/a/.."):
            self.assertEqual(lp(root), "\\\\?\\D:\\")
        self.assertEqual(lp("C:/../.."), "\\\\?\\C:\\")
        for p in ("C:/x/y", "\\\\server\\share\\y"):
            self.assertNotIn("/", lp(p))
            self.assertEqual(setup.plain_path(lp(p)), setup.windows_path(p))

    def test_windows_path(self):
        self.assertEqual(setup.windows_path("C:/Games//NSMBU/./data/"), "C:\\Games\\NSMBU\\data")
        self.assertEqual(setup.windows_path("relative/./dir/../x"), "relative\\x")
        self.assertEqual(setup.windows_path("//srv/share"), "\\\\srv\\share\\")

    def test_msys2_python_keeps_ordinary_paths(self):
        with mock.patch.object(setup, "NATIVE_WINDOWS_PATHS", False):
            p = "D:/a/_temp/msys64/tmp/x"
            self.assertIs(setup.longpath(p), p)
        self.assertEqual(setup.NATIVE_WINDOWS_PATHS, setup.IS_WIN and os.path.join("a", "b") == "a\\b")

    def test_longpath_other_platforms_unchanged(self):
        for p in ("/home/player/nsmbu", "relative/dir", "C:\\x"):
            self.assertIs(setup.longpath(p, windows=False), p)
        if not setup.IS_WIN:
            p = "/Users/player/nsmbu"
            self.assertIs(setup.longpath(p), p)

    def test_messages_show_normal_paths(self):
        for p in ("C:\\Games\\NSMBU", "\\\\server\\share\\NSMBU"):
            self.assertEqual(setup.plain_path(setup.longpath(p, windows=True)), p)
        err = FileNotFoundError(2, "No such file or directory")
        err.filename = setup.longpath("C:\\Users\\player\\Downloads\\x\\data\\toolchain", windows=True)
        text = setup.plain_text(err)
        self.assertNotIn("\\\\?\\", text)
        self.assertIn("'C:\\\\Users\\\\player\\\\Downloads", text)
        with mock.patch.object(setup, "GUI", None), mock.patch("builtins.print") as out:
            setup.say("Removed \\\\?\\C:\\Users\\player\\x and \\\\?\\UNC\\srv\\s\\y")
        out.assert_called_once_with("Removed C:\\Users\\player\\x and \\\\srv\\s\\y", flush=True)

    def test_measured_depths(self):
        self.assertEqual(setup.DEEPEST_DATA_PATH, 79)
        self.assertEqual(setup.MAX_DATA_DIR_CHARS, 179)
        cache = "nsmbu\\shadercache\\vulkan-%s-%s-%s.bin" % ("0" * 8, "0" * 8, "f" * 32)
        self.assertEqual(len(cache), setup.DEEPEST_DATA_PATH)
        game = "game\\content\\Common\\layout\\ChallengeImage\\ChallengeImage_132.gtx"
        self.assertLessEqual(len(game), setup.DEEPEST_DATA_PATH)
        self.assertLessEqual(len(game.replace("game", "game.partial", 1)), setup.DEEPEST_DATA_PATH)
        compilers = 0
        for tc in setup.load_toolchains()["toolchains"].values():
            if tc["kind"] == "llvm-mingw":
                triple = tc.get("triple", "x86_64-w64-mingw32")
                for program in ("-clang.exe", "-clang++.exe"):
                    exe = "toolchain\\%s\\bin\\%s%s" % (tc["dir"], triple, program)
                    self.assertLessEqual(len(exe), setup.DEEPEST_DATA_PATH, exe)
                    compilers += 1
        self.assertGreaterEqual(compilers, 4)
        own = [
            "nsmbu\\shadercache\\vulkan-shaders\\spirv.bin",
            "nsmbu\\screenshots\\NSMBU_2026-10-26_17-28-42_3.png",
            "nsmbu\\save\\user\\rp_personal_savedata.dat",
            "work\\shader-warm\\save\\user\\rp_personal_savedata.dat",
        ]
        for path in own:
            self.assertLessEqual(len(path), setup.DEEPEST_DATA_PATH, path)

    def test_deepest_accepted_folder(self):
        pkg = "C:\\" + "p" * (setup.MAX_DATA_DIR_CHARS - len("\\data") - 3)
        self.assertEqual(len(pkg), 174)
        self.assertGreater(len(pkg + "\\data\\toolchain\\unpack.tmp\\") + 100, setup.WINDOWS_MAX_PATH)
        for folder in (self.RELEASE, self.RELEASE.replace("player", "a.much.longer.account.name"), "C:\\Games\\NSMBU", pkg):
            self.assertIsNone(setup.path_length_problem(folder + "\\data", folder, windows=True), folder)
        deeper = pkg + "p"
        self.assertEqual(setup.path_length_problem(deeper + "\\data", deeper, windows=True),
                         "This folder's path is too long for Windows (175 characters, at most 174 work). Move the NSMBU "
                         "folder to a shorter place such as C:\\Games\\NSMBU and start it again.")
        self.assertIsNone(setup.path_length_problem("/x" * 200, "/x" * 199, windows=False))

    def test_limit_boundary(self):
        pkg = "C:\\" + "p" * (setup.MAX_DATA_DIR_CHARS - len("\\data") - 3)
        self.assertEqual(len(pkg + "\\data"), setup.MAX_DATA_DIR_CHARS)
        self.assertIsNone(setup.path_length_problem(pkg + "\\data", pkg, windows=True))
        self.assertIsNotNone(setup.path_length_problem(pkg + "p\\data", pkg + "p", windows=True))
        self.assertIn("(175 characters, at most 174 work)",
                      setup.path_length_problem(setup.longpath(pkg + "p\\data", windows=True), "c" + pkg[1:] + "p",
                                                windows=True))

    def test_install_folder_outside_the_release(self):
        data = "D:\\" + "\\".join(["games"] * 30)
        msg = setup.path_length_problem(data, self.RELEASE, windows=True)
        self.assertIn("The install folder's path is too long for Windows (%d characters, at most 179 work)" % len(data), msg)

    def test_setup_stops_before_anything_else(self):
        with tempfile.TemporaryDirectory() as d:
            data = os.path.join(d, "x" * (setup.MAX_DATA_DIR_CHARS - len(d)), "data")
            args = mock.Mock(data_dir=data, app_dir=None)
            saved = setup.LOG.f
            try:
                with mock.patch.object(setup, "IS_WIN", True), mock.patch.object(setup, "PKG", os.path.dirname(data)), \
                        mock.patch.object(setup, "load_manifest", return_value={"version": "v0", "exe": "nsmbu"}):
                    with self.assertRaises(setup.SetupError) as e:
                        setup.Ctx(args)
                self.assertTrue(str(e.exception).startswith("This folder's path is too long for Windows"))
                with open(os.path.join(data, "setup.log"), encoding="utf-8") as f:
                    self.assertIn("too long for Windows", f.read())
            finally:
                if setup.LOG.f is not saved:
                    setup.LOG.f.close()
                setup.LOG.f = saved

    def test_unpacks_a_deep_compiler_archive_into_a_deep_folder(self):
        import shutil
        import zipfile
        top = tempfile.mkdtemp()
        self.addCleanup(lambda: shutil.rmtree(setup.longpath(top), ignore_errors=True))
        deep = setup.NATIVE_WINDOWS_PATHS or not setup.IS_WIN
        data = os.path.join(top, "d" * max(1, (120 if deep else 20) - len(top) - 1))
        os.makedirs(data)
        member = "llvm-mingw-test/" + "/".join(["python-lib-site-packages-%02d" % i for i in range(5 if deep else 2)]) + "/deep_module.pyd"
        arc = os.path.join(top, "src.zip")
        with zipfile.ZipFile(arc, "w") as z:
            z.writestr(member, b"deep")
            z.writestr("llvm-mingw-test/bin/aarch64-w64-mingw32-clang.exe", b"")
        sha = setup.file_sha256(arc)
        pins = {"toolchains": {"test-mingw": {"kind": "llvm-mingw", "url": "https://example.invalid/llvm-mingw-test.zip",
                                              "sha256": sha, "size": os.path.getsize(arc), "dir": "llvm-mingw-test",
                                              "triple": "aarch64-w64-mingw32"}}}
        env = {k: v for k, v in os.environ.items() if k != "NSMBU_TOOLCHAIN_DIR"}
        fetch = lambda url, dst, *a, **k: shutil.copyfile(arc, dst)
        with mock.patch.object(setup, "load_toolchains", return_value=pins), mock.patch.object(setup, "download", fetch), \
                mock.patch.dict(os.environ, env, clear=True), mock.patch.object(setup, "say"):
            tc = setup.get_toolchain("test-mingw", data, setup.UI(False))
        root = os.path.join(data, "toolchain")
        deep = os.path.join(root, *member.split("/"))
        if setup.NATIVE_WINDOWS_PATHS:
            self.assertGreater(len(os.path.join(root, "unpack.tmp", *member.split("/"))), 260)
        with open(setup.longpath(deep), "rb") as f:
            self.assertEqual(f.read(), b"deep")
        self.assertEqual(tc.cc, [os.path.join(root, "llvm-mingw-test", "bin", "aarch64-w64-mingw32-clang.exe")])
        self.assertEqual(tc.cxx, [os.path.join(root, "llvm-mingw-test", "bin", "aarch64-w64-mingw32-clang++.exe")])
        self.assertEqual(sorted(os.listdir(root)), ["llvm-mingw-test"])
        marker = os.path.join(root, "llvm-mingw-test", ".nsmbu-toolchain")
        self.assertEqual(setup.folder_size(root), 4 + os.path.getsize(setup.longpath(marker)))
        with mock.patch.object(setup, "say"):
            setup.remove_toolchain(data)
        self.assertFalse(os.path.exists(root))

    def test_pinned_toolchains_name_their_triple(self):
        tcs = setup.load_toolchains()["toolchains"]
        self.assertEqual(tcs["llvm-mingw-20260922-aarch64"]["triple"], "aarch64-w64-mingw32")
        self.assertNotIn("triple", tcs["llvm-mingw-20260922"])
        with tempfile.TemporaryDirectory() as d, mock.patch.dict(os.environ, {"NSMBU_TOOLCHAIN_DIR": d}):
            tc = setup.get_toolchain("llvm-mingw-20260922-aarch64", d, setup.UI(False))
            self.assertEqual(os.path.basename(tc.cc[0]), "aarch64-w64-mingw32-clang.exe")
            tc = setup.get_toolchain("llvm-mingw-20260922", d, setup.UI(False))
            self.assertEqual(os.path.basename(tc.cxx[0]), "x86_64-w64-mingw32-clang++.exe")

class BracketsInInstallFolder(unittest.TestCase):
    def test_recompile_counts_generated_code_in_a_folder_with_brackets(self):
        with tempfile.TemporaryDirectory() as temporary:
            gen_dir = os.path.join(temporary, "NSMBU [WiiU]", "data", "work", "gen")

            class FakeProc:
                def __init__(self):
                    self.stdout = iter([])

                def wait(self):
                    os.makedirs(gen_dir, exist_ok=True)
                    for name in ("code_000.c", "code_001.c"):
                        open(os.path.join(gen_dir, name), "w").close()
                    return 0

            with mock.patch.object(setup.subprocess, "Popen", return_value=FakeProc()):
                self.assertEqual(setup.recompile(os.path.join(temporary, "game"), gen_dir), 2)


class NonInteractive(unittest.TestCase):
    def test_ui_refuses_to_prompt(self):
        ui = setup.UI(False)
        with self.assertRaises(setup.SetupError):
            ui.ask("question")
        self.assertTrue(ui.yesno("q", True))
        self.assertFalse(ui.yesno("q", True, noninteractive=False))

if __name__ == "__main__":
    unittest.main(verbosity=2)

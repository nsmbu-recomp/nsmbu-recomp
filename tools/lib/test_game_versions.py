import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import game_versions as gv


def write_rpx(folder):
    code = os.path.join(folder, "code")
    os.makedirs(code, exist_ok=True)
    os.makedirs(os.path.join(folder, "content"), exist_ok=True)
    os.makedirs(os.path.join(folder, "meta"), exist_ok=True)
    with open(os.path.join(code, "red-pro2.rpx"), "wb") as f:
        f.write(b"fake")


class GameVersions(unittest.TestCase):
    def test_list_and_activate(self):
        with tempfile.TemporaryDirectory() as tmp:
            write_rpx(os.path.join(tmp, "usa"))
            write_rpx(os.path.join(tmp, "eu"))
            self.assertEqual(gv.list_versions(tmp), ["eu", "usa"])
            info = gv.activate(tmp, "usa")
            self.assertEqual(info["id"], "usa")
            self.assertTrue(info["extracted"])
            self.assertEqual(gv.read_active(tmp), "usa")
            self.assertEqual(gv.active_dir(tmp), os.path.join(tmp, "usa"))
            self.assertFalse(os.path.lexists(os.path.join(tmp, "code")))
            gv.activate(tmp, "eu")
            self.assertEqual(gv.active_dir(tmp), os.path.join(tmp, "eu"))
            self.assertEqual(gv.read_active(tmp), "eu")

    def test_list_dumps_includes_root_wux(self):
        with tempfile.TemporaryDirectory() as tmp:
            folder = os.path.join(tmp, "eu-base")
            os.makedirs(folder)
            with open(os.path.join(tmp, "New Super Mario Bros. U (Europe).wux"), "wb") as f:
                f.write(b"disc")
            self.assertEqual(gv.list_dumps(tmp), ["eu-base"])
            self.assertEqual(gv.list_versions(tmp), [])
            info = gv.describe_version(tmp, "eu-base")
            self.assertFalse(info["extracted"])
            self.assertTrue(info["has_wux"])

    def test_create_dump(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = gv.create_dump(tmp, "usa")
            self.assertEqual(path, os.path.join(tmp, "usa"))
            self.assertTrue(os.path.isdir(os.path.join(path, "code")))
            with self.assertRaises(gv.GameVersionsError):
                gv.create_dump(tmp, "usa")

    def test_clears_legacy_overlays(self):
        with tempfile.TemporaryDirectory() as tmp:
            write_rpx(os.path.join(tmp, "usa"))
            os.symlink("usa/code", os.path.join(tmp, "code"))
            os.symlink("usa/game.wux", os.path.join(tmp, "game.wux"))
            gv.activate(tmp, "usa")
            self.assertFalse(os.path.lexists(os.path.join(tmp, "code")))
            self.assertFalse(os.path.lexists(os.path.join(tmp, "game.wux")))

    def test_migrate_flat(self):
        with tempfile.TemporaryDirectory() as tmp:
            write_rpx(tmp)
            with open(os.path.join(tmp, "game.key"), "wb") as f:
                f.write(b"k")
            info = gv.migrate_flat(tmp, "usa")
            self.assertEqual(info["id"], "usa")
            self.assertFalse(gv.flat_layout(tmp))
            self.assertTrue(os.path.isfile(os.path.join(tmp, "usa", "code", "red-pro2.rpx")))
            self.assertFalse(os.path.lexists(os.path.join(tmp, "code")))
            self.assertEqual(gv.active_dir(tmp), os.path.join(tmp, "usa"))
            self.assertTrue(os.path.isfile(os.path.join(tmp, "usa", "game.key")))

    def test_flat_and_versions_errors(self):
        with tempfile.TemporaryDirectory() as tmp:
            write_rpx(tmp)
            write_rpx(os.path.join(tmp, "eu"))
            with self.assertRaises(gv.GameVersionsError):
                gv.migrate_flat(tmp)
            with self.assertRaises(gv.GameVersionsError):
                gv.resolve_for_select(tmp, interactive=False)


if __name__ == "__main__":
    unittest.main()

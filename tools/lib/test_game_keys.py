import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import game_keys
import game_versions as gv

COMMON = "d7b00402659ba2abd2cb0db27fa2b656"
USA_DISC = "185f9d54d98599ab5fc4acec76e86645"
EU_DISC = "04ac7a687613016d99d067e5f894c5d9"


def write_keys(game, extra=""):
    with open(os.path.join(game, "keys.txt"), "w") as f:
        f.write("common = %s\n" % COMMON)
        f.write("0005000010101d00 = %s\n" % USA_DISC)
        f.write("0005000010101e00 = %s\n" % EU_DISC)
        if extra:
            f.write(extra)


class GameKeys(unittest.TestCase):
    def test_list_loose_images(self):
        with tempfile.TemporaryDirectory() as tmp:
            open(os.path.join(tmp, "New Super Mario Bros. U (USA).wux"), "wb").close()
            open(os.path.join(tmp, "other.wud"), "wb").close()
            os.makedirs(os.path.join(tmp, "usa-base"))
            names = [os.path.basename(p) for p in game_keys.list_loose_images(tmp)]
            self.assertEqual(names, ["New Super Mario Bros. U (USA).wux", "other.wud"])

    def test_keys_from_keys_txt(self):
        with tempfile.TemporaryDirectory() as tmp:
            write_keys(tmp)
            image = os.path.join(tmp, "New Super Mario Bros. U (USA).wux")
            open(image, "wb").close()
            disc, disc_src = game_keys.find_disc_key(image, tmp)
            common, common_src = game_keys.find_common_key(image, tmp)
            self.assertEqual(disc.hex(), USA_DISC)
            self.assertIn("0005000010101d00", disc_src)
            self.assertEqual(common.hex(), COMMON)
            self.assertTrue(common_src.endswith("keys.txt"))
            self.assertTrue(game_keys.keys_ready(image, tmp))

    def test_europe_title_key(self):
        with tempfile.TemporaryDirectory() as tmp:
            write_keys(tmp)
            image = os.path.join(tmp, "New Super Mario Bros. U (Europe).wux")
            open(image, "wb").close()
            disc, src = game_keys.find_disc_key(image, tmp)
            self.assertEqual(disc.hex(), EU_DISC)
            self.assertIn("0005000010101e00", src)

    def test_image_for_version(self):
        with tempfile.TemporaryDirectory() as tmp:
            usa = os.path.join(tmp, "New Super Mario Bros. U (USA).wux")
            open(usa, "wb").close()
            self.assertEqual(game_keys.image_for_version(tmp, "usa-base"), usa)
            self.assertEqual(game_keys.image_for_version(tmp, "usa-1.3.0"), usa)
            self.assertIsNone(game_keys.image_for_version(tmp, "eu-base"))

    def test_import_renames_at_root(self):
        with tempfile.TemporaryDirectory() as tmp:
            image = os.path.join(tmp, "My Dump.wux")
            open(image, "wb").close()
            dest = game_keys.import_loose_image(tmp, image, "usa-base")
            self.assertEqual(dest, os.path.join(tmp, "New Super Mario Bros. U (USA).wux"))
            self.assertTrue(os.path.isfile(dest))
            self.assertFalse(os.path.lexists(image))
            self.assertTrue(os.path.isdir(os.path.join(tmp, "usa-base", "code")))
            self.assertFalse(os.path.isfile(os.path.join(tmp, "usa-base", "game.wux")))

    def test_describe_uses_root_image(self):
        with tempfile.TemporaryDirectory() as tmp:
            write_keys(tmp)
            os.makedirs(os.path.join(tmp, "usa-base", "code"))
            open(os.path.join(tmp, "New Super Mario Bros. U (USA).wux"), "wb").close()
            info = gv.describe_version(tmp, "usa-base")
            self.assertTrue(info["has_wux"])
            self.assertTrue(info["has_key"])
            self.assertTrue(info["image"].endswith("(USA).wux"))


if __name__ == "__main__":
    unittest.main()

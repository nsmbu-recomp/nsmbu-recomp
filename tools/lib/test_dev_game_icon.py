import os
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dev_game_icon
import game_versions as gv


class DevGameIcon(unittest.TestCase):
    def test_running_in_ci(self):
        with patch.dict(os.environ, {"GITHUB_ACTIONS": "true"}, clear=False):
            self.assertTrue(dev_game_icon.running_in_ci())
        with patch.dict(os.environ, {"CI": "true"}, clear=False):
            self.assertTrue(dev_game_icon.running_in_ci())
        with patch.dict(os.environ, {}, clear=True):
            self.assertFalse(dev_game_icon.running_in_ci())

    def test_icon_tga_path(self):
        with tempfile.TemporaryDirectory() as tmp:
            game = os.path.join(tmp, "game")
            dump = os.path.join(game, "usa-1.3.0")
            os.makedirs(os.path.join(dump, "meta"))
            tga = os.path.join(dump, "meta", "iconTex.tga")
            with open(tga, "wb") as f:
                f.write(b"x")
            gv.write_active(game, "usa-1.3.0")
            self.assertEqual(dev_game_icon.icon_tga_path(game), tga)

    def test_refresh_skips_in_ci(self):
        with patch.dict(os.environ, {"CI": "true"}, clear=False):
            with patch.object(dev_game_icon, "subprocess") as sp:
                self.assertFalse(dev_game_icon.refresh_android_local_icon())
                sp.run.assert_not_called()

    def test_refresh_runs_make_icon(self):
        with tempfile.TemporaryDirectory() as tmp:
            dump = os.path.join(tmp, "game", "usa-1.3.0")
            os.makedirs(os.path.join(dump, "meta"))
            with open(os.path.join(dump, "meta", "iconTex.tga"), "wb") as f:
                f.write(b"x")
            gv.write_active(os.path.join(tmp, "game"), "usa-1.3.0")
            root = os.path.join(tmp, "repo")
            os.makedirs(root)
            script = os.path.join(root, "runtime", "android", "make_icon.py")
            os.makedirs(os.path.dirname(script))
            with open(script, "w") as f:
                f.write("#!/usr/bin/env python3\nimport sys, os\n"
                        "out = os.path.join(os.path.dirname(__file__), 'app', 'local-res')\n"
                        "os.makedirs(out)\n")
            with patch.dict(os.environ, {"CI": "", "GITHUB_ACTIONS": ""}, clear=False):
                with patch.object(dev_game_icon, "ROOT", root):
                    with patch.object(dev_game_icon, "MAKE_ICON", script):
                        with patch.object(dev_game_icon.gv, "active_dir", return_value=dump):
                            self.assertTrue(dev_game_icon.refresh_android_local_icon(root))


if __name__ == "__main__":
    unittest.main()

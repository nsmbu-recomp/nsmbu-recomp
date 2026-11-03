import subprocess
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
ANDROID = ROOT / "runtime/android"
MASTER = ANDROID / "icon" / "app_icon_1024.png"
RES = ANDROID / "app" / "src" / "main" / "res"


class BakeAppIcon(unittest.TestCase):
    def test_master_icon_is_square_png(self):
        if not MASTER.is_file():
            self.skipTest("master icon missing")
        try:
            from PIL import Image
        except ImportError:
            self.skipTest("Pillow required")
        with Image.open(MASTER) as image:
            self.assertEqual(image.size, (1024, 1024))
            self.assertIn(image.mode, ("RGB", "RGBA"))

    def test_bake_writes_launcher_sizes(self):
        if not MASTER.is_file():
            self.skipTest("master icon missing")
        try:
            import PIL
        except ImportError:
            self.skipTest("Pillow required")
        subprocess.run(["python3", str(ANDROID / "bake_app_icon.py")], cwd=ROOT, check=True)
        for folder, size in (("mdpi", 48), ("hdpi", 72), ("xhdpi", 96), ("xxhdpi", 144), ("xxxhdpi", 192)):
            path = RES / f"mipmap-{folder}" / "ic_launcher.png"
            self.assertTrue(path.is_file(), path)
            from PIL import Image
            with Image.open(path) as image:
                self.assertEqual(image.size, (size, size))
        art = RES / "drawable-nodpi" / "ic_launcher_art.png"
        self.assertTrue(art.is_file())
        from PIL import Image
        with Image.open(art) as image:
            self.assertEqual(image.size, (432, 432))
        xml = (RES / "mipmap-anydpi-v26" / "ic_launcher.xml").read_text()
        self.assertIn("@drawable/ic_launcher_art", xml)


if __name__ == "__main__":
    unittest.main()

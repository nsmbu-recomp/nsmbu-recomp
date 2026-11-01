import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
WORKFLOW = (ROOT / ".github/workflows/release.yml").read_text()


def job(name):
    match = re.search(r"^  %s:\n(.*?)(?=^  [a-z][a-z0-9_-]*:\n|\Z)" % re.escape(name), WORKFLOW, re.S | re.M)
    if not match:
        raise AssertionError("no %s job in release.yml" % name)
    return match.group(1)


class ReleaseWorkflowLinux(unittest.TestCase):
    def test_linux_installs_sdl3_from_ci_assets(self):
        linux = job("linux")
        self.assertIn('tools/release/sdl3.py install --platform "linux-$ARCH"', linux)
        self.assertIn("$HOME/sdl3/LICENSE.txt", linux)
        self.assertNotIn("cmake -S SDL3", linux)
        self.assertNotIn("path: ~/sdl3", linux)


if __name__ == "__main__":
    unittest.main()

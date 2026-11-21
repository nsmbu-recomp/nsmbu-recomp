import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
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

    def test_named_test_release_replaces_only_its_own_tag(self):
        publish = job("publish")
        self.assertIn("test_name:", WORKFLOW)
        self.assertIn('gh release delete "$TEST_TAG" --cleanup-tag --yes', publish)
        self.assertIn("--prerelease --latest=false", publish)
        self.assertIn('notes.py README.md "$VERSION" --test "$TEST_TAG"', publish)
        self.assertIn("gh release delete devel-build --cleanup-tag --yes", publish)


if __name__ == "__main__":
    unittest.main()

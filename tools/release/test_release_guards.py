import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
WORKFLOW = (ROOT / ".github/workflows/release.yml").read_text()
HOOK = (ROOT / ".githooks/pre_push.py").read_text()


def job(name):
    match = re.search(r"^  %s:\n(.*?)(?=^  [a-z][a-z0-9_-]*:\n|\Z)" % re.escape(name), WORKFLOW, re.S | re.M)
    if not match:
        raise AssertionError("no %s job in release.yml" % name)
    return match.group(1)


class ReleaseGuards(unittest.TestCase):
    def test_gradle_setup_does_not_recheck_fetched_wrappers(self):
        android = job("android")
        self.assertIn("gradle/actions/wrapper-validation@v4", android)
        setup = android.split("gradle/actions/setup-gradle@v4", 1)[1][:500]
        self.assertIn("validate-wrappers: false", setup)

    def test_windows_arm64_build_does_not_launch_ccache(self):
        windows = job("windows")
        build = windows.split("with llvm-mingw against placeholder guest code", 1)[1].split("- name:", 1)[0]
        start = build.index('if [[ "${{ matrix.arch }}" != arm64 ]]; then')
        end = build.index("\n          fi\n", start)
        guarded = build[start:end]
        outside = build[:start] + build[end:]
        self.assertIn('COMPILER_LAUNCHER="$CCACHE"', guarded)
        self.assertNotIn("COMPILER_LAUNCHER", outside)

    def test_linux_release_compiles_with_zig(self):
        linux = job("linux")
        self.assertIn('-DCMAKE_C_COMPILER="$ZBIN/zcc"', linux)
        self.assertIn('-DCMAKE_CXX_COMPILER="$ZBIN/zcxx"', linux)

    def test_pre_push_runs_the_release_gates(self):
        self.assertIn("test_release_guards.py", HOOK)
        self.assertIn("zig_gate.py", HOOK)


if __name__ == "__main__":
    unittest.main()

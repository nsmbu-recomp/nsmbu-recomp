import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
WORKFLOW = (ROOT / ".github/workflows/release.yml").read_text()
GRADLE = (ROOT / "android/app/build.gradle").read_text()


def job(name):
    match = re.search(r"^  %s:\n(.*?)(?=^  [a-z][a-z0-9_-]*:\n|\Z)" % re.escape(name), WORKFLOW, re.S | re.M)
    if not match:
        raise AssertionError("no %s job in release.yml" % name)
    return match.group(1)


class ReleaseWorkflow(unittest.TestCase):
    def test_android_job_builds_both_abis_without_game_code(self):
        android = job("android")
        self.assertIn("abi: arm64-v8a", android)
        self.assertIn("abi: x86_64", android)
        self.assertIn("APK: dist/NSMBU-${{ needs.version.outputs.version }}-android-${{ matrix.name }}.apk", android)
        self.assertIn("tools/recomp/stubgen.py build/gen-stub", android)
        self.assertLess(android.index('rm "android/app/libs/$ABI/libmain.so"'), android.index("assembleRelease"))
        self.assertIn('python3 tools/release/guard.py "$APK"', android)
        self.assertNotIn("--extraction-fixture", android)
        self.assertNotIn("--service-fixture", android)
        self.assertNotIn("template", android.lower())

    def test_gradle_properties_exist(self):
        used = set(re.findall(r"-P(nsmbu\w+)=", job("android")))
        self.assertEqual(used, {"nsmbuAbi", "nsmbuVersionName", "nsmbuVersionCode",
                                "nsmbuPythonDir", "nsmbuToolchainDir", "nsmbuExtractorDir"})
        for name in used:
            self.assertIn("gradleProperty('%s')" % name, GRADLE)
        for name in re.findall(r"^\s+(NSMBU_\w+): \$\{\{ secrets", job("android"), re.M):
            self.assertIn("'%s'" % name, GRADLE)

    def test_publish_ships_the_apks(self):
        publish = job("publish")
        self.assertRegex(publish, r"needs: \[[^\]]*\bandroid\b")
        self.assertIn("guard.py dist/*.zip dist/*.apk", publish)
        self.assertIn("*.apk > SHA256SUMS.txt", publish)
        self.assertIn("dist/*.apk dist/SHA256SUMS.txt", publish)

    def test_release_downloads_the_compiler_release(self):
        android = job("android")
        self.assertIn("uses: ./.github/actions/android-compiler", android)
        self.assertNotIn("build_toolchain.py", WORKFLOW)
        self.assertNotIn("lookup-only", WORKFLOW)
        self.assertNotIn("android-compiler-cache", WORKFLOW)
        action = (ROOT / ".github/actions/android-compiler/action.yml").read_text()
        self.assertIn("gh release download android-clang", action)
        self.assertIn("nsmbu-recomp/ci-assets", action)
        self.assertNotIn("build_toolchain.py --source", action)
        self.assertIn("nsmbu-recomp/ci-assets", (ROOT / "tools/release/appimage.py").read_text())
        self.assertIn('extractfile("./build.json")', action)


if __name__ == "__main__":
    unittest.main()

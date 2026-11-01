"""The app's Gradle script agrees with the packaging scripts and the release guard."""
import importlib.util
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]
GRADLE = (ROOT / "runtime/android/app/build.gradle").read_text()
spec = importlib.util.spec_from_file_location("nsmbu_guard", ROOT / "tools/release/guard.py")
guard = importlib.util.module_from_spec(spec)
spec.loader.exec_module(guard)


class GradleConfig(unittest.TestCase):
    def test_release_python_check_matches_the_guard(self):
        block = GRADLE[GRADLE.index("tasks.register('verifyReleasePython')"):]
        listed = set(re.findall(r"'(tools/[^']+)'", block[:block.index(".any")]))
        self.assertEqual(listed, guard.ANDROID_TEST_FILES)
        self.assertIn("dependsOn 'verifyReleasePython'", GRADLE)

    def test_setup_packages_and_release_properties(self):
        for name in ("nsmbuPythonDir", "nsmbuToolchainDir", "nsmbuExtractorDir", "nsmbuAbi",
                     "nsmbuVersionName", "nsmbuVersionCode", "NSMBU_KEYSTORE", "NSMBU_KEYSTORE_PASSWORD",
                     "NSMBU_KEY_ALIAS", "NSMBU_KEY_PASSWORD"):
            self.assertIn("'" + name + "'", GRADLE)
        self.assertIn("'**/libnsmbu-*.so'", GRADLE)
        self.assertNotIn("androidx.window", GRADLE)


if __name__ == "__main__":
    unittest.main()

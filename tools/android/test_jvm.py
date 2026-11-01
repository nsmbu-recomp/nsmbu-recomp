"""Compile and run the plain-Java checks of Android app classes that use no Android types."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
APP = ROOT / "android/app/src/main/java/org/nsmbrecomp/nsmbu"
TESTS = ROOT / "tools/android"
SUITES = {
    "org.nsmbrecomp.nsmbu.ResourceVerifierTest": ["ResourceVerifier.java"],
    "org.nsmbrecomp.nsmbu.ResourceCacheCleanupTest": ["ResourceVerifier.java", "ResourceCacheCleanup.java"],
    "org.nsmbrecomp.nsmbu.AndroidUserDirectoriesTest": ["AndroidUserDirectories.java"],
    "org.nsmbrecomp.nsmbu.SetupFolderCheckTest": ["SetupFolderCheck.java"],
    "org.nsmbrecomp.nsmbu.SetupJobCleanupTest": ["SetupJobCleanup.java"],
    "org.nsmbrecomp.nsmbu.SetupPickerTest": ["SetupPicker.java"],
    "SetupKeyRecoveryPolicyTest": ["SetupKeyRecoveryPolicy.java"],
    "SetupPolicyTest": ["SetupPolicy.java"],
    "org.nsmbrecomp.nsmbu.SetupLogFilesTest": ["SetupLogFiles.java"],
    "SetupPresentationTest": ["SetupPresentation.java"],
    "SetupRecoveryPolicyTest": ["SetupRecoveryPolicy.java"],
    "org.nsmbrecomp.nsmbu.HeatGuardDeviceTest": ["HeatGuardDevice.java"],
    "org.nsmbrecomp.nsmbu.TouchLayoutTest": ["TouchLayout.java"],
    "org.nsmbrecomp.nsmbu.ControllerHintPolicyTest": ["ControllerHintPolicy.java"],
    "org.nsmbrecomp.nsmbu.PhoneRumbleTest": ["stubs/phone_rumble", "PhoneRumble.java"],
    "org.nsmbrecomp.nsmbu.SystemExitReportTest": ["SystemExitReport.java", "CrashReportFiles.java"],
    "org.nsmbrecomp.nsmbu.SystemExitCollectorTest": ["stubs/android_exit", "SystemExitReport.java", "SystemExitReports.java"],
}


def source_paths(names):
    """App sources by file name; a "stubs/..." name stands for every Java file of that test stub tree."""
    paths = []
    for name in names:
        paths += sorted(str(path) for path in (TESTS / name).rglob("*.java")) if name.startswith("stubs/") else [str(APP / name)]
    return paths


class JvmTests(unittest.TestCase):
    def run_suite(self, main, sources):
        if not shutil.which("javac") or not shutil.which("java"):
            self.skipTest("JDK required")
        test = TESTS / (main.rsplit(".", 1)[-1] + ".java")
        with tempfile.TemporaryDirectory() as classes:
            compiled = subprocess.run(["javac", "-d", classes] + source_paths(sources) + [str(test)],
                                      capture_output=True, text=True)
            self.assertEqual(compiled.returncode, 0, compiled.stderr)
            result = subprocess.run(["java", "-cp", classes, main], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_suites(self):
        for main, sources in SUITES.items():
            with self.subTest(main=main):
                self.run_suite(main, sources)


if __name__ == "__main__":
    unittest.main()

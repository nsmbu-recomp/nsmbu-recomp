import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
APP = ROOT / "android/app/src/main"
JAVA = APP / "java/org/nsmbrecomp/nsmbu"


def text(path):
    return (ROOT / path).read_text()


class PhoneWiring(unittest.TestCase):
    def test_heat_guard_jni_matches_the_java_class(self):
        native = text("runtime/src/platform/perf_hint.cpp")
        self.assertIn("Java_org_nsmbrecomp_nsmbu_HeatGuardDevice_nativeSetDeviceClass", native)
        self.assertIn("private static native void nativeSetDeviceClass(int deviceClass);", text(JAVA / "HeatGuardDevice.java"))
        self.assertIn('ENV(HEAT_GUARD, "NSMBU_HEAT_GUARD")', text("runtime/src/core/env.h"))

    def test_shared_logs_are_the_ones_the_runtime_writes(self):
        main = text("runtime/src/main.cpp")
        for name in ("nsmbu.log", "nsmbu-previous.log"):
            self.assertIn(name, main)
            self.assertIn('"%s"' % name, text(JAVA / "LogShare.java"))
        self.assertIn('new File(AndroidUserDirectories.config(root), "logs")', text(JAVA / "LogShare.java"))
        self.assertIn("SetupLogFiles.sources", text(JAVA / "LogShare.java"))
        for name in ("host.json", "state.json", "translation.log", "compile.log", "extract.log", "link.log"):
            self.assertIn('"%s"' % name, text(JAVA / "SetupLogFiles.java"))
        self.assertIn('return config_dir() + "/logs";', text("runtime/src/platform/host.h"))
        self.assertNotRegex(main, r"#ifdef __ANDROID__\s*\n\s*if \(!e \|\| !\*e\) return;")

    def test_touch_controls_jni_matches_the_java_natives(self):
        native = text("runtime/src/platform/touch_pad.cpp")
        natives = re.findall(r"private static native \w+ (\w+)\(", text(JAVA / "TouchControls.java"))
        self.assertEqual(sorted(natives), sorted(re.findall(r"Java_org_nsmbrecomp_nsmbu_TouchControls_(\w+)\(", native)))
        self.assertIn("runtime/src/platform/touch_pad.cpp", text("CMakeLists.txt"))
        self.assertIn("new TouchControls(this)", text(JAVA / "NsmbuActivity.java"))

    def test_phone_rumble_reaches_the_activity(self):
        self.assertIn('GetStaticMethodID(cls,"phoneRumble","(I)V")', text("runtime/src/platform/input_sdl.cpp"))
        self.assertIn("public static void phoneRumble(int level)", text(JAVA / "NsmbuActivity.java"))
        self.assertIn('ENV(PHONE_RUMBLE, "NSMBU_PHONE_RUMBLE")', text("runtime/src/core/env.h"))

    def test_installer_skip_is_optional_and_android_settings_can_clear_it(self):
        self.assertIn("SkipInstaller.enabled", text(JAVA / "SetupActivity.java"))
        self.assertIn("setup_skip_installer", text(APP / "res/values/strings.xml"))
        overlay = text("runtime/src/overlay/overlay.cpp")
        self.assertIn("kAndroid", overlay)
        self.assertIn('"Debug", "Android"', overlay)
        self.assertIn("skipInstaller", overlay)

    def test_back_opens_settings(self):
        self.assertIn("SDL_SCANCODE_AC_BACK", text("runtime/src/platform/input_sdl.cpp"))
        self.assertIn("SDL_HINT_ANDROID_TRAP_BACK_BUTTON", text("runtime/src/gfx/vulkan/backend.cpp"))

    def test_files_provider_is_system_only_and_protects_the_game(self):
        manifest = text(APP / "AndroidManifest.xml")
        provider = re.search(r'<provider android:name="\.GameFilesProvider".*?</provider>', manifest, re.S)
        self.assertIsNotNone(provider)
        self.assertIn('android:permission="android.permission.MANAGE_DOCUMENTS"', provider.group(0))
        self.assertIn('{"game", "config/nsmbu/shadercache"}', text(JAVA / "GameFilesProvider.java"))
        self.assertIn('@xml/game_mode_config', manifest)
        config = text(APP / "res/xml/game_mode_config.xml")
        self.assertIn('android:allowGameDownscaling="false"', config)
        self.assertIn('android:allowGameFpsOverride="false"', config)


if __name__ == "__main__":
    unittest.main()

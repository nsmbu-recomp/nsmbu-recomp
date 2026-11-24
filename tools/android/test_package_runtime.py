"""Check that runtime delivery preserves linking and excludes generated game code."""
import json
from pathlib import Path
import sys
import tempfile
import zipfile
import unittest
from unittest.mock import patch

import package_runtime
from package_runtime import NDK_REVISION, ROOT, package, recipe

sys.path.insert(0, str(ROOT / "tools/release"))
import guard


class RuntimeRecipeTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.build, self.sdk = self.root / "build", self.root / "sdk"
        self.build.mkdir()
        for name in ("runtime.o", "libgamecode.a", "libgpu.a", "libSDL3.so"):
            (self.build / name).write_bytes(("authored fixture " + name).encode())

    @patch("package_runtime.subprocess.run")
    def test_retains_order_and_relocates_runtime_without_game_archive(self, strip):
        result = recipe(["clang++", "--target=aarch64-none-linux-android33", "--sysroot=/host/ndk",
                         "-shared", "-Xlinker", "--dependency-file=link.d", "-Wl,-soname,libmain.so",
                         "-o", "libmain.so", "runtime.o", "libgamecode.a", "libgpu.a", "libSDL3.so",
                         "libgpu.a", "-llog"], self.build, self.sdk, Path("llvm-strip"))
        start = result.index("{sdk}/obj/0-runtime.o")
        self.assertEqual(result[start:start + 5], ["{sdk}/obj/0-runtime.o", "{gamecode}",
                         "{sdk}/lib/1-libgpu.a", "{sdk}/lib/libSDL3.so", "{sdk}/lib/1-libgpu.a"])
        self.assertFalse(any("/host/" in arg or "dependency-file" in arg for arg in result))
        self.assertIn("-Wl,-soname,libnsmbugame.so", result)
        self.assertEqual((self.sdk / "obj/0-runtime.o").read_bytes(), (self.build / "runtime.o").read_bytes())
        self.assertFalse(any("gamecode" in str(path) for path in self.sdk.rglob("*")))
        self.assertEqual(strip.call_count, 3)

    @patch("package_runtime.subprocess.run")
    def test_missing_or_duplicate_game_archive_rejected(self, strip):
        for count in (0, 2):
            with self.subTest(count=count), self.assertRaisesRegex(ValueError, "Incomplete"):
                recipe(["clang++", "-shared", "-o", "libmain.so", "runtime.o"] +
                       ["libgamecode.a"] * count, self.build, self.sdk, Path("llvm-strip"))

    def test_unknown_link_inputs_rejected(self):
        with self.assertRaisesRegex(ValueError, "Unexpected Android link input"):
            recipe(["clang++", "unexpected.rsp"], self.build, self.sdk, Path("llvm-strip"))

    @patch("package_runtime.subprocess.run")
    def test_package_writes_sdk_manifest_and_passes_guard(self, strip):
        ndk = self.root / "ndk"
        (ndk / "toolchains/llvm/prebuilt/host/bin").mkdir(parents=True)
        (ndk / "toolchains/llvm/prebuilt/host/bin/llvm-strip").write_text("")
        (ndk / "source.properties").write_text("Pkg.Revision = " + NDK_REVISION + "\n")
        link = ["clang++", "--target=aarch64-none-linux-android33", "-shared", "-o", "libmain.so",
                "runtime.o", "libgamecode.a", "libgpu.a", "libSDL3.so", "-llog"]
        flags = ["--target=aarch64-none-linux-android33", "-O3", "-I{sdk}/include", "-I{gen}", "-g"]
        output = self.root / "out"
        with patch("package_runtime.link_arguments", return_value=link), \
             patch.object(package_runtime.desktop, "gamecode_flags", return_value=flags):
            metadata = package(self.build, ndk, "arm64-v8a", output, "v-test")
        with zipfile.ZipFile(output / "assets/runtime-sdk.zip") as bundle:
            names = bundle.namelist()
            manifest = json.loads(bundle.read("sdk/manifest.json"))
        self.assertEqual(manifest["identity"], metadata["identity"])
        self.assertEqual(manifest["platform"], "android-arm64-v8a")
        self.assertEqual(manifest["exe"], "libgame.so")
        self.assertEqual(manifest["version"], "v-test")
        self.assertEqual(manifest["gamecode_cflags"], ["-O3", "-I{sdk}/include", "-I{gen}", "-fPIC"])
        self.assertIn("{gamecode}", manifest["link"])
        self.assertIn("sdk/include/ppc.h", names)
        self.assertFalse(any("gamecode" in name for name in names))
        problems, _ = guard.scan(str(output / "assets/runtime-sdk.zip"))
        self.assertEqual(problems, [])


if __name__ == "__main__":
    unittest.main()

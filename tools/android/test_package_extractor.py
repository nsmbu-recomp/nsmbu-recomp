"""Reject a mismatched build before producing an installable extractor."""
from pathlib import Path
import json
import os
import stat
import tempfile
import unittest

from package_extractor import NAME, NDK_REVISION, package
from test_package_toolchain import elf


class ExtractorPackageTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.build, self.ndk, self.output = (self.root / name for name in ("build", "ndk", "apk"))
        self.build.mkdir()
        self.ndk.mkdir()
        (self.ndk / "source.properties").write_text("Pkg.Revision = " + NDK_REVISION + "\n")
        self.cache()
        (self.build / "nsmbu-zstd.txt").write_text("system\n")

    def cache(self, abi="arm64-v8a", api="android-33", toolchain=None):
        toolchain = toolchain or self.ndk / "build/cmake/android.toolchain.cmake"
        (self.build / "CMakeCache.txt").write_text(
            f"ANDROID_ABI:STRING={abi}\nANDROID_PLATFORM:STRING={api}\nCMAKE_TOOLCHAIN_FILE:FILEPATH={toolchain}\n")

    def rejected(self, message):
        with self.assertRaisesRegex(ValueError, message):
            package(self.build, self.ndk, "arm64-v8a", self.output)
        self.assertFalse(self.output.exists())

    def test_rejects_wrong_abi_or_api(self):
        self.cache(abi="x86_64")
        self.rejected("ABI/API")
        self.cache(api="android-29")
        self.rejected("ABI/API")

    def test_rejects_wrong_ndk_and_toolchain(self):
        self.cache(toolchain=self.root / "different-ndk/toolchain.cmake")
        self.rejected("pinned NDK toolchain")
        self.cache()
        (self.ndk / "source.properties").write_text("Pkg.Revision = 1\n")
        self.rejected("pinned NDK")

    def test_rejects_system_zstd(self):
        self.rejected("pinned bundled zstd")

    def test_packages_bundled_build(self):
        (self.build / "nsmbu-zstd.txt").write_text("bundled zstd\n")
        (self.build / "nsmbu-extract").write_bytes(elf())
        (self.build / "_deps/zstd-src").mkdir(parents=True)
        (self.build / "_deps/zstd-src/LICENSE").write_text("authored license")
        strip = self.ndk / "toolchains/llvm/prebuilt/host/bin/llvm-strip"
        strip.parent.mkdir(parents=True)
        strip.write_text("#!/bin/sh\nexit 0\n")
        strip.chmod(strip.stat().st_mode | stat.S_IXUSR)
        metadata = package(self.build, self.ndk, "arm64-v8a", self.output)
        installed = self.output / "jniLibs/arm64-v8a" / NAME
        self.assertEqual(installed.read_bytes(), elf())
        self.assertTrue(os.access(installed, os.X_OK))
        self.assertEqual(metadata["executable"], "libnsmbu-extract-driver.so")
        self.assertIn("tools/wudextract/nsmbu_extract.cpp", metadata["source_sha256"])
        written = json.loads((self.output / "assets/extractor-apk.json").read_text())
        self.assertEqual(written, metadata)
        self.assertTrue((self.output / "assets/extractor-licenses/zstd.txt").is_file())

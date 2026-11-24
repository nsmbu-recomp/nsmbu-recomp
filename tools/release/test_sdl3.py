#!/usr/bin/env python3
"""sdl3.py accepts only a prefix whose manifest matches the SDL and llvm-mingw pins."""
import hashlib
import io
import json
import os
import sys
import tarfile
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sdl3


def digest(data):
    return hashlib.sha256(data).hexdigest()


def archive_bytes(platform):
    blob = io.BytesIO()
    with tarfile.open(fileobj=blob, mode="w:gz") as tar:
        def add(name, data):
            info = tarfile.TarInfo(name)
            info.size = len(data)
            tar.addfile(info, io.BytesIO(data))
        add("LICENSE.txt", b"zlib\n")
        add("lib/cmake/SDL3/SDL3Config.cmake", b"")
        add("lib/cmake/SDL3/SDL3sharedTargets.cmake", b"")
        if platform.startswith("windows"):
            add("bin/SDL3.dll", b"dll")
            add("lib/cmake/SDL3/SDL3staticTargets.cmake", b"")
        else:
            add("lib/libSDL3.so.0", b"so")
    return blob.getvalue()


class Workflow(unittest.TestCase):
    def test_windows_release_links_the_prefix(self):
        with open(os.path.join(os.path.dirname(__file__), "..", "..", ".github", "workflows", "release.yml")) as handle:
            workflow = handle.read()
        start = workflow.find("\n  windows:\n")
        end = workflow.find("\n  android:\n")
        windows = workflow[start:end]
        self.assertIn('tools/release/sdl3.py install --platform "windows-${{ matrix.arch }}"', windows)
        self.assertIn('-DCMAKE_PREFIX_PATH="$SDL3_PREFIX"', windows)
        self.assertIn("$SDL3_PREFIX/LICENSE.txt", windows)
        self.assertNotIn("sdl3-src/LICENSE.txt", windows)


class Prefix(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.pins = sdl3.mingw_pins()
        self.assets = {}
        for platform in sdl3.PLATFORMS:
            data = archive_bytes(platform)
            name = sdl3.archive_name(platform)
            with open(os.path.join(self.tmp.name, name), "wb") as handle:
                handle.write(data)
            sha = digest(data)
            with open(os.path.join(self.tmp.name, name + ".sha256"), "w") as handle:
                handle.write("%s  %s\n" % (sha, name))
            self.assets[platform] = {"name": name, "sha256": sha}
        manifest = {
            "version": sdl3.VERSION,
            "upstream_sha256": sdl3.UPSTREAM_SHA256,
            "mingw_sha256": self.pins,
            "assets": self.assets,
        }
        with open(os.path.join(self.tmp.name, "sdl3.json"), "w") as handle:
            json.dump(manifest, handle)

    def tearDown(self):
        self.tmp.cleanup()

    def test_install_from_matching_release(self):
        dest = os.path.join(self.tmp.name, "prefix")
        sdl3.install_from(self.tmp.name, "windows-x86_64", dest, self.pins)
        self.assertTrue(os.path.isfile(os.path.join(dest, "bin", "SDL3.dll")))
        sdl3.install_from(self.tmp.name, "linux-aarch64", dest, self.pins)
        self.assertTrue(os.path.isfile(os.path.join(dest, "lib", "libSDL3.so.0")))

    def test_rejects_a_different_mingw_pin(self):
        pins = dict(self.pins)
        pins["llvm-mingw-20260922"] = "0" * 64
        with self.assertRaises(SystemExit):
            sdl3.install_from(self.tmp.name, "windows-x86_64", os.path.join(self.tmp.name, "out"), pins)

    def test_rejects_a_short_prefix(self):
        name = sdl3.archive_name("linux-x86_64")
        blob = io.BytesIO()
        with tarfile.open(fileobj=blob, mode="w:gz") as tar:
            info = tarfile.TarInfo("LICENSE.txt")
            info.size = 1
            tar.addfile(info, io.BytesIO(b"x"))
        data = blob.getvalue()
        with open(os.path.join(self.tmp.name, name), "wb") as handle:
            handle.write(data)
        sha = digest(data)
        with open(os.path.join(self.tmp.name, name + ".sha256"), "w") as handle:
            handle.write("%s  %s\n" % (sha, name))
        with open(os.path.join(self.tmp.name, "sdl3.json")) as handle:
            manifest = json.load(handle)
        manifest["assets"]["linux-x86_64"]["sha256"] = sha
        with open(os.path.join(self.tmp.name, "sdl3.json"), "w") as handle:
            json.dump(manifest, handle)
        with self.assertRaises(SystemExit):
            sdl3.install_from(self.tmp.name, "linux-x86_64", os.path.join(self.tmp.name, "out"), self.pins)


if __name__ == "__main__":
    unittest.main()

"""Exercise the real SDK payload with the unchanged release guard; no game files."""
from pathlib import Path
import ast
import importlib.util
import json
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest import mock
import zipfile

import guard
import package

class SDKPackage(unittest.TestCase):
    def test_platform_sdk_archives_pass_guard(self):
        for platform in ("macos-arm64", "linux-x86_64", "linux-aarch64", "windows-x86_64", "windows-arm64"):
            with self.subTest(platform=platform), tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp) / ("NSMBU-test-" + platform)
                package.copy_sdk_headers(str(root))
                include = root / "sdk/include"
                self.assertTrue((include / "ppc.h").is_file())
                self.assertFalse((root / "sdk/guest").exists())
                archive = Path(tmp) / "sdk.zip"
                with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as z:
                    for path in root.rglob("*"):
                        if path.is_file():
                            z.write(path, path.relative_to(root.parent).as_posix())
                problems, count = guard.scan(str(archive))
                self.assertEqual(count, 4)
                self.assertEqual(problems, [])

    def test_sdk_manifest_fields(self):
        manifest = package.sdk_manifest("v1", "android-arm64-v8a", "android-ndk", "libgame.so", "/ndk/bin/clang++",
                                        ["-O3"], ["{gamecode}", "-o", "{out}"])
        self.assertEqual(manifest["format"], 1)
        self.assertEqual(manifest["built_with"], "clang++")
        self.assertEqual(manifest["runtime_files"], [])
        self.assertEqual(manifest["link"], ["{gamecode}", "-o", "{out}"])

    def test_installer_local_imports_are_shipped(self):
        installer = Path(package.ROOT) / "installer"
        shipped = set(package.INSTALLER_FILES)
        for source in installer.glob("*.py"):
            if source.relative_to(package.ROOT).as_posix() not in shipped:
                continue
            for node in ast.walk(ast.parse(source.read_text())):
                names = ([alias.name for alias in node.names] if isinstance(node, ast.Import)
                         else [node.module] if isinstance(node, ast.ImportFrom) and node.module else [])
                for name in names:
                    sibling = installer / (name.split(".")[0] + ".py")
                    if sibling.is_file():
                        self.assertIn(sibling.relative_to(package.ROOT).as_posix(), shipped)

    def test_recomp_local_imports_are_shipped(self):
        recomp = Path(package.ROOT) / "recomp"
        shipped = set(package.TOOL_FILES)
        for relative in package.TOOL_FILES:
            if not relative.startswith("recomp/") or not relative.endswith(".py"):
                continue
            source = Path(package.ROOT) / relative
            for node in ast.walk(ast.parse(source.read_text())):
                names = ([alias.name for alias in node.names] if isinstance(node, ast.Import)
                         else [node.module] if isinstance(node, ast.ImportFrom) and node.module else [])
                for name in names:
                    if not name or name.startswith("."):
                        continue
                    sibling = recomp / (name.split(".")[0] + ".py")
                    if sibling.is_file():
                        self.assertIn(sibling.relative_to(package.ROOT).as_posix(), shipped,
                                      relative + " imports " + name)
        self.assertIn("recomp/names.py", shipped)
        self.assertNotIn("recomp/headers_names.txt", shipped)

    def test_names_load_without_data_file(self):
        spec = importlib.util.spec_from_file_location(
            "recomp_names", Path(package.ROOT) / "recomp/names.py")
        names = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(names)
        with mock.patch.object(names, "DATA_FILE", "/nonexistent/headers_names.txt"):
            self.assertEqual(names.load(), {})

    def test_packaged_recomp_imports_in_isolated_python(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp) / "pkg"
            for relative in package.TOOL_FILES:
                dest = root / relative
                dest.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(Path(package.ROOT) / relative, dest)
            maps = Path(package.ROOT) / "recomp/builds"
            if maps.is_dir():
                shutil.copytree(maps, root / "recomp/builds")
            result = subprocess.run(
                [sys.executable, "-I", "-B", "-c",
                 "import runpy,sys; runpy.run_path(sys.argv[1], run_name='recomp_import_test')",
                 str(root / "recomp/recomp.py")],
                cwd=tmp, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_packaged_installer_import_in_isolated_python(self):

        for layout in ("macos", "linux", "windows", "appimage", "android"):
            with self.subTest(layout=layout), tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp) / layout
                for relative in package.TOOL_FILES + package.INSTALLER_FILES:
                    dest = root / relative
                    dest.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copyfile(Path(package.ROOT) / relative, dest)
                maps = Path(package.ROOT) / "recomp/builds"
                if maps.is_dir():
                    shutil.copytree(maps, root / "recomp/builds")
                result = subprocess.run(
                    [sys.executable, "-I", "-B", "-c",
                     "import runpy,sys; runpy.run_path(sys.argv[1], run_name='package_import_test')",
                     str(root / "installer/setup.py")],
                    cwd=tmp, capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_windows_python_pins_match_their_file_lists(self):
        pins = json.loads((Path(package.ROOT) / "installer/toolchains.json").read_text())["python"]
        for pin, listing, platform in (("windows", "python-windows-files.json", "windows-x86_64"),
                                       ("windows-arm64", "python-windows-arm64-files.json", "windows-arm64")):
            with self.subTest(platform=platform):
                files = json.loads((Path(package.ROOT) / "tools/release" / listing).read_text())
                self.assertEqual(files["sha256"], pins[pin]["sha256"])
                self.assertEqual(files["url"], pins[pin]["url"])
                self.assertTrue(guard.python_files("NSMBU-v1-%s.zip" % platform).endswith(listing))
                self.assertTrue(guard.python_files("/x/NSMBU-v1-%s/" % platform).endswith(listing))
                for name in ("python.exe", "pythonw.exe", "python3.dll", "python313.dll"):
                    self.assertIn(name, files["files"])

    def test_guard_still_rejects_game_tree_and_function_body(self):
        problems = []
        guard.check_entry("sdk/guest/include/game/link.h", b"/* declarations */", problems)
        self.assertTrue(any("game file tree" in problem for problem in problems))
        problems = []
        guard.check_entry("sdk/guest/include/nsmbu/bindings.h",
                          b"void f_02000000(Cpu* __restrict c) {\n", problems)
        self.assertTrue(any("recompiled game functions" in problem for problem in problems))
        problems = []
        guard.check_entry("sdk/guest/include/nsmbu/functions.h", b"0" * 32, problems)
        self.assertTrue(any("key-like" in problem for problem in problems))

if __name__ == "__main__":
    unittest.main()

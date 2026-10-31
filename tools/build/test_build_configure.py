#!/usr/bin/env python3
import os
import tempfile
import unittest

import build


class BuildConfigureTest(unittest.TestCase):
    def test_cache_get(self):
        with tempfile.TemporaryDirectory() as tmp:
            cache = os.path.join(tmp, "CMakeCache.txt")
            with open(cache, "w", encoding="utf-8") as handle:
                handle.write("CMAKE_BUILD_TYPE:STRING=Debug\n")
                handle.write("NSMBU_BUNDLED_DEPS:BOOL=ON\n")
            self.assertEqual(build.cmake_cache_get(cache, "CMAKE_BUILD_TYPE"), "Debug")
            self.assertEqual(build.cmake_cache_get(cache, "NSMBU_BUNDLED_DEPS"), "ON")
            self.assertIsNone(build.cmake_cache_get(cache, "MISSING"))

    def test_configure_needed_when_cache_missing(self):
        with tempfile.TemporaryDirectory() as tmp:
            self.assertTrue(
                build.cmake_configure_needed(
                    tmp,
                    "/src",
                    {"CMAKE_BUILD_TYPE": "Debug"},
                    {"NSMBU_BUNDLED_DEPS": "ON"},
                )
            )

    def test_configure_skipped_when_cache_matches(self):
        with tempfile.TemporaryDirectory() as tmp:
            cache = os.path.join(tmp, "CMakeCache.txt")
            with open(cache, "w", encoding="utf-8") as handle:
                handle.write("CMAKE_HOME_DIRECTORY:INTERNAL=/src\n")
                handle.write("CMAKE_BUILD_TYPE:STRING=Debug\n")
                handle.write("NSMBU_BUNDLED_DEPS:BOOL=ON\n")
            self.assertFalse(
                build.cmake_configure_needed(
                    tmp,
                    "/src",
                    {"CMAKE_BUILD_TYPE": "Debug"},
                    {"NSMBU_BUNDLED_DEPS": "ON"},
                )
            )

    def test_configure_needed_when_define_differs(self):
        with tempfile.TemporaryDirectory() as tmp:
            cache = os.path.join(tmp, "CMakeCache.txt")
            with open(cache, "w", encoding="utf-8") as handle:
                handle.write("CMAKE_HOME_DIRECTORY:INTERNAL=/src\n")
                handle.write("CMAKE_BUILD_TYPE:STRING=Debug\n")
                handle.write("NSMBU_BUNDLED_DEPS:BOOL=OFF\n")
            self.assertTrue(
                build.cmake_configure_needed(
                    tmp,
                    "/src",
                    {"CMAKE_BUILD_TYPE": "Debug"},
                    {"NSMBU_BUNDLED_DEPS": "ON"},
                )
            )

    def test_gen_sources_newer_when_hook_file_is_newer(self):
        with tempfile.TemporaryDirectory() as tmp:
            table = os.path.join(tmp, "table.c")
            hook = os.path.join(tmp, "hooks.txt")
            with open(table, "w", encoding="utf-8") as handle:
                handle.write("old\n")
            with open(hook, "w", encoding="utf-8") as handle:
                handle.write("new\n")
            os.utime(table, (1, 1))
            os.utime(hook, (2, 2))
            self.assertTrue(build.gen_sources_newer(table, [hook]))
            os.utime(table, (3, 3))
            self.assertFalse(build.gen_sources_newer(table, [hook]))
            self.assertFalse(build.gen_sources_newer(table, [os.path.join(tmp, "missing.txt")]))
            self.assertTrue(build.gen_sources_newer(os.path.join(tmp, "absent.c"), [hook]))

    def test_cmake_define_args(self):
        defines = build.cmake_define_args([], bundled_default=True, setup_gui=False)
        self.assertEqual(defines["NSMBU_BUNDLED_DEPS"], "ON")
        defines = build.cmake_define_args(["-DNSMBU_BUNDLED_DEPS=OFF"], bundled_default=True, setup_gui=True)
        self.assertEqual(defines["NSMBU_BUNDLED_DEPS"], "OFF")
        self.assertEqual(defines["NSMBU_SETUP_GUI"], "ON")


if __name__ == "__main__":
    unittest.main()

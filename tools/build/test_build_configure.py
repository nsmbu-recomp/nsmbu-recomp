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

    def test_hook_stamp_ignores_mtime(self):
        with tempfile.TemporaryDirectory() as tmp:
            hook = os.path.join(tmp, "hooks.txt")
            with open(hook, "w", encoding="utf-8") as handle:
                handle.write("024EFE58\n")
            first = build.builds.hook_inputs_stamp(tmp)
            os.utime(hook, (1, 1))
            self.assertEqual(build.builds.hook_inputs_stamp(tmp), first)
            self.assertFalse(build.hooks_need_recomp(first, first))
            self.assertTrue(build.hooks_need_recomp(None, first))
            with open(hook, "w", encoding="utf-8") as handle:
                handle.write("024F009C\n")
            self.assertTrue(build.hooks_need_recomp(first, build.builds.hook_inputs_stamp(tmp)))

    def test_cmake_define_args(self):
        defines = build.cmake_define_args([], bundled_default=True, setup_gui=False)
        self.assertEqual(defines["NSMBU_BUNDLED_DEPS"], "ON")
        defines = build.cmake_define_args(["-DNSMBU_BUNDLED_DEPS=OFF"], bundled_default=True, setup_gui=True)
        self.assertEqual(defines["NSMBU_BUNDLED_DEPS"], "OFF")
        self.assertEqual(defines["NSMBU_SETUP_GUI"], "ON")


if __name__ == "__main__":
    unittest.main()

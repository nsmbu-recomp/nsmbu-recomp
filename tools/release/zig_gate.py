#!/usr/bin/env python3
"""Compile this tree with the zig toolchain the Linux release job uses.

The pre-push hook's Clang build accepts code the release job rejects.
This is that release compile, stopped before packaging.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import host_artifacts as h

BUILD_DIR = os.path.join(h.ROOT, "build", "zig-gate")
GEN_STUB = os.path.join(h.ROOT, "build", "zig-gate-stub")


def main():
    if sys.platform != "linux":
        h.die("the zig release gate runs on Linux")
    arch = h.host_arch()
    if arch not in ("x86_64", "aarch64"):
        h.die("the zig release gate supports x86_64 and aarch64 (this machine is %s)" % arch)
    h.require_tools()
    h.check_vendors()
    zig_root, target = h.ensure_zig(h.toolchain_for_arch(arch))
    h.write_zbin(zig_root, target)
    pins = h.dependency_pins()
    sdl_prefix, _src, _lib = h.ensure_sdl3(pins, target)
    vulkan_root = h.ensure_vulkan_headers(pins)
    h.run_build("pre-push", sdl_prefix, vulkan_root, h.vulkan_library(arch), BUILD_DIR, GEN_STUB)


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Build a local release-shaped package under .tmp/ so nsmbu-setup can install for real.

Needs a Ninja Release build (package.py reads the link line with `ninja -t commands`).
Default build dir is build/release (override with NSMBU_INSTALLER_BUILD).

Prints the package directory path.
"""
import os
import platform
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.path.abspath(os.environ.get("NSMBU_INSTALLER_BUILD", os.path.join(ROOT, "build", "release")))
OUT = os.path.join(ROOT, ".tmp", "installer-out")


def cmake_compiler(build):
    cache = os.path.join(build, "CMakeCache.txt")
    if not os.path.isfile(cache):
        return ""
    with open(cache) as f:
        for line in f:
            if line.startswith("CMAKE_C_COMPILER:"):
                return line.split("=", 1)[-1].strip()
    return ""


def host_platform(build):
    system = platform.system()
    machine = platform.machine().lower()
    arch = {"amd64": "x86_64", "x64": "x86_64", "arm64": "aarch64"}.get(machine, machine)
    cc = cmake_compiler(build).lower()
    zig_built = "zig" in cc or cc.endswith("/zcc") or cc.endswith("\\zcc")
    if system == "Darwin":
        return "macos-arm64" if arch == "aarch64" else "macos-x86_64", "xcode-clt"
    if system == "Windows":
        return "windows-x86_64", "llvm-mingw-20260922"
    if zig_built:
        if arch == "aarch64":
            return "linux-aarch64", "zig-0.16.0-aarch64"
        return "linux-x86_64", "zig-0.16.0"
    return ("linux-aarch64" if arch == "aarch64" else "linux-x86_64"), "host-clang"


def require(path, what):
    if not os.path.isfile(path):
        sys.exit("missing %s (%s); run: nsmbu build --release" % (what, path))


def first_existing(paths):
    for p in paths:
        if p and os.path.isfile(p):
            return p
    return None


def main():
    plat, toolchain = host_platform(BUILD)
    exe = ".exe" if sys.platform == "win32" else ""
    require(os.path.join(BUILD, "build.ninja"), "Ninja build (package.py needs ninja -t commands)")
    require(os.path.join(BUILD, "nsmbu" + exe), "nsmbu")
    require(os.path.join(BUILD, "nsmbu-extract" + exe), "nsmbu-extract")
    require(os.path.join(BUILD, "nsmbu-setup" + exe), "nsmbu-setup")
    require(os.path.join(BUILD, "compile_commands.json"), "compile_commands.json")
    require(os.path.join(BUILD, "nsmbu-zstd.txt"), "bundled zstd marker")

    sdl = first_existing([
        os.path.join(BUILD, "_deps", "sdl3-build", "libSDL3.so.0"),
        os.path.join(BUILD, "_deps", "sdl3-build", "libSDL3.dylib"),
        os.path.join(BUILD, "_deps", "sdl3-build", "Release", "SDL3.dll"),
    ])
    vulkan = first_existing([
        "/usr/lib/x86_64-linux-gnu/libvulkan.so.1",
        "/usr/lib/aarch64-linux-gnu/libvulkan.so.1",
        "/usr/lib/libvulkan.so.1",
    ])
    licenses = [
        ("SDL3 (zlib)", first_existing([
            os.path.join(BUILD, "_deps", "sdl3-src", "LICENSE.txt"),
            os.path.join(ROOT, "runtime", "third_party", "SDL", "LICENSE.txt"),
        ])),
        ("zlib (zlib)", first_existing([os.path.join(BUILD, "_deps", "zlib-src", "LICENSE")])),
        ("LZ4 (BSD-2-Clause)", first_existing([os.path.join(BUILD, "_deps", "lz4-src", "lib", "LICENSE")])),
        ("glslang (BSD-3-Clause and others)",
         first_existing([os.path.join(BUILD, "_deps", "glslang-src", "LICENSE.txt")])),
    ]
    for name, path in licenses:
        if not path:
            sys.exit("missing license file for %s" % name)

    if os.path.isdir(OUT):
        shutil.rmtree(OUT)
    os.makedirs(OUT)

    cmd = [
        sys.executable, os.path.join(ROOT, "tools", "release", "package.py"),
        "--build", BUILD,
        "--platform", plat,
        "--version", "dev",
        "--out", OUT,
        "--toolchain", toolchain,
        "--setup-gui", os.path.join(BUILD, "nsmbu-setup" + exe),
        "--no-zip",
    ]
    if sdl:
        cmd += ["--runtime-file", sdl]
    if vulkan and plat.startswith("linux"):
        cmd += ["--linkonly-lib", vulkan]
    for name, path in licenses:
        cmd += ["--license", "%s=%s" % (name, path)]

    print("packaging local installer from %s" % BUILD, file=sys.stderr)
    subprocess.run(cmd, check=True, cwd=ROOT, stdout=sys.stderr)
    pkg = os.path.join(OUT, "NSMBU-dev-%s" % plat)
    if not os.path.isdir(pkg):
        sys.exit("package folder missing: " + pkg)
    if not os.path.isfile(os.path.join(pkg, "sdk", "include", "ppc.h")):
        sys.exit("package missing sdk/include/ppc.h")
    if not os.path.isfile(os.path.join(pkg, "sdk", "manifest.json")):
        sys.exit("package missing sdk/manifest.json")
    print(pkg)


if __name__ == "__main__":
    main()

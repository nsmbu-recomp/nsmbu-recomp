#!/usr/bin/env python3
import argparse
import hashlib
import os
import shutil
import subprocess
import sys
import tarfile
import urllib.request

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "lib"))
import build as build_mod
import slot

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
CACHE = os.path.join(ROOT, ".tmp", "toolchains")
BUILD_DIR = os.path.join(ROOT, "build", "windows")

LLVM_MINGW = {
    "url": "https://github.com/mstorsjo/llvm-mingw/releases/download/20260922/llvm-mingw-20260922-ucrt-ubuntu-22.04-x86_64.tar.xz",
    "sha256": "bb7bb7654b33d5aa8712acb837c963b2e0c56352560c76105270a3268c665c21",
    "dir": "llvm-mingw-20260922-ucrt-ubuntu-22.04-x86_64",
}

VULKAN_HEADERS = {
    "url": "https://github.com/KhronosGroup/Vulkan-Headers/archive/refs/tags/v1.4.363.tar.gz",
    "sha256": "cbaf687d3c59b9666fe080e7f8396b6b4f4d344a768ee6b337c59852f4526b68",
}

VULKAN_DEF = {
    "url": "https://raw.githubusercontent.com/KhronosGroup/Vulkan-Loader/vulkan-sdk-1.4.363.0/loader/vulkan-1.def",
    "sha256": "9ba339b7f5ee2df28487698a6840ecf095fac58b415c2c89ad9f161e9d316378",
}


def file_sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as handle:
        while True:
            chunk = handle.read(1024 * 1024)
            if not chunk:
                break
            h.update(chunk)
    return h.hexdigest()


def download(url, dest, sha256):
    if os.path.isfile(dest) and file_sha256(dest) == sha256:
        return
    os.makedirs(os.path.dirname(dest), exist_ok=True)
    tmp = dest + ".partial"
    print(f"downloading {url}", flush=True)
    urllib.request.urlretrieve(url, tmp)
    got = file_sha256(tmp)
    if got != sha256:
        os.remove(tmp)
        sys.exit(f"sha256 mismatch for {dest}: got {got}, want {sha256}")
    os.replace(tmp, dest)


def ensure_llvm_mingw():
    tdir = os.path.join(CACHE, LLVM_MINGW["dir"])
    clang = os.path.join(tdir, "bin", "x86_64-w64-mingw32-clang")
    if os.path.isfile(clang):
        return tdir
    arc = os.path.join(CACHE, os.path.basename(LLVM_MINGW["url"]))
    download(LLVM_MINGW["url"], arc, LLVM_MINGW["sha256"])
    print(f"unpacking {arc}", flush=True)
    tmp = os.path.join(CACHE, "llvm-mingw-unpack")
    shutil.rmtree(tmp, ignore_errors=True)
    os.makedirs(tmp)
    with tarfile.open(arc) as tar:
        tar.extractall(tmp)
    src = os.path.join(tmp, LLVM_MINGW["dir"])
    if not os.path.isdir(src):
        sys.exit(f"unexpected llvm-mingw layout in {arc}")
    shutil.rmtree(tdir, ignore_errors=True)
    os.replace(src, tdir)
    shutil.rmtree(tmp, ignore_errors=True)
    if not os.path.isfile(clang):
        sys.exit(f"missing {clang}")
    return tdir


def ensure_vulkan(mingw):
    vdir = os.path.join(CACHE, "vulkan-1.4.363")
    include = os.path.join(vdir, "include")
    lib = os.path.join(vdir, "libvulkan-1.a")
    if os.path.isdir(include) and os.path.isfile(lib):
        return vdir
    os.makedirs(vdir, exist_ok=True)
    headers_arc = os.path.join(CACHE, "Vulkan-Headers-v1.4.363.tar.gz")
    download(VULKAN_HEADERS["url"], headers_arc, VULKAN_HEADERS["sha256"])
    if not os.path.isdir(include):
        print(f"unpacking {headers_arc}", flush=True)
        with tarfile.open(headers_arc) as tar:
            tar.extractall(CACHE)
        src = os.path.join(CACHE, "Vulkan-Headers-1.4.363")
        if not os.path.isdir(src):
            sys.exit("unexpected Vulkan-Headers layout")
        for name in ("include", "LICENSE.md"):
            src_path = os.path.join(src, name)
            dst_path = os.path.join(vdir, name)
            if os.path.isdir(src_path):
                shutil.rmtree(dst_path, ignore_errors=True)
                shutil.copytree(src_path, dst_path)
            elif os.path.isfile(src_path):
                shutil.copy2(src_path, dst_path)
        shutil.rmtree(src, ignore_errors=True)
    def_path = os.path.join(vdir, "vulkan-1.def")
    download(VULKAN_DEF["url"], def_path, VULKAN_DEF["sha256"])
    dlltool = os.path.join(mingw, "bin", "llvm-dlltool")
    if not os.path.isfile(dlltool):
        sys.exit(f"missing {dlltool}")
    subprocess.run(
        [dlltool, "-m", "i386:x86-64", "-d", def_path, "-l", lib],
        check=True,
    )
    return vdir


def main():
    if sys.platform == "win32":
        sys.exit("tools/build/build_windows.py is for cross-building on Linux; use just build on Windows")
    parser = argparse.ArgumentParser(description="Cross-build nsmbu.exe for Windows (llvm-mingw)")
    parser.add_argument("-j", "--jobs", type=int, default=os.cpu_count() or 4)
    parser.add_argument("--release", action="store_true")
    parser.add_argument("--no-debug", action="store_true")
    parser.add_argument("--target", default="nsmbu")
    parser.add_argument("cmake_args", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    _, args.cmake_args = slot.take_slot(args.cmake_args)
    if args.cmake_args and args.cmake_args[0] == "--":
        args.cmake_args = args.cmake_args[1:]

    if not shutil.which("ninja"):
        sys.exit("ninja not found on PATH")
    if not shutil.which("cmake"):
        sys.exit("cmake not found on PATH")

    build_mod.ensure_gen_dir()
    mingw = ensure_llvm_mingw()
    vulkan = ensure_vulkan(mingw)
    bin_dir = os.path.join(mingw, "bin")
    cc = os.path.join(bin_dir, "x86_64-w64-mingw32-clang")
    cxx = os.path.join(bin_dir, "x86_64-w64-mingw32-clang++")
    rc = os.path.join(bin_dir, "x86_64-w64-mingw32-windres")

    if args.release or args.no_debug:
        build_type = "Release"
    else:
        build_type = "Debug"

    cmake = [
        "cmake",
        "-S",
        ROOT,
        "-B",
        BUILD_DIR,
        "-G",
        "Ninja",
        f"-DCMAKE_BUILD_TYPE={build_type}",
        "-DCMAKE_SYSTEM_NAME=Windows",
        "-DCMAKE_SYSTEM_PROCESSOR=x86_64",
        f"-DCMAKE_C_COMPILER={cc}",
        f"-DCMAKE_CXX_COMPILER={cxx}",
        f"-DCMAKE_RC_COMPILER={rc}",
        "-DCMAKE_EXE_LINKER_FLAGS=-static",
        "-DNSMBU_BUNDLED_DEPS=ON",
        f"-DVulkan_INCLUDE_DIR={os.path.join(vulkan, 'include')}",
        f"-DVulkan_LIBRARY={os.path.join(vulkan, 'libvulkan-1.a')}",
    ]
    cmake.extend(args.cmake_args)
    cache_entries = {
        "CMAKE_C_COMPILER": cc,
        "CMAKE_CXX_COMPILER": cxx,
        "CMAKE_BUILD_TYPE": build_type,
        "CMAKE_EXE_LINKER_FLAGS": "-static",
    }
    define_entries = build_mod.cmake_define_args(args.cmake_args, bundled_default=True, setup_gui=False)
    define_entries["Vulkan_INCLUDE_DIR"] = os.path.join(vulkan, "include")
    define_entries["Vulkan_LIBRARY"] = os.path.join(vulkan, "libvulkan-1.a")
    if build_mod.cmake_configure_needed(BUILD_DIR, ROOT, cache_entries, define_entries):
        build_mod.run_cmake(cmake, ROOT)
    subprocess.run(
        ["cmake", "--build", BUILD_DIR, "--target", args.target, "-j", str(args.jobs)],
        check=True,
        cwd=ROOT,
    )
    exe = os.path.join(BUILD_DIR, "nsmbu.exe")
    if args.target == "nsmbu" and not os.path.isfile(exe):
        sys.exit(f"build finished but missing {exe}")


if __name__ == "__main__":
    main()

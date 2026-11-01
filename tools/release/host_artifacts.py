#!/usr/bin/env python3
import argparse
import hashlib
import json
import os
import platform
import shutil
import subprocess
import sys
import tarfile
ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
WORKFLOW_ENV = os.path.join(ROOT, ".github", "workflows", "release.yml")
TOOLCHAINS_JSON = os.path.join(ROOT, "installer", "toolchains.json")
BUILD_DIR = os.path.join(ROOT, "build", "release-upload")
INSTALLER_DIR = os.path.join(ROOT, "build", "installer")
STAGE_OUT = os.path.join(ROOT, ".tmp", "installer-package-out")
GEN_STUB = os.path.join(BUILD_DIR, "gen-stub")
CACHE = os.path.join(ROOT, ".tmp", "release")
SRC_CACHE = os.path.join(CACHE, "src")
STAMPS = os.path.join(CACHE, "stamps")
ZBIN = os.path.join(CACHE, "zbin")
ZIG_CACHE = os.path.join(CACHE, "zig-cache")
LUAJIT = os.path.join(ROOT, "vendor", "luajit")

WORKFLOW_KEYS = ("SDL3_URL", "SDL3_SHA256", "VULKAN_HEADERS_URL", "VULKAN_HEADERS_SHA256")


def die(msg):
    print(msg, file=sys.stderr)
    sys.exit(1)


def log(msg):
    print("[installer] " + msg, flush=True)


def host_arch():
    machine = platform.machine().lower()
    return {"amd64": "x86_64", "x86_64": "x86_64", "arm64": "aarch64", "aarch64": "aarch64"}.get(machine)


def toolchain_for_arch(arch):
    return "zig-0.16.0-aarch64" if arch == "aarch64" else "zig-0.16.0"


def read_workflow_env():
    if not os.path.isfile(WORKFLOW_ENV):
        die("missing " + WORKFLOW_ENV)
    want = set(WORKFLOW_KEYS)
    found = {}
    with open(WORKFLOW_ENV, encoding="utf-8") as f:
        for line in f:
            for key in want:
                prefix = "  %s:" % key
                if line.startswith(prefix):
                    found[key] = line[len(prefix) :].strip()
    missing = [k for k in WORKFLOW_KEYS if k not in found or not found[k]]
    if missing:
        die("release workflow env missing: " + ", ".join(missing))
    return found


def default_version():
    r = subprocess.run(
        ["git", "describe", "--exact-match", "--tags", "HEAD"],
        cwd=ROOT,
        capture_output=True,
        text=True,
    )
    if r.returncode == 0:
        tag = r.stdout.strip()
        if tag.startswith("v"):
            return tag
    r = subprocess.run(["git", "rev-parse", "--short=8", "HEAD"], cwd=ROOT, capture_output=True, text=True, check=True)
    return "dev-" + r.stdout.strip()


def require_tools():
    missing = [name for name in ("cmake", "ninja", "curl", "python3") if not shutil.which(name)]
    if missing:
        die("missing on PATH: " + ", ".join(missing))


def vulkan_library(arch):
    primary = "/usr/lib/%s-linux-gnu/libvulkan.so.1" % arch
    if os.path.isfile(primary):
        return primary
    fallback = "/usr/lib/libvulkan.so.1"
    if os.path.isfile(fallback):
        return fallback
    die("missing Vulkan loader (install libvulkan1); looked for %s and %s" % (primary, fallback))


def stamp_ok(path, expected):
    try:
        with open(path, encoding="utf-8") as f:
            return f.read().strip() == expected
    except OSError:
        return False


def write_stamp(path, expected):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        f.write(expected + "\n")


def file_sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def curl_download(url, dest):
    os.makedirs(os.path.dirname(dest), exist_ok=True)
    log("downloading " + url)
    r = subprocess.run(["curl", "-fsSL", "-o", dest, url], cwd=ROOT)
    if r.returncode != 0:
        die("curl failed for " + url)


def check_luajit():
    if os.path.isfile(os.path.join(LUAJIT, "src", "Makefile")) and os.path.isfile(os.path.join(LUAJIT, "src", "lj_arch.h")):
        return
    die("vendor/luajit has no sources; run: git submodule update --init vendor/luajit")


def load_toolchain(toolchain_id):
    with open(TOOLCHAINS_JSON, encoding="utf-8") as f:
        toolchains = json.load(f)["toolchains"]
    if toolchain_id not in toolchains:
        die("unknown toolchain: " + toolchain_id)
    return toolchains[toolchain_id]


def ensure_zig(toolchain_id):
    t = load_toolchain(toolchain_id)
    zig_root = os.path.join(CACHE, "zig", t["dir"])
    zig_bin = os.path.join(zig_root, "zig")
    stamp = os.path.join(STAMPS, "zig-" + toolchain_id)
    if stamp_ok(stamp, t["sha256"]) and os.path.isfile(zig_bin):
        log("zig toolchain ready (%s)" % toolchain_id)
        return zig_root, t["target"]
    log("getting zig toolchain (%s)" % toolchain_id)
    os.makedirs(os.path.join(CACHE, "zig"), exist_ok=True)
    archive = os.path.join(CACHE, "zig-" + toolchain_id + ".tar.xz")
    if not (os.path.isfile(archive) and file_sha256(archive) == t["sha256"]):
        curl_download(t["url"], archive)
        if file_sha256(archive) != t["sha256"]:
            die("SHA-256 mismatch for zig toolchain download")
    if os.path.isdir(zig_root):
        shutil.rmtree(zig_root)
    log("extracting zig toolchain")
    with tarfile.open(archive, "r:xz") as tf:
        tf.extractall(path=os.path.join(CACHE, "zig"))
    if not os.path.isfile(zig_bin):
        die("zig binary missing after extract: " + zig_bin)
    write_stamp(stamp, t["sha256"])
    return zig_root, t["target"]


def write_zbin(zig_root, target):
    os.makedirs(ZBIN, exist_ok=True)
    zig = os.path.join(zig_root, "zig")
    wrappers = (
        ("zcc", 'exec "%s" cc -target %s "$@"\n' % (zig, target)),
        ("zcxx", 'exec "%s" c++ -target %s "$@"\n' % (zig, target)),
        ("zar", 'exec "%s" ar "$@"\n' % zig),
        ("zranlib", 'exec "%s" ranlib "$@"\n' % zig),
    )
    for name, body in wrappers:
        path = os.path.join(ZBIN, name)
        with open(path, "w", encoding="utf-8") as f:
            f.write("#!/bin/sh\n" + body)
        os.chmod(path, 0o755)


def sdl_source_dir(url):
    base = url.rsplit("/", 1)[-1]
    if base.endswith(".tar.gz"):
        return base[:-7]
    die("cannot infer SDL source directory from URL: " + url)


def find_sdl3_shared_lib(prefix):
    for dirpath, _, filenames in os.walk(prefix):
        if "libSDL3.so.0" in filenames:
            return os.path.join(dirpath, "libSDL3.so.0")
    return None


def ensure_sdl3(env, target):
    url = env["SDL3_URL"]
    sha = env["SDL3_SHA256"]
    want = sha + " zig " + target
    prefix = os.path.join(CACHE, "sdl3")
    stamp = os.path.join(STAMPS, "sdl3")
    src_root = os.path.join(SRC_CACHE, sdl_source_dir(url))
    license_txt = os.path.join(src_root, "LICENSE.txt")
    lib = find_sdl3_shared_lib(prefix)
    if stamp_ok(stamp, want) and lib and os.path.isfile(license_txt):
        log("SDL3 ready (cached)")
        return prefix, src_root, lib
    log("building SDL3 for the installer package")
    os.makedirs(SRC_CACHE, exist_ok=True)
    archive = os.path.join(SRC_CACHE, "sdl3.tar.gz")
    if not (os.path.isfile(archive) and file_sha256(archive) == sha):
        curl_download(url, archive)
        if file_sha256(archive) != sha:
            die("SHA-256 mismatch for SDL3 download")
    src_name = sdl_source_dir(url)
    src_root = os.path.join(SRC_CACHE, src_name)
    if not os.path.isdir(src_root):
        log("extracting SDL3 sources")
        subprocess.run(["tar", "-xzf", archive, "-C", SRC_CACHE], cwd=ROOT, check=True)
    build = os.path.join(CACHE, "sdl3-build")
    if os.path.isdir(build):
        shutil.rmtree(build)
    if os.path.isdir(prefix):
        shutil.rmtree(prefix)
    sdl_env = os.environ.copy()
    sdl_env["ZIG_GLOBAL_CACHE_DIR"] = ZIG_CACHE
    os.makedirs(ZIG_CACHE, exist_ok=True)
    log("configuring SDL3")
    subprocess.run(
        [
            "cmake",
            "-S",
            src_root,
            "-B",
            build,
            "-G",
            "Ninja",
            "-DCMAKE_BUILD_TYPE=Release",
            "-DCMAKE_C_COMPILER=" + os.path.join(ZBIN, "zcc"),
            "-DCMAKE_CXX_COMPILER=" + os.path.join(ZBIN, "zcxx"),
            "-DCMAKE_AR=" + os.path.join(ZBIN, "zar"),
            "-DCMAKE_RANLIB=" + os.path.join(ZBIN, "zranlib"),
            "-DCMAKE_C_LINKER_DEPFILE_SUPPORTED=FALSE",
            "-DCMAKE_CXX_LINKER_DEPFILE_SUPPORTED=FALSE",
            "-DSDL_SHARED=ON",
            "-DSDL_STATIC=OFF",
            "-DSDL_TESTS=OFF",
            "-DSDL_EXAMPLES=OFF",
            "-DCMAKE_INSTALL_PREFIX=" + prefix,
        ],
        cwd=ROOT,
        env=sdl_env,
        check=True,
    )
    log("compiling SDL3")
    subprocess.run(["cmake", "--build", build], cwd=ROOT, env=sdl_env, check=True)
    subprocess.run(["cmake", "--install", build], cwd=ROOT, env=sdl_env, check=True)
    lib = find_sdl3_shared_lib(prefix)
    if not lib:
        die("SDL3 install did not produce libSDL3.so.0 under " + prefix)
    if not os.path.isfile(license_txt):
        die("SDL3 source missing LICENSE.txt: " + license_txt)
    write_stamp(stamp, want)
    log("SDL3 built")
    return prefix, src_root, lib


def ensure_vulkan_headers(env):
    url = env["VULKAN_HEADERS_URL"]
    want = env["VULKAN_HEADERS_SHA256"]
    root = os.path.join(CACHE, "vulkan")
    license_md = os.path.join(root, "LICENSE.md")
    stamp = os.path.join(STAMPS, "vulkan")
    if stamp_ok(stamp, want) and os.path.isfile(license_md):
        log("Vulkan-Headers ready (cached)")
        return root
    log("getting Vulkan-Headers")
    os.makedirs(SRC_CACHE, exist_ok=True)
    archive = os.path.join(SRC_CACHE, "vulkan-headers.tar.gz")
    if not (os.path.isfile(archive) and file_sha256(archive) == want):
        curl_download(url, archive)
        if file_sha256(archive) != want:
            die("SHA-256 mismatch for Vulkan-Headers download")
    if os.path.isdir(root):
        shutil.rmtree(root)
    os.makedirs(root, exist_ok=True)
    subprocess.run(["tar", "-xzf", archive, "-C", root, "--strip-components=1"], cwd=ROOT, check=True)
    if not os.path.isfile(license_md):
        die("Vulkan-Headers extract missing LICENSE.md")
    write_stamp(stamp, want)
    return root


def drop_linker_depfile_detection():
    info = os.path.join(BUILD_DIR, "CMakeFiles")
    if not os.path.isdir(info):
        return
    for dirpath, _, filenames in os.walk(info):
        for name in filenames:
            if name not in ("CMakeCCompiler.cmake", "CMakeCXXCompiler.cmake"):
                continue
            path = os.path.join(dirpath, name)
            with open(path, encoding="utf-8") as f:
                text = f.read()
            if "LINKER_DEPFILE_SUPPORTED TRUE" not in text:
                continue
            os.remove(path)


def run_build(version, sdl_prefix, vulkan_root, vulkan_lib):
    os.makedirs(GEN_STUB, exist_ok=True)
    log("generating stub game code")
    subprocess.run([sys.executable, os.path.join(ROOT, "recomp", "stubgen.py"), GEN_STUB], cwd=ROOT, check=True)
    drop_linker_depfile_detection()
    env = os.environ.copy()
    env["ZIG_GLOBAL_CACHE_DIR"] = ZIG_CACHE
    os.makedirs(ZIG_CACHE, exist_ok=True)
    cmake_args = [
        "cmake",
        "-S",
        ROOT,
        "-B",
        BUILD_DIR,
        "-G",
        "Ninja",
        "-DCMAKE_BUILD_TYPE=Release",
        "-DNSMBU_VERSION=" + version,
        "-DGEN_DIR=" + GEN_STUB,
        "-DCMAKE_C_COMPILER=" + os.path.join(ZBIN, "zcc"),
        "-DCMAKE_CXX_COMPILER=" + os.path.join(ZBIN, "zcxx"),
        "-DCMAKE_AR=" + os.path.join(ZBIN, "zar"),
        "-DCMAKE_RANLIB=" + os.path.join(ZBIN, "zranlib"),
        "-DNSMBU_BUNDLED_DEPS=ON",
        "-DCMAKE_PREFIX_PATH=" + sdl_prefix,
        "-DNSMBU_SETUP_GUI=ON",
        "-DVulkan_INCLUDE_DIR=" + os.path.join(vulkan_root, "include"),
        "-DVulkan_LIBRARY=" + vulkan_lib,
        "-DCMAKE_C_LINKER_DEPFILE_SUPPORTED=FALSE",
        "-DCMAKE_CXX_LINKER_DEPFILE_SUPPORTED=FALSE",
    ]
    log("configuring Release build in %s" % BUILD_DIR)
    subprocess.run(cmake_args, cwd=ROOT, env=env, check=True)
    log("compiling the game and setup GUI (this is the long step)")
    subprocess.run(["cmake", "--build", BUILD_DIR], cwd=ROOT, env=env, check=True)
    log("Release build finished")


def package_release(
    version, arch, toolchain_id, out_dir, sdl_runtime_lib, vulkan_root, vulkan_lib, zig_dir, sdl_src
):
    platform_name = "linux-" + arch
    pkg_name = "NSMBU-%s-%s" % (version, platform_name)
    licenses = [
        "SDL3 (zlib)=%s" % os.path.join(sdl_src, "LICENSE.txt"),
        "Vulkan-Headers (Apache-2.0)=%s" % os.path.join(vulkan_root, "LICENSE.md"),
        "glslang (BSD-3-Clause and others)=%s"
        % os.path.join(BUILD_DIR, "_deps", "glslang-src", "LICENSE.txt"),
        "zlib (zlib)=%s" % os.path.join(BUILD_DIR, "_deps", "zlib-src", "LICENSE"),
        "LZ4 (BSD-2-Clause)=%s" % os.path.join(BUILD_DIR, "_deps", "lz4-src", "lib", "LICENSE"),
        "LLVM libc++ (Apache-2.0 WITH LLVM-exception)=%s" % os.path.join(zig_dir, "lib", "libcxx", "LICENSE.TXT"),
    ]
    args = [
        os.path.join(ROOT, "tools", "release", "package.py"),
        "--build",
        BUILD_DIR,
        "--platform",
        platform_name,
        "--version",
        version,
        "--out",
        out_dir,
        "--toolchain",
        toolchain_id,
        "--setup-gui",
        os.path.join(BUILD_DIR, "nsmbu-setup"),
        "--runtime-file",
        sdl_runtime_lib,
        "--linkonly-lib",
        vulkan_lib,
    ]
    for lic in licenses:
        args.extend(["--license", lic])
    args.append("--no-zip")
    log("packaging %s" % pkg_name)
    subprocess.run([sys.executable] + args, cwd=ROOT, check=True)
    pkg_dir = os.path.join(out_dir, pkg_name)
    launcher = os.path.join(pkg_dir, "nsmbu-launcher")
    if not os.path.isfile(launcher):
        die("package step did not write " + launcher)
    return pkg_dir


def main():
    if sys.platform != "linux":
        die("nsmbu installer builds this host's GitHub release package; only Linux x86_64 and aarch64 are supported")
    arch = host_arch()
    if arch not in ("x86_64", "aarch64"):
        die(
            "nsmbu installer builds this host's GitHub release package; only Linux x86_64 and aarch64 are supported "
            "(this machine is %s)" % platform.machine()
        )

    ap = argparse.ArgumentParser(description="Build the Linux installer package at build/installer.")
    ap.add_argument("--version", help="release version string (default: v* tag on HEAD or dev-<git sha>)")
    ap.add_argument("--no-open", action="store_true", help="stop after the package is ready")
    a = ap.parse_args()

    version = a.version if a.version else default_version()
    log("building Linux %s installer package (%s)" % (arch, version))

    require_tools()
    check_luajit()
    wf = read_workflow_env()
    vulkan_lib = vulkan_library(arch)
    toolchain_id = toolchain_for_arch(arch)

    zig_dir, target = ensure_zig(toolchain_id)
    write_zbin(zig_dir, target)
    sdl_prefix, sdl_src, sdl_runtime_lib = ensure_sdl3(wf, target)
    vulkan_root = ensure_vulkan_headers(wf)

    run_build(version, sdl_prefix, vulkan_root, vulkan_lib)

    os.makedirs(os.path.join(ROOT, ".tmp"), exist_ok=True)
    if os.path.isdir(STAGE_OUT):
        shutil.rmtree(STAGE_OUT)
    os.makedirs(STAGE_OUT, exist_ok=True)

    pkg_dir = package_release(
        version, arch, toolchain_id, STAGE_OUT, sdl_runtime_lib, vulkan_root, vulkan_lib, zig_dir, sdl_src
    )

    log("writing %s" % INSTALLER_DIR)
    if os.path.isdir(INSTALLER_DIR):
        shutil.rmtree(INSTALLER_DIR)
    os.makedirs(os.path.dirname(INSTALLER_DIR), exist_ok=True)
    shutil.move(pkg_dir, INSTALLER_DIR)
    if os.path.isdir(STAGE_OUT):
        shutil.rmtree(STAGE_OUT)

    log("checking package contents")
    subprocess.run(
        [sys.executable, os.path.join(ROOT, "tools", "release", "guard.py"), INSTALLER_DIR],
        cwd=ROOT,
        check=True,
    )

    installer_abs = os.path.abspath(INSTALLER_DIR)
    log("package ready: " + installer_abs)
    print(installer_abs)

    if a.no_open:
        return

    launcher = os.path.join(INSTALLER_DIR, "nsmbu-launcher")
    log("opening setup")
    subprocess.run([launcher, "--setup"], cwd=INSTALLER_DIR, check=True)


if __name__ == "__main__":
    main()

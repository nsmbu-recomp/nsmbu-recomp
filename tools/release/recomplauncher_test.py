#!/usr/bin/env python3
"""A local release folder RecompLauncher can open as a developer build.

The folder is build/recomplauncher. The runtime, sdk and tools come from the latest GitHub
release for this PC. The launcher program and the installer files come from this checkout.
A later run keeps data/ (the compiler and a finished setup) and replaces those files.
"""
import json
import os
import platform
import shutil
import stat
import subprocess
import sys
import zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import package

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
OUT = os.path.join(ROOT, "build", "recomplauncher")
SETUP_BUILD = os.path.join(ROOT, "build", "setup-gui")
LEGACY = (
    os.path.join(ROOT, "build", "recomplauncher-test"),
    os.path.join(ROOT, "dist", "NSMBU-recomplauncher-dev"),
)
CACHE = os.path.join(ROOT, ".tmp", "recomplauncher")


def die(msg):
    print(msg, file=sys.stderr)
    sys.exit(1)


def log(msg):
    print(msg, flush=True)


def host_platform():
    machine = {"amd64": "x86_64", "x86_64": "x86_64", "arm64": "aarch64", "aarch64": "aarch64"}.get(
        platform.machine().lower())
    system = platform.system()
    if system == "Linux" and machine in ("x86_64", "aarch64"):
        return "linux-%s" % machine, "tools/nsmbu-launcher", "nsmbu-setup"
    if system == "Darwin" and machine == "aarch64":
        return "macos-arm64", "NSMBU.app/Contents/MacOS/nsmbu-launcher", "nsmbu-setup"
    if system == "Windows" and machine in ("x86_64", "aarch64"):
        return "windows-%s" % machine, "NSMBU.exe", "nsmbu-setup.exe"
    die("no RecompLauncher test package for %s %s" % (system, platform.machine()))


def local_version():
    sha = subprocess.check_output(["git", "rev-parse", "--short", "HEAD"], cwd=ROOT, text=True).strip()
    dirty = subprocess.check_output(["git", "status", "--porcelain"], cwd=ROOT, text=True).strip()
    return "local-%s%s" % (sha, "-dirty" if dirty else "")


def have_release(path):
    return os.path.isfile(os.path.join(path, "sdk", "manifest.json")) and os.path.isdir(os.path.join(path, "installer"))


def seed_from_legacy():
    for legacy in LEGACY:
        if not os.path.isdir(legacy) or not have_release(legacy):
            continue
        os.makedirs(os.path.dirname(OUT), exist_ok=True)
        log("moving %s to build/recomplauncher" % os.path.relpath(legacy, ROOT))
        shutil.move(legacy, OUT)
        return True
    return False


def seed_from_release(asset_platform):
    if not shutil.which("gh"):
        die("gh is not on PATH, and build/recomplauncher does not exist yet")
    os.makedirs(CACHE, exist_ok=True)
    pattern = "NSMBU-*-%s.zip" % asset_platform
    log("downloading the latest %s release" % asset_platform)
    subprocess.check_call([
        "gh", "release", "download", "--repo", "nsmbu-recomp/nsmbu-recomp",
        "--pattern", pattern, "--dir", CACHE, "--clobber",
    ])
    zips = [n for n in os.listdir(CACHE) if n.endswith(".zip") and asset_platform in n and "AppImage" not in n]
    if len(zips) != 1:
        die("expected one %s zip in %s, found %s" % (pattern, CACHE, ", ".join(zips) or "none"))
    extract = os.path.join(CACHE, "extract")
    if os.path.isdir(extract):
        shutil.rmtree(extract)
    os.makedirs(extract)
    with zipfile.ZipFile(os.path.join(CACHE, zips[0])) as z:
        z.extractall(extract)
    kids = [os.path.join(extract, n) for n in os.listdir(extract)]
    src = kids[0] if len(kids) == 1 and os.path.isdir(kids[0]) else extract
    if not have_release(src):
        die("the release zip has no sdk/manifest.json")
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    shutil.move(src, OUT)


def gcc_install_dir():
    root = "/usr/lib/gcc/x86_64-linux-gnu"
    if not os.path.isdir(root):
        return ""
    hits = []
    for name in os.listdir(root):
        if os.path.isfile(os.path.join(root, name, "libstdc++.so")):
            hits.append(name)
    if not hits:
        return ""
    return os.path.join(root, sorted(hits, key=lambda s: [int(p) if p.isdigit() else p for p in s.split(".")])[-1])


def cxx_compiler():
    for name in ("clang++-19", "clang++"):
        path = shutil.which(name)
        if path:
            return path
    die("clang++ is required to build the launcher")


def ensure_setup_build():
    if os.path.isfile(os.path.join(SETUP_BUILD, "build.ninja")):
        return
    cxx = cxx_compiler()
    cc = cxx.replace("clang++", "clang")
    flags = []
    gcc = gcc_install_dir()
    if gcc:
        flags.append("--gcc-install-dir=" + gcc)
    log("configuring the launcher build")
    cmd = [
        "cmake", "-S", ROOT, "-B", SETUP_BUILD, "-G", "Ninja",
        "-DCMAKE_BUILD_TYPE=Release",
        "-DCMAKE_C_COMPILER=" + cc,
        "-DCMAKE_CXX_COMPILER=" + cxx,
        "-DNSMBU_SETUP_GUI=ON",
        "-DNSMBU_BUNDLED_DEPS=ON",
        "-DNSMBU_RENDERER=VULKAN",
        "-DCMAKE_IGNORE_PREFIX_PATH=" + os.path.expanduser("~/.local"),
    ]
    if flags:
        cmd.append("-DCMAKE_CXX_FLAGS=" + " ".join(flags))
        cmd.append("-DCMAKE_EXE_LINKER_FLAGS=" + " ".join(flags))
    subprocess.check_call(cmd, cwd=ROOT)


def build_launcher(built_name, dest_rel):
    ensure_setup_build()
    log("building the launcher")
    subprocess.check_call(["cmake", "--build", SETUP_BUILD, "--target", "nsmbu-setup"], cwd=ROOT)
    src = os.path.join(SETUP_BUILD, built_name)
    if not os.path.isfile(src):
        die("the launcher build did not write " + src)
    dest = os.path.join(OUT, dest_rel)
    os.makedirs(os.path.dirname(dest), exist_ok=True)
    shutil.copy2(src, dest)
    os.chmod(dest, os.stat(dest).st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)


def overlay(version):
    shutil.copy2(os.path.join(ROOT, "installer", "setup.py"), os.path.join(OUT, "installer", "setup.py"))
    script = os.path.join(OUT, "installer", "recomplauncher-prepare.sh")
    shutil.copy2(os.path.join(ROOT, "installer", "recomplauncher-prepare.sh"), script)
    os.chmod(script, os.stat(script).st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)
    with open(os.path.join(ROOT, "recomplauncher.json"), encoding="utf-8") as f:
        profile = json.load(f)
    profile.setdefault("prepare", {})["version"] = version
    with open(os.path.join(OUT, "recomplauncher.json"), "w", encoding="utf-8") as f:
        json.dump(profile, f, indent=2)
        f.write("\n")
    root_launcher = os.path.join(OUT, "nsmbu-launcher")
    if os.path.isfile(root_launcher):
        os.remove(root_launcher)
    desktop = os.path.join(OUT, "NSMBU.desktop")
    if os.path.isfile(desktop):
        with open(desktop, encoding="utf-8") as f:
            text = f.read()
        text = text.replace("./nsmbu-launcher", "./tools/nsmbu-launcher")
        with open(desktop, "w", encoding="utf-8") as f:
            f.write(text)
        os.chmod(desktop, 0o644)
    with open(os.path.join(OUT, "nsmbu"), "w", encoding="utf-8") as f:
        f.write(package.LINUX_PLAY)
    os.chmod(os.path.join(OUT, "nsmbu"), 0o755)


def main():
    asset_platform, launcher_rel, built_name = host_platform()
    if not have_release(OUT):
        if not seed_from_legacy():
            seed_from_release(asset_platform)
    version = local_version()
    build_launcher(built_name, launcher_rel)
    overlay(version)
    log("RecompLauncher folder: build/recomplauncher")
    log("prepare.version: " + version)


if __name__ == "__main__":
    main()

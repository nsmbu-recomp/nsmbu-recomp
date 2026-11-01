#!/usr/bin/env python3
"""Pack an already-packaged Linux release folder as one AppImage (issue #55).

usage: appimage.py PKG_DIR --out OUT_DIR [--appimagetool PATH] [--runtime-file PATH]
       appimage.py --extract-runtime APPIMAGE OUT_FILE

PKG_DIR is the NSMBU-VERSION-linux-ARCH folder tools/release/package.py produced (with
nsmbu-launcher, tools/, sdk/, portable.txt, ...). An AppImage mount is read-only, so the result
drops portable.txt: setup then keeps the game, its code, saves and settings in the per-user
folders (~/.local/share/nsmbu and ~/.config/nsmbu, setup.py default_data_dir) instead of next to
the program, and nothing is ever written beside the .AppImage.

Adds what appimagetool wants at the AppDir root (AppRun, a .desktop, the project's own icon) and
squashes it with the AppImage project's appimagetool. Output: OUT_DIR/<basename of PKG_DIR>.AppImage.

The AppImage runtime (the ELF in front of the squashfs) is pinned by SHA-256 (RUNTIME, type2-runtime
RUNTIME_COMMIT) and passed to appimagetool with --runtime-file; a file or download with another
SHA-256 fails the build. Upstream's "continuous" release moves, so the runtime files are hosted as
the release asset "appimage-runtime-<commit>" of nsmbu-recomp/ci-assets (RUNTIME_URLS). That asset must be
published once before a release build can download the runtime: runtime-x86_64, runtime-aarch64 and
type2-runtime's LICENSE. appimage.py --extract-runtime recovers the exact runtime of a built AppImage
(the ELF before the squashfs, with the per-image MD5 digest that appimagetool writes zeroed).

Updating the pin, deliberately and together with a release:
  1. take the new runtime-<arch> files (a type2-runtime build: release asset or your own build);
  2. check them (`strings runtime-x86_64 | grep commit/` names the commit; upstream signs them);
  3. put their SHA-256 in RUNTIME, RUNTIME_COMMIT to the commit, and host them where RUNTIME_URLS looks;
  4. update tools/release/appimage-runtime-licenses (components and versions the new build links);
  5. build, then check `<AppImage> --appimage-version` and run the release workflow's AppImage step.
"""
import argparse
import hashlib
import json
import os
import platform
import shutil
import struct
import subprocess
import sys
import tempfile
import urllib.request

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))

ICON = os.path.join(ROOT, "android", "app", "src", "main", "res", "mipmap-xxxhdpi", "ic_launcher.png")

APPIMAGETOOL_VERSION = "1.9.1"
APPIMAGETOOL = {
    "x86_64": ("appimagetool-x86_64.AppImage",
               "ed4ce84f0d9caff66f50bcca6ff6f35aae54ce8135408b3fa33abfc3cb384eb0"),
    "aarch64": ("appimagetool-aarch64.AppImage",
                "f0837e7448a0c1e4e650a93bb3e85802546e60654ef287576f46c71c126a9158"),
}
APPIMAGETOOL_URL = "https://github.com/AppImage/appimagetool/releases/download/%s/" % APPIMAGETOOL_VERSION

RUNTIME_COMMIT = "8f39b89"
RUNTIME = {
    "x86_64": ("runtime-x86_64", "156f4bdbde9c52d01814600013e0a273f0118dc2de98975f3c8c63427ec79074"),
    "aarch64": ("runtime-aarch64", "b4ff0030242d0c3bb12ce40541828303cf167493f4793456f0436edd6255c39d"),
}
RUNTIME_URLS = [
    "https://github.com/nsmbu-recomp/ci-assets/releases/download/appimage-runtime-%s/" % RUNTIME_COMMIT,
    "https://github.com/ZeldaWWHDRecomp/ZeldaWWHDRecomp/releases/download/appimage-runtime-%s/" % RUNTIME_COMMIT,
]

RUNTIME_LICENSES = os.path.join(ROOT, "tools", "release", "appimage-runtime-licenses")

def runtime_license_files():
    return sorted(f for f in os.listdir(RUNTIME_LICENSES) if f.startswith("AppImage-runtime-") and f.endswith(".txt"))

def add_runtime_licenses(appdir):
    licdir = os.path.join(appdir, "third-party-licenses")
    os.makedirs(licdir, exist_ok=True)
    names = runtime_license_files()
    if "AppImage-runtime-README.txt" not in names or len(names) < 2:
        sys.exit("the AppImage runtime's license texts are missing in " + RUNTIME_LICENSES)
    for name in names:
        shutil.copyfile(os.path.join(RUNTIME_LICENSES, name), os.path.join(licdir, name))
    with open(os.path.join(licdir, "README.txt"), "a") as f:
        f.write("\nThe AppImage runtime (the first part of the .AppImage file, type2-runtime %s) and what it\n"
                "links statically: AppImage-runtime-README.txt (components, versions, source), and\n" % RUNTIME_COMMIT)
        for name in names:
            if name != "AppImage-runtime-README.txt":
                f.write("  %s\n" % name)

APPRUN = """#!/bin/sh
# NSMBU (AppImage): start the release's launcher (next to tools/ and sdk/).
# This image is read-only: the setup keeps the game, its code, saves and settings in the
# per-user folders (issue #55), never beside this file.
exec "$(dirname "$0")/nsmbu-launcher" "$@"
"""

DESKTOP = """[Desktop Entry]
Type=Application
Name=NSMBU
Comment=New Super Mario Bros. U (native PC port); the first start prepares the game from your own disc dump
Exec=nsmbu-launcher
Icon=nsmbu
Categories=Game;
Terminal=false
Actions=setup;

[Desktop Action setup]
Name=Setup (repair, update, change the game)
Exec=nsmbu-launcher --setup
"""

def host_arch():
    m = platform.machine().lower()
    return {"amd64": "x86_64", "x86_64": "x86_64", "arm64": "aarch64", "aarch64": "aarch64"}.get(m)

def file_sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()

def ensure_appimagetool(given, arch):
    """The pinned appimagetool for `arch`, or the one given/installed. Returns its path."""
    if given:
        if not os.path.isfile(given):
            sys.exit("appimagetool not found: " + given)
        return given
    which = shutil.which("appimagetool")
    if which:
        return which
    if arch not in APPIMAGETOOL:
        sys.exit("no pinned appimagetool for %s (got --appimagetool to a build of it)" % arch)
    name, want = APPIMAGETOOL[arch]
    url = APPIMAGETOOL_URL + name

    dst = os.path.join(tempfile.gettempdir(), "nsmbu-appimagetool-%s-%s" % (arch, APPIMAGETOOL_VERSION))
    if not (os.path.isfile(dst) and file_sha256(dst) == want):
        print("downloading appimagetool %s (%s)" % (APPIMAGETOOL_VERSION, arch))
        tmp = dst + ".part"
        try:
            req = urllib.request.Request(url, headers={"User-Agent": "nsmbu-package"})
            with urllib.request.urlopen(req, timeout=120) as r, open(tmp, "wb") as f:
                shutil.copyfileobj(r, f)
        except OSError as e:
            sys.exit("download of appimagetool failed: %s\nFetch %s yourself and pass --appimagetool" % (e, url))
        if file_sha256(tmp) != want:
            os.remove(tmp)
            sys.exit("the download of %s is corrupt or was changed (SHA-256 mismatch)" % url)
        os.replace(tmp, dst)
        os.chmod(dst, 0o755)
    return dst

def ensure_runtime(given, arch, urls=None):
    """The pinned AppImage runtime for `arch` (RUNTIME): the file given, else a download. A file
    whose SHA-256 is not the pinned one is refused (the build fails). Returns its path."""
    if arch not in RUNTIME:
        sys.exit("no pinned AppImage runtime for %s" % arch)
    name, want = RUNTIME[arch]
    if given:
        if not os.path.isfile(given):
            sys.exit("AppImage runtime not found: " + given)
        got = file_sha256(given)
        if got != want:
            sys.exit("AppImage runtime %s has SHA-256 %s, the pinned %s (type2-runtime %s) has %s; "
                     "see RUNTIME in tools/release/appimage.py to update the pin deliberately"
                     % (given, got, name, RUNTIME_COMMIT, want))
        return given
    dst = os.path.join(tempfile.gettempdir(), "nsmbu-appimage-%s-%s" % (name, RUNTIME_COMMIT))
    if os.path.isfile(dst) and file_sha256(dst) == want:
        return dst
    tmp = dst + ".part"
    tried = []
    for base in (RUNTIME_URLS if urls is None else urls):
        url = base + name
        print("downloading the AppImage runtime (%s, type2-runtime %s) from %s" % (arch, RUNTIME_COMMIT, url))
        try:
            req = urllib.request.Request(url, headers={"User-Agent": "nsmbu-package"})
            with urllib.request.urlopen(req, timeout=120) as r, open(tmp, "wb") as f:
                shutil.copyfileobj(r, f)
        except OSError as e:
            tried.append("%s: %s" % (url, e))
            continue
        got = file_sha256(tmp)
        if got == want:
            os.replace(tmp, dst)
            return dst
        os.remove(tmp)
        tried.append("%s: SHA-256 %s, not the pinned %s" % (url, got, want))
    sys.exit("no source has the pinned AppImage runtime %s (type2-runtime %s):\n  %s\n"
             "Pass --runtime-file, or see RUNTIME in tools/release/appimage.py"
             % (name, RUNTIME_COMMIT, "\n  ".join(tried)))

def extract_runtime(appimage, out):
    """The runtime part of a built AppImage (the ELF before the squashfs) as appimagetool got it:
    the .digest_md5 section, which appimagetool fills for each image, zeroed again."""
    with open(appimage, "rb") as f:
        head = f.read(1 << 24)
    if head[:4] != b"\x7fELF" or head[4] != 2:
        sys.exit("%s does not start with a 64-bit ELF runtime" % appimage)
    little = head[5] == 1
    e = "<" if little else ">"
    shoff, = struct.unpack_from(e + "Q", head, 0x28)
    shentsize, shnum, shstrndx = struct.unpack_from(e + "HHH", head, 0x3A)
    end = shoff + shentsize * shnum
    if head[end:end + 4] not in (b"hsqs", b"sqsh"):
        sys.exit("%s: no squashfs after the runtime (offset %d)" % (appimage, end))
    runtime = bytearray(head[:end])
    headers = [struct.unpack_from(e + "IIQQQQIIQQ", head, shoff + i * shentsize) for i in range(shnum)]
    names = headers[shstrndx][4]
    for h in headers:
        start = names + h[0]
        if head[start:head.index(b"\0", start)] == b".digest_md5":
            runtime[h[4]:h[4] + h[5]] = bytes(h[5])
    with open(out, "wb") as f:
        f.write(runtime)
    print("wrote %s (%d bytes, SHA-256 %s)" % (out, len(runtime), hashlib.sha256(runtime).hexdigest()))

def package_version(pkg):
    try:
        with open(os.path.join(pkg, "sdk", "manifest.json")) as f:
            return json.load(f).get("version") or ""
    except (OSError, ValueError):
        return ""

def make_appdir(pkg, appdir):
    """PKG_DIR as an AppDir: no portable.txt (read-only mount), plus AppRun, .desktop and the icon."""

    shutil.copytree(pkg, appdir, symlinks=True,
                    ignore=shutil.ignore_patterns("data", "portable.txt", ".appimagetool-*", "*.desktop"))
    with open(os.path.join(appdir, "AppRun"), "w") as f:
        f.write(APPRUN)
    os.chmod(os.path.join(appdir, "AppRun"), 0o755)
    add_runtime_licenses(appdir)
    with open(os.path.join(appdir, "nsmbu.desktop"), "w") as f:
        f.write(DESKTOP)
    if not os.path.isfile(ICON):
        sys.exit("project icon not found: " + ICON)
    shutil.copyfile(ICON, os.path.join(appdir, "nsmbu.png"))
    shutil.copyfile(ICON, os.path.join(appdir, ".DirIcon"))

def main():
    if sys.argv[1:2] == ["--extract-runtime"]:
        if len(sys.argv) != 4:
            sys.exit("usage: appimage.py --extract-runtime APPIMAGE OUT_FILE")
        return extract_runtime(sys.argv[2], sys.argv[3])
    ap = argparse.ArgumentParser()
    ap.add_argument("pkg", help="the NSMBU-VERSION-linux-ARCH folder package.py produced")
    ap.add_argument("--out", required=True, help="where to write the .AppImage")
    ap.add_argument("--appimagetool", help="appimagetool to use (default: one on PATH, else the pinned download)")
    ap.add_argument("--runtime-file", help="the pinned AppImage runtime (RUNTIME; default: download it); "
                    "a file with another SHA-256 is refused")
    a = ap.parse_args()

    pkg = os.path.normpath(os.path.abspath(a.pkg))
    if not os.path.isfile(os.path.join(pkg, "installer", "setup.py")):
        sys.exit("%s is not a packaged release (no installer/setup.py)" % pkg)
    if not os.path.isfile(os.path.join(pkg, "nsmbu-launcher")):
        sys.exit("%s has no nsmbu-launcher (package it with --setup-gui)" % pkg)
    if not os.path.isfile(os.path.join(pkg, "portable.txt")):
        sys.exit("%s has no portable.txt: package.py always writes one (the zip is the portable "
                 "release; the AppImage drops it)" % pkg)
    arch = host_arch()
    if not arch:
        sys.exit("unsupported machine " + platform.machine())
    out = os.path.abspath(a.out)
    os.makedirs(out, exist_ok=True)
    name = os.path.basename(pkg)
    target = os.path.join(out, name + ".AppImage")
    tool = ensure_appimagetool(a.appimagetool, arch)
    runtime = ensure_runtime(a.runtime_file, arch)

    work = tempfile.mkdtemp(prefix="nsmbu-appimage-")
    try:
        appdir = os.path.join(work, name + ".AppDir")
        make_appdir(pkg, appdir)
        env = dict(os.environ)
        env.update({"ARCH": arch, "APPIMAGE_EXTRACT_AND_RUN": "1"})
        version = package_version(pkg)
        if version:
            env["VERSION"] = version
        subprocess.run([tool, "--runtime-file", runtime, appdir, target], env=env, check=True)
        if not os.path.isfile(target):
            sys.exit("appimagetool did not write " + target)
        os.chmod(target, 0o755)
    finally:
        shutil.rmtree(work, ignore_errors=True)
    print("wrote", target, os.path.getsize(target) // (1 << 20), "MiB")

if __name__ == "__main__":
    main()

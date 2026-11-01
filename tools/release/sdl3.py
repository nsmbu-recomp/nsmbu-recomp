#!/usr/bin/env python3
"""Install the SDL3 prefix built for release CI.

usage: sdl3.py install --platform PLATFORM --dest DIR

Release jobs link this prefix. They do not compile SDL. The build is the sdl3
release in nsmbu-recomp/ci-assets (that repo's SDL3 workflow). A prefix is
accepted only when its manifest matches this SDL source pin and the llvm-mingw
pins in tools/installer/toolchains.json.
"""
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tarfile
import tempfile

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
TOOLCHAINS = os.path.join(ROOT, "tools", "installer", "toolchains.json")

VERSION = "3.4.18"
UPSTREAM_SHA256 = "9c75cf16330322c217dedd2e0609f1124f1b54b8633e763467b4684d0f4334a3"
ASSETS_REPO = "nsmbu-recomp/ci-assets"
RELEASE = "sdl3"
PLATFORMS = ("linux-x86_64", "linux-aarch64", "windows-x86_64", "windows-arm64")
MINGW_KEYS = {
    "windows-x86_64": "llvm-mingw-20260922",
    "windows-arm64": "llvm-mingw-20260922-aarch64",
}


def sha256_file(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def mingw_pins(toolchains_path=TOOLCHAINS):
    with open(toolchains_path) as handle:
        toolchains = json.load(handle)["toolchains"]
    return {key: toolchains[key]["sha256"] for key in MINGW_KEYS.values()}


def archive_name(platform):
    return "sdl3-%s.tar.gz" % platform


def check_manifest(manifest, pins):
    problems = []
    if manifest.get("version") != VERSION:
        problems.append("SDL version %r, expected %s" % (manifest.get("version"), VERSION))
    if manifest.get("upstream_sha256") != UPSTREAM_SHA256:
        problems.append("SDL source pin does not match tools/release/sdl3.py")
    if manifest.get("mingw_sha256") != pins:
        problems.append("llvm-mingw pin does not match tools/installer/toolchains.json")
    assets = manifest.get("assets")
    if not isinstance(assets, dict) or set(assets) != set(PLATFORMS):
        problems.append("SDL release assets are %s" % (sorted(assets) if isinstance(assets, dict) else assets))
    else:
        for platform in PLATFORMS:
            asset = assets[platform]
            if not isinstance(asset, dict) or asset.get("name") != archive_name(platform):
                problems.append("bad asset name for " + platform)
            elif len(asset.get("sha256", "")) != 64:
                problems.append("bad sha256 for " + platform)
    if problems:
        raise SystemExit("sdl3 release in %s cannot be used:\n%s" % (ASSETS_REPO, "\n".join(problems)))


def check_prefix(dest, platform):
    cmake = os.path.join(dest, "lib", "cmake", "SDL3")
    required = [
        os.path.join(dest, "LICENSE.txt"),
        os.path.join(cmake, "SDL3Config.cmake"),
        os.path.join(cmake, "SDL3sharedTargets.cmake"),
    ]
    if platform.startswith("windows"):
        required.append(os.path.join(dest, "bin", "SDL3.dll"))
        required.append(os.path.join(cmake, "SDL3staticTargets.cmake"))
    else:
        required.append(os.path.join(dest, "lib", "libSDL3.so.0"))
    missing = [path for path in required if not os.path.exists(path)]
    if missing:
        raise SystemExit("SDL prefix %s is incomplete:\n%s" % (dest, "\n".join(missing)))


def extract_archive(archive, dest):
    os.makedirs(dest, exist_ok=True)
    with tarfile.open(archive) as tar:
        for member in tar.getmembers():
            parts = member.name.replace("\\", "/").split("/")
            if member.name.startswith("/") or ".." in parts:
                raise SystemExit("unsafe path in SDL archive: " + member.name)
        if sys.version_info >= (3, 12):
            tar.extractall(dest, filter="data")
        else:
            tar.extractall(dest)


def install_from(download_dir, platform, dest, pins):
    if platform not in PLATFORMS:
        raise SystemExit("platform must be one of " + ", ".join(PLATFORMS))
    with open(os.path.join(download_dir, "sdl3.json")) as handle:
        manifest = json.load(handle)
    check_manifest(manifest, pins)
    name = archive_name(platform)
    archive = os.path.join(download_dir, name)
    sidecar = archive + ".sha256"
    with open(sidecar) as handle:
        line = handle.read().strip().split()
    if len(line) != 2 or line[1] != name or line[0] != manifest["assets"][platform]["sha256"]:
        raise SystemExit("checksum file does not match sdl3.json for " + name)
    if sha256_file(archive) != line[0]:
        raise SystemExit("checksum does not match " + name)
    if os.path.isdir(dest):
        shutil.rmtree(dest)
    extract_archive(archive, dest)
    check_prefix(dest, platform)


def install(platform, dest):
    pins = mingw_pins()
    with tempfile.TemporaryDirectory() as download_dir:
        result = subprocess.run(
            ["gh", "release", "download", RELEASE, "--repo", ASSETS_REPO,
             "--pattern", archive_name(platform), "--pattern", archive_name(platform) + ".sha256",
             "--pattern", "sdl3.json", "--dir", download_dir, "--clobber"],
            check=False)
        if result.returncode != 0:
            raise SystemExit(
                "The sdl3 release is missing from %s. Run the SDL3 workflow in that repo." % ASSETS_REPO)
        install_from(download_dir, platform, dest, pins)


def main(argv):
    parser = argparse.ArgumentParser(description="Install the release-CI SDL3 prefix")
    parser.add_argument("command", choices=("install",))
    parser.add_argument("--platform", required=True)
    parser.add_argument("--dest", required=True)
    args = parser.parse_args(argv)
    install(args.platform, args.dest)
    print("SDL3 %s installed at %s" % (VERSION, args.dest))


if __name__ == "__main__":
    main(sys.argv[1:])

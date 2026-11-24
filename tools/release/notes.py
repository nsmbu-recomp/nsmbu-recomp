#!/usr/bin/env python3
"""Release notes: a short install summary and the matching CHANGELOG.md version.

usage: notes.py README.md VERSION [--devel COMMIT | --test NAME COMMIT] > notes.md

VERSION is the tag (v0.1) or the changelog heading (0.1).
The notes take the matching "## [0.1]" / "## 0.1" block from CHANGELOG.md next to the README.

--devel: notes for the rolling development pre-release; VERSION is the published version
(v0.1-devel.1a2b3c4d) and the changes are the "## [Unreleased]" block, the ones not released yet.

--test: notes for a named pre-release such as issue-60. VERSION is the published version
(v0.1-test.issue-60.1a2b3c4d). The changes are the same "## [Unreleased]" block.
"""
import os
import re
import sys

def changelog_version(readme, version):
    path = os.path.join(os.path.dirname(os.path.abspath(readme)), "CHANGELOG.md")
    with open(path, encoding="utf-8") as f:
        text = f.read()
    ver = version[1:] if version.startswith("v") else version
    m = re.search(
        r"^## (?:\[%s\]|%s)(?:\s+-\s+\d{4}-\d{2}-\d{2})?\s*$\n(.*?)(?=^## |\Z)"
        % (re.escape(ver), re.escape(ver)),
        text,
        re.S | re.M,
    )
    body = m.group(1) if m else ""
    body = re.split(r"^\[[^\]]+\]:\s", body, maxsplit=1, flags=re.M)[0]
    return body.strip()

def prerelease(argv):
    if not argv:
        return None, None, None
    if argv[0] == "--devel" and len(argv) == 2:
        return "devel", None, argv[1]
    if argv[0] == "--test" and len(argv) == 3:
        return "test", argv[1], argv[2]
    sys.exit("usage: notes.py README.md VERSION [--devel COMMIT | --test NAME COMMIT]")

def main():
    if len(sys.argv) < 3:
        sys.exit("usage: notes.py README.md VERSION [--devel COMMIT | --test NAME COMMIT]")
    readme, version = sys.argv[1:3]
    kind, label, commit = prerelease(sys.argv[3:])
    new = changelog_version(readme, "Unreleased" if kind else version)
    if kind == "devel":
        print("""**Development build of main @ %s: not a release.** It has the changes below that are not
released yet, and has had less testing than a release. Use it to try a fix, and include the commit
(%s) when you report a problem. The latest release is on the Releases page.
""" % (commit[:7], commit[:7]))
    elif kind == "test":
        print("""**Test build `%s` @ %s: not a release.** It is a pre-release for trying one fix.
It does not replace the development build. Include the commit (%s) when you report a problem.
The latest release is on the Releases page.
""" % (label, commit[:7], commit[:7]))
    print("""**New Super Mario Bros. U, native PC port, %s**

The Wii U game rebuilt as a native program for Windows, macOS, Linux and Android. The download has no
game files, no game code and no keys: the installer builds the game on your machine from your own copy.

| System | Start |
| --- | --- |
| Windows 10/11, x86-64 (`windows-x86_64`) or ARM64 (`windows-arm64`), Vulkan | `NSMBU.exe` |
| macOS 14+, Apple Silicon | `NSMBU.app` |
| Linux, x86-64 or arm64, glibc 2.35+, Vulkan | `nsmbu-launcher` |
| Android 13+, arm64, Vulkan | `android-arm64.apk` |

Download the zip for your system, unzip it anywhere and start that file. The first start builds the
game once and can take a few minutes; later starts go straight to the game. The game, saves and
settings stay in the folder. On Linux you can also use the single-file `.AppImage` without
unzipping (Steam Deck included); it keeps them in `~/.local/share/nsmbu` and `~/.config/nsmbu`
instead (Ubuntu 24.04+: `libfuse2t64`, or `--appimage-extract-and-run`).

On Android, install the `android-arm64.apk` (`android-x86_64` is for emulators and Chromebooks), open
the app and choose your game: it builds the game on the phone once, which takes a few minutes.

The installer takes a `.wux`/`.wud` disc image with its disc and Wii U common keys, a Cemu `.wua`
archive (no keys needed), or an extracted game folder. Supported regions and versions are in the
[Compatibility](https://github.com/nsmbu-recomp/nsmbu-recomp#compatibility) table.

The builds are unsigned. On macOS 14, right-click the app and choose **Open**. On macOS 15 or
newer, use **System Settings > Privacy & Security > Open Anyway**, and keep `NSMBU.app` inside the
unzipped folder. On Windows, choose **More info > Run anyway** when SmartScreen blocks the app.

To open the installer again, hold Shift while starting (macOS, Windows) or run
`nsmbu-launcher --setup` (Linux). Full install steps and controls are in the
[README](https://github.com/nsmbu-recomp/nsmbu-recomp#install).

## Changelog

%s
""" % (version, new or "See CHANGELOG.md."))

if __name__ == "__main__":
    main()

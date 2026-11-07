"""Refuse files that look like game content, keys or generated game code.

Runs in CI on every push and pull request, and locally as the pre-commit hook
(.githooks/pre-commit). With no arguments it checks every tracked file; with
--staged it checks the files staged for the next commit.

Standard library only, so it runs before any environment is set up.
"""

import argparse
import os
import re
import subprocess
import sys
from pathlib import Path

MAX_BYTES = 1024 * 1024

# Address maps checked in on purpose. They are larger than MAX_BYTES and hold
# addresses, not game bytes. The marker and key scan still apply.
ALLOW_LARGE = {
    "recomp/builds/eu_v0.json",
    "recomp/builds/usa_v0.json",
}

# Wii U disc images, tickets, executables and NSMBU content formats.
BLOCKED_SUFFIXES = {
    ".wud", ".wux", ".wua", ".wad", ".iso", ".rvz", ".gci", ".7z",
    ".tik", ".tmd", ".cert", ".key", ".keys",
    ".rpx", ".rpl", ".elf",
    ".app", ".h3",
    ".szs", ".sarc", ".arc", ".pack", ".yaz0",
    ".bfres", ".gtx", ".bflim", ".bflan", ".bflyt", ".bffnt", ".brfnt",
    ".bfstm", ".bfwav", ".bfsar", ".bfstp",
    ".msbt", ".msbf", ".msbp",
    ".byml", ".byaml",
}

# The game executable has no extra words in the name. Key files and shader
# caches are named, not just suffixed.
BLOCKED_NAMES = {
    "red-pro2", "red-pro2.rpx",
    "keys.txt", "common.key", "game.key",
    "title.tik", "title.tmd", "title.cert",
    "shaders.bin", "headstart.bin", "template.bin",
    "shaderprep_template.bin", "testcache.bin",
}

# recomp/recomp.py writes one code_NNN.c per chunk.
GENERATED_CODE_NAME = re.compile(r"code_\d+\.c")

# Written by the code generator into every C, header and report it emits.
# Split so this file does not contain the marker it searches for.
GENERATED_MARKER = "NSMBU_RECOMP_" + "GENERATED_FROM_GAME_CODE"

# A Cemu keys.txt line: common, disc, or a 16-hex title id, then 32 hex digits.
# A line that is only 32 hex digits is a bare key (Cemu tries each one).
KEY_LINE = re.compile(
    r"(?im)^[ \t]*(?:"
    r"(?:common(?:[ \t]+key)?|disc(?:[ \t]+key)?|wii[ \t]+u[ \t]+common[ \t]+key|[0-9a-f]{16})"
    r"[ \t]*[=:][ \t]*[0-9a-f]{32}"
    r"|[0-9a-f]{32}"
    r")[ \t]*$"
)


def list_files(staged: bool) -> list[Path]:
    if staged:
        cmd = ["git", "diff", "--cached", "--name-only", "--diff-filter=ACMR", "-z"]
    else:
        # Tracked files plus untracked ones that are not ignored: everything a commit could pick up.
        cmd = ["git", "ls-files", "-z", "--cached", "--others", "--exclude-standard"]
    out = subprocess.run(cmd, check=True, capture_output=True).stdout
    return [Path(p) for p in out.decode("utf-8").split("\0") if p]


def problems_for(path: Path, allow_large: bool = False) -> list[str]:
    """allow_large: files over MAX_BYTES are checked for the marker and keys only, not refused for size."""
    found = []
    if path.suffix.lower() in BLOCKED_SUFFIXES:
        found.append(f"blocked file type {path.suffix}")
    if path.name.lower() in BLOCKED_NAMES:
        found.append("name of a game executable, key file or shader cache")
    if GENERATED_CODE_NAME.fullmatch(path.name):
        found.append("generated game code")
    if not path.is_file():
        return found
    size = path.stat().st_size
    permitted_large = allow_large or path.as_posix() in ALLOW_LARGE
    if size > MAX_BYTES and not permitted_large:
        found.append(f"{size // 1024} KiB, over the {MAX_BYTES // 1024} KiB limit")
        return found
    data = path.read_bytes()
    if GENERATED_MARKER.encode() in data:
        found.append("generated from game code")
    if KEY_LINE.search(data.decode("utf-8", errors="ignore")):
        found.append("contains a Wii U key")
    return found


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--staged", action="store_true", help="check staged files only")
    args = parser.parse_args()

    root = subprocess.run(
        ["git", "rev-parse", "--show-toplevel"],
        check=True, capture_output=True, text=True,
    ).stdout.strip()
    os.chdir(root)

    failed = False
    for path in list_files(args.staged):
        for problem in problems_for(path):
            print(f"{path}: {problem}")
            failed = True
    if failed:
        print("\nGame content, keys and generated game code never go into the repository.")
        print("See CONTRIBUTING.md.")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())

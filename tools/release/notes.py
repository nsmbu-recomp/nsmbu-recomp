#!/usr/bin/env python3
"""Release notes: the matching CHANGELOG.md version.

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
    print("""## Changelog

%s
""" % (new or "See CHANGELOG.md."))

if __name__ == "__main__":
    main()

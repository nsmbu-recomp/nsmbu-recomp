#!/usr/bin/env python3
"""Run the host, Python, and CLI tests before a git push.

Enable once per clone: git config core.hooksPath .githooks

Git passes the remote name and URL, and the ref list on stdin.
The script builds and tests this checkout.
Playtests stay on `nsmbu test` (they need a game dump).
"""

import os
import subprocess
import sys

# nsmbu build writes build/debug. Linux CI writes build/linux.
BUILD_DIRS = ("build/debug", "build/linux")


def parse_push_lines(text):
    """Commits a pre-push hook must test. Deletes and duplicate shas are skipped."""
    shas = []
    seen = set()
    for line in text.splitlines():
        line = line.strip()
        if not line:
            continue
        parts = line.split()
        if len(parts) != 4:
            raise ValueError(f"pre-push: bad ref line: {line}")
        local_sha = parts[1]
        if local_sha and set(local_sha) == {"0"}:
            continue
        if local_sha not in seen:
            seen.add(local_sha)
            shas.append(local_sha)
    return shas


def test_commands(build_dir):
    return [
        ["ctest", "--test-dir", build_dir, "--output-on-failure"],
        [sys.executable, "-m", "unittest", "discover", "-s", "tests/py", "-p", "test_*.py"],
        ["npm", "test", "--prefix", "tools/cli"],
    ]


def git_out(args, cwd):
    result = subprocess.run(args, cwd=cwd, capture_output=True, text=True)
    if result.returncode != 0:
        err = (result.stderr or result.stdout).strip()
        raise RuntimeError(err or " ".join(args))
    return result.stdout.strip()


def repo_root():
    return git_out(["git", "rev-parse", "--show-toplevel"], cwd=None)


def head_sha(root):
    return git_out(["git", "rev-parse", "HEAD"], root)


def run(cmd, cwd):
    print("pre-push: " + " ".join(cmd), flush=True)
    return subprocess.run(cmd, cwd=cwd).returncode


def find_build_dir(root):
    for rel in BUILD_DIRS:
        if os.path.isfile(os.path.join(root, rel, "CTestTestfile.cmake")):
            return rel
    return None


def ensure_build(root):
    rel = find_build_dir(root)
    if rel is None:
        code = run([sys.executable, "tools/build/build.py", "--target", "all"], root)
        if code != 0:
            return None
        return "build/debug"
    if run(["cmake", "--build", rel], root) != 0:
        return None
    return rel


def run_tests(root):
    build_dir = ensure_build(root)
    if build_dir is None:
        return 1
    for cmd in test_commands(build_dir):
        code = run(cmd, root)
        if code != 0:
            return code
    return 0


def main(argv=None):
    argv = sys.argv[1:] if argv is None else argv
    # Git calls the hook with the remote name and URL. A bare run tests HEAD.
    hook = len(argv) > 0
    try:
        root = repo_root()
        shas = parse_push_lines(sys.stdin.read()) if hook else [head_sha(root)]
    except (ValueError, RuntimeError) as exc:
        print(exc, file=sys.stderr)
        return 1
    if not shas:
        return 0
    os.chdir(root)
    if run_tests(root) != 0:
        print("pre-push: tests failed", flush=True)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())

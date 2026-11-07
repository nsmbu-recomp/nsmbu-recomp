#!/usr/bin/env python3
"""Run the host, Python, and CLI tests before a git push.

Enable once per clone: git config core.hooksPath .githooks

Git passes the remote name and URL, and the ref list on stdin.
A clean checkout at that commit is built in place.
Any other commit is built in a worktree under .tmp/pre-push/.
Playtests stay on `nsmbu test` (they need a game dump).
"""

import os
import shutil
import subprocess
import sys

SUBMODULES = (
    "vendor/fmt",
    "vendor/imgui",
    "vendor/xxHash",
    "vendor/metal-cpp",
)

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


def tests_in_checkout(head, sha, clean):
    """True when this checkout is exactly the commit about to be pushed."""
    return clean and head == sha


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


def checkout_is_clean(root):
    status = subprocess.run(
        ["git", "status", "--porcelain"],
        cwd=root,
        capture_output=True,
        text=True,
        check=True,
    )
    return status.stdout.strip() == ""


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


def worktree_sha(path):
    if not os.path.exists(os.path.join(path, ".git")):
        return None
    result = subprocess.run(
        ["git", "rev-parse", "HEAD"],
        cwd=path,
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        return None
    return result.stdout.strip()


def remove_worktree(root, path):
    subprocess.run(["git", "worktree", "remove", "--force", path], cwd=root)
    if os.path.isdir(path):
        shutil.rmtree(path)


def prepare_tree(root, sha):
    if tests_in_checkout(head_sha(root), sha, checkout_is_clean(root)):
        print(f"pre-push: {sha[:12]} matches this checkout", flush=True)
        return root
    base = os.path.join(root, ".tmp", "pre-push")
    os.makedirs(base, exist_ok=True)
    dest = os.path.join(base, sha)
    for name in os.listdir(base):
        path = os.path.join(base, name)
        if os.path.abspath(path) != os.path.abspath(dest) and os.path.isdir(path):
            remove_worktree(root, path)
    if worktree_sha(dest) != sha:
        if os.path.isdir(dest):
            remove_worktree(root, dest)
        print(f"pre-push: worktree {dest}", flush=True)
        if run(["git", "worktree", "add", "--detach", dest, sha], root) != 0:
            return None
        if run(["git", "-C", dest, "submodule", "update", "--init", *SUBMODULES], root) != 0:
            return None
    else:
        print(f"pre-push: reuse worktree {dest}", flush=True)
    return dest


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
    for sha in shas:
        tree = prepare_tree(root, sha)
        if tree is None or run_tests(tree) != 0:
            print("pre-push: tests failed", flush=True)
            return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())

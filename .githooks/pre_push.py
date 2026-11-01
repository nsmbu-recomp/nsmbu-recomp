#!/usr/bin/env python3
import os
import shutil
import subprocess
import sys

def repo_root():
    here = os.path.dirname(os.path.abspath(__file__))
    parent = os.path.dirname(here)
    if os.path.basename(here) == "hooks" and os.path.basename(parent) == ".git":
        return os.path.dirname(parent)
    return parent

ROOT = repo_root()
sys.path.insert(0, os.path.join(ROOT, "tools", "build"))
import build

ZERO = "0" * 40
WORKTREE = os.path.join(ROOT, ".tmp", "pre-push-check")
REQUIRED_TESTS = (
    "build_coverage",
    "screenshot",
    "release_sdk_package",
    "mod_content_startup",
    "mod_cemu_startup",
    "mod_cemu_backend",
    "mod_packages",
    "mod_lua",
    "mod_content_fs_on",
    "mod_content_fs_off",
)
PATH_ALIAS_TESTS = ("mod_content_startup",)


def reject(message):
    print("pre-push: " + message, file=sys.stderr)
    sys.exit(1)


def require_tools():
    for name in ("git", "cmake", "ninja", "ctest"):
        if shutil.which(name) is None:
            reject(name + " is not on PATH")


def pushed_commits():
    found = []
    seen = set()
    for line in sys.stdin:
        parts = line.split()
        if len(parts) < 4:
            continue
        _local_ref, local_oid, remote_ref, _remote_oid = parts[:4]
        if local_oid == ZERO:
            continue
        if not remote_ref.startswith(("refs/heads/", "refs/tags/")):
            continue
        peeled = subprocess.run(
            ["git", "rev-parse", "--verify", local_oid + "^{commit}"],
            cwd=ROOT,
            check=False,
            capture_output=True,
            text=True,
        )
        if peeled.returncode != 0:
            reject("cannot resolve " + local_oid + " to a commit")
        commit = peeled.stdout.strip()
        if commit in seen:
            continue
        seen.add(commit)
        found.append((remote_ref, commit))
    return found


def tree_is_clean_at(commit):
    head = subprocess.run(
        ["git", "rev-parse", "HEAD"],
        cwd=ROOT,
        check=True,
        capture_output=True,
        text=True,
    ).stdout.strip()
    if head != commit:
        return False
    status = subprocess.run(
        ["git", "status", "--porcelain", "--untracked-files=all"],
        cwd=ROOT,
        check=True,
        capture_output=True,
        text=True,
    )
    return status.stdout == ""


def worktree_is_registered(path):
    listed = subprocess.run(
        ["git", "worktree", "list", "--porcelain"],
        cwd=ROOT,
        check=True,
        capture_output=True,
        text=True,
    )
    target = os.path.abspath(path)
    prefix = "worktree "
    for line in listed.stdout.splitlines():
        if line.startswith(prefix) and os.path.abspath(line[len(prefix):]) == target:
            return True
    return False


def ensure_worktree(commit):
    os.makedirs(os.path.dirname(WORKTREE), exist_ok=True)
    if not os.path.exists(os.path.join(WORKTREE, ".git")):
        if os.path.exists(WORKTREE):
            reject(WORKTREE + " exists and is not a git worktree")
        subprocess.run(
            ["git", "worktree", "prune"],
            cwd=ROOT,
            check=True,
        )
        if worktree_is_registered(WORKTREE):
            subprocess.run(
                ["git", "worktree", "remove", "--force", WORKTREE],
                cwd=ROOT,
                check=False,
            )
        subprocess.run(
            ["git", "worktree", "add", "--detach", WORKTREE, commit],
            cwd=ROOT,
            check=True,
        )
    else:
        subprocess.run(
            ["git", "-C", WORKTREE, "checkout", "--detach", "--force", commit],
            check=True,
        )
        subprocess.run(["git", "-C", WORKTREE, "clean", "-fd"], check=True)
    subprocess.run(
        ["git", "-C", WORKTREE, "submodule", "update", "--init", "--recursive"],
        check=True,
    )


def configure_and_test(tree):
    libdir = build.libstdcxx_libdir()
    if not libdir:
        reject("Clang needs a libstdc++ dev package")
    cc, cxx = build.clang_pair(libdir)
    if not cc:
        reject("clang++ cannot link a program")
    install = " ".join(build.gcc_install_flags(libdir))
    compile_flags = build.debug_compile_flags(install, False)
    link_flags = build.debug_link_flags(libdir, False)
    gen = os.path.join(tree, "build", "gen-stub")
    bld = os.path.join(tree, "build", "linux")
    subprocess.run(
        [sys.executable, os.path.join(tree, "tools", "recomp", "stubgen.py"), gen],
        cwd=tree,
        check=True,
    )
    subprocess.run(
        [
            "cmake",
            "-S",
            tree,
            "-B",
            bld,
            "-G",
            "Ninja",
            "-DCMAKE_C_COMPILER=" + cc,
            "-DCMAKE_CXX_COMPILER=" + cxx,
            "-DCMAKE_C_FLAGS=" + compile_flags,
            "-DCMAKE_CXX_FLAGS=" + compile_flags,
            "-DCMAKE_EXE_LINKER_FLAGS=" + link_flags,
            "-DCMAKE_SHARED_LINKER_FLAGS=" + link_flags,
            "-DCMAKE_MODULE_LINKER_FLAGS=" + link_flags,
            "-DGEN_DIR=" + gen,
            "-DNSMBU_BUNDLED_DEPS=ON",
            "-DNSMBU_SETUP_GUI=ON",
        ],
        cwd=tree,
        check=True,
    )
    subprocess.run(["cmake", "--build", bld], cwd=tree, check=True)
    listed = subprocess.run(
        ["ctest", "--test-dir", bld, "-N"],
        cwd=tree,
        check=True,
        capture_output=True,
        text=True,
    ).stdout
    for name in REQUIRED_TESTS:
        if name not in listed:
            reject("ctest did not register " + name)
    alias = "|".join(PATH_ALIAS_TESTS)
    print(
        "pre-push: path-alias tests first (" + ", ".join(PATH_ALIAS_TESTS) + ")",
        file=sys.stderr,
    )
    subprocess.run(
        [
            "ctest",
            "--test-dir",
            bld,
            "-R",
            "^(" + alias + ")$",
            "--output-on-failure",
        ],
        cwd=tree,
        check=True,
    )
    subprocess.run(
        ["ctest", "--test-dir", bld, "--output-on-failure"],
        cwd=tree,
        check=True,
    )
    subprocess.run(
        [sys.executable, os.path.join(tree, "installer", "test_setup.py")],
        cwd=tree,
        check=True,
    )
    subprocess.run(
        [sys.executable, os.path.join(tree, "tools", "recomp", "test_builds.py")],
        cwd=tree,
        check=True,
    )


def check_commit(remote_ref, commit):
    print("pre-push: " + remote_ref + " " + commit, file=sys.stderr)
    if tree_is_clean_at(commit):
        tree = ROOT
    else:
        print("pre-push: testing a clean worktree of " + commit, file=sys.stderr)
        ensure_worktree(commit)
        tree = WORKTREE
    try:
        configure_and_test(tree)
    except subprocess.CalledProcessError as exc:
        reject(commit + " failed with exit " + str(exc.returncode))


def main():
    require_tools()
    commits = pushed_commits()
    for remote_ref, commit in commits:
        check_commit(remote_ref, commit)


if __name__ == "__main__":
    main()

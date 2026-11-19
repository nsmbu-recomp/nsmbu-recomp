import os
import shutil
import subprocess
import sys

USER_NAME = "nsmbu"
LEGACY_NAME = "user"


def paths(root):
    user = os.path.join(root, USER_NAME)
    save = os.path.join(user, "save")
    return user, save


def ensure(root):
    user, save = paths(root)
    legacy = os.path.join(root, LEGACY_NAME)
    if os.path.isdir(legacy) and not os.path.isdir(user):
        shutil.move(legacy, user)
    os.makedirs(user, exist_ok=True)
    os.makedirs(save, exist_ok=True)
    return user, save


def sync_example_mods(_root):
    return


def apply_env(env, root):
    user, save = ensure(root)
    sync_example_mods(root)
    return user, save


def wine_path(wine, path):
    r = subprocess.run(
        [wine, "winepath", "-w", path],
        capture_output=True,
        text=True,
        check=False,
    )
    if r.returncode != 0:
        err = (r.stderr or r.stdout or "").strip()
        sys.exit("winepath failed for %s" % path + (": %s" % err if err else ""))
    out = (r.stdout or "").strip()
    if not out:
        sys.exit("winepath returned empty for %s" % path)
    return out


def has_save_arg(args):
    for arg in args:
        if arg == "--save" or arg.startswith("--save="):
            return True
    return False


def with_save_arg(args, save):
    if has_save_arg(args):
        return list(args)
    return ["--save", save, *args]
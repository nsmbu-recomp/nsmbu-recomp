import os
import shutil

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
    env.setdefault("NSMBU_USER_DIR", user)
    os.environ.setdefault("NSMBU_USER_DIR", env["NSMBU_USER_DIR"])
    sync_example_mods(root)
    return user, save


def has_save_arg(args):
    for arg in args:
        if arg == "--save" or arg.startswith("--save="):
            return True
    return False


def with_save_arg(args, save):
    if has_save_arg(args):
        return list(args)
    return ["--save", save, *args]

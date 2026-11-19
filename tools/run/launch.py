#!/usr/bin/env python3
import os
import shutil
import subprocess
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "lib"))
import devdata
import game_versions as gv
import runtime_flags
import slot

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

LAUNCH_DEBUG_ENV = {
    "NSMBU_PROFILE": "1",
    "NSMBU_VK_STATS": "1",
    "NSMBU_SYNC_STATS": "1",
}


def nsmbu_exe(prefer_release=False):
    debug = os.path.join(ROOT, "build", "debug")
    release = os.path.join(ROOT, "build", "release")
    rel = os.path.join(ROOT, "build", "rel")
    flat = os.path.join(ROOT, "build")
    preferred = release if prefer_release else debug
    other = debug if prefer_release else release
    for base in (preferred, other, rel, flat):
        for name in ("nsmbu", "nsmbu.exe"):
            path = os.path.join(base, name)
            if os.path.isfile(path):
                return path
    return os.path.join(preferred, "nsmbu")


def nsmbu_windows_exe(prefer_release=False):
    windows = os.path.join(ROOT, "build", "windows")
    rel = os.path.join(ROOT, "build", "rel")
    release = os.path.join(ROOT, "build", "release")
    debug = os.path.join(ROOT, "build", "debug")
    flat = os.path.join(ROOT, "build")
    if prefer_release:
        bases = (windows, rel, release, debug, flat)
    else:
        bases = (windows, debug, release, rel, flat)
    for base in bases:
        path = os.path.join(base, "nsmbu.exe")
        if os.path.isfile(path):
            return path
    return os.path.join(windows, "nsmbu.exe")


def split_launch_argv(argv):
    opts = {
        "no_debug": False,
        "gdb": False,
        "lldb": False,
        "wine": False,
        "trace": False,
        "save": None,
    }
    rest = []
    i = 0
    while i < len(argv):
        arg = argv[i]
        if arg == "--":
            rest.extend(argv[i + 1:])
            break
        if arg == "--no-debug":
            opts["no_debug"] = True
        elif arg == "--gdb":
            opts["gdb"] = True
        elif arg == "--lldb":
            opts["lldb"] = True
        elif arg == "--wine":
            opts["wine"] = True
        elif arg == "--trace":
            opts["trace"] = True
        elif arg == "--save":
            if i + 1 >= len(argv):
                sys.exit("--save requires a path")
            opts["save"] = argv[i + 1]
            i += 1
        elif arg.startswith("--save="):
            opts["save"] = arg[len("--save="):]
        else:
            rest.append(arg)
        i += 1
    return opts, rest


def main():
    opts, run_args = split_launch_argv(sys.argv[1:])
    if opts["wine"] and (opts["gdb"] or opts["lldb"]):
        sys.exit("--wine cannot be combined with --gdb or --lldb")
    slot_num, course_pack, run_args = slot.take_args(run_args)
    use_debug_env = not opts["no_debug"]
    if opts["wine"]:
        exe = nsmbu_windows_exe(prefer_release=opts["no_debug"])
        if not os.path.isfile(exe):
            sys.exit("missing %s; run: python3 tools/build/build_windows.py" % exe)
    else:
        exe = nsmbu_exe(prefer_release=opts["no_debug"])
        if not os.path.isfile(exe):
            sys.exit("missing %s; run: nsmbu build" % exe)
    try:
        game = gv.active_dir()
    except gv.GameVersionsError as e:
        sys.exit(str(e))
    if not os.path.isdir(game) or not os.path.isfile(os.path.join(game, "code", "red-pro2.rpx")):
        sys.exit("missing active dump; run: nsmbu extract / nsmbu game")
    user, save = devdata.apply_env(None, ROOT)
    if opts["save"]:
        save = os.path.abspath(opts["save"])
    wine = None
    game_arg = game
    save_arg = save
    user_arg = user
    if opts["wine"]:
        wine = shutil.which("wine")
        if not wine:
            sys.exit("wine not found on PATH")
        game_arg = devdata.wine_path(wine, game)
        save_arg = devdata.wine_path(wine, save)
        user_arg = devdata.wine_path(wine, user)
    cmd = [exe, "--game", game_arg]
    if opts["trace"]:
        cmd.append("--trace")
    if course_pack is not None:
        cmd.extend(["--open", course_pack])
    game_args = devdata.with_save_arg(run_args, save_arg)
    if not runtime_flags.has_flag(game_args, "--user-dir"):
        game_args = ["--user-dir", user_arg, *game_args]
    if use_debug_env:
        for flag in runtime_flags.args_from_env(LAUNCH_DEBUG_ENV):
            if not runtime_flags.has_flag(game_args, flag):
                cmd.append(flag)
    if slot_num is not None:
        cmd.append(slot.load_flag(slot_num, user))
    cmd.extend(game_args)
    env = runtime_flags.strip_runtime(os.environ)
    os.chdir(ROOT)
    if opts["gdb"]:
        gdb = shutil.which("gdb")
        if not gdb:
            sys.exit("gdb not found on PATH")
        raise SystemExit(subprocess.run([gdb, "--args", *cmd], env=env).returncode)
    if opts["lldb"]:
        lldb = shutil.which("lldb")
        if not lldb:
            sys.exit("lldb not found on PATH")
        raise SystemExit(subprocess.run([lldb, "--", *cmd], env=env).returncode)
    if wine is not None:
        os.execve(wine, [wine, *cmd], env)
    os.execve(exe, cmd, env)


if __name__ == "__main__":
    main()
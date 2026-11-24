#!/usr/bin/env python3
import argparse
import os
import shutil
import subprocess
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "lib"))
import devdata
import game_versions as gv
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

def wine_win_path(wine, path):
    r = subprocess.run(
        [wine, "winepath", "-w", path],
        capture_output=True,
        text=True,
        check=False,
    )
    if r.returncode != 0:
        err = (r.stderr or r.stdout or "").strip()
        sys.exit(f"winepath failed for {path}" + (f": {err}" if err else ""))
    out = (r.stdout or "").strip()
    if not out:
        sys.exit(f"winepath returned empty for {path}")
    return out

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--no-debug", action="store_true")
    parser.add_argument("--gdb", action="store_true")
    parser.add_argument("--lldb", action="store_true")
    parser.add_argument("--wine", action="store_true", help="run the Windows build under Wine")
    parser.add_argument("--trace", action="store_true", help="pass --trace to the game (HLE trace)")
    parser.add_argument("--save", help="save directory passed to the game")
    parser.add_argument("run_args", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    if args.wine and (args.gdb or args.lldb):
        sys.exit("--wine cannot be combined with --gdb or --lldb")
    slot_num, course_pack, args.run_args = slot.take_args(args.run_args)
    use_debug_env = not args.no_debug
    if args.wine:
        exe = nsmbu_windows_exe(prefer_release=args.no_debug)
        if not os.path.isfile(exe):
            sys.exit(f"missing {exe}; run: nsmbu wine")
    else:
        exe = nsmbu_exe(prefer_release=args.no_debug)
        if not os.path.isfile(exe):
            sys.exit(f"missing {exe}; run: nsmbu build")
    try:
        game = gv.active_dir()
    except gv.GameVersionsError as e:
        sys.exit(str(e))
    if not os.path.isdir(game) or not os.path.isfile(os.path.join(game, "code", "red-pro2.rpx")):
        sys.exit("missing active dump; run: nsmbu extract / nsmbu game")
    env = os.environ.copy()
    _, save = devdata.apply_env(env, ROOT)
    if args.save:
        save = os.path.abspath(args.save)
    if slot_num is not None:
        slot.apply_slot(env, slot_num)
    if use_debug_env:
        for key, value in LAUNCH_DEBUG_ENV.items():
            if key not in env:
                env[key] = value
    wine = None
    game_arg = game
    save_arg = save
    if args.wine:
        wine = shutil.which("wine")
        if not wine:
            sys.exit("wine not found on PATH")
        game_arg = wine_win_path(wine, game)
        save_arg = wine_win_path(wine, save)
        if "NSMBU_USER_DIR" in env:
            env["NSMBU_USER_DIR"] = wine_win_path(wine, env["NSMBU_USER_DIR"])
    cmd = [exe, "--game", game_arg]
    if args.trace:
        cmd.append("--trace")
    if course_pack is not None:
        cmd.extend(["--open", course_pack])
    cmd.extend(devdata.with_save_arg(args.run_args, save_arg))
    os.chdir(ROOT)
    if args.gdb:
        gdb = shutil.which("gdb")
        if not gdb:
            sys.exit("gdb not found on PATH")
        raise SystemExit(subprocess.run([gdb, "--args", *cmd], env=env).returncode)
    if args.lldb:
        lldb = shutil.which("lldb")
        if not lldb:
            sys.exit("lldb not found on PATH")
        raise SystemExit(subprocess.run([lldb, "--", *cmd], env=env).returncode)
    if wine is not None:
        os.execve(wine, [wine, *cmd], env)
    os.execve(exe, cmd, env)

if __name__ == "__main__":
    main()

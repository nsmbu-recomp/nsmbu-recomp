#!/usr/bin/env python3
import argparse
import os
import subprocess
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "lib"))
import devdata
import game_versions as gv
import slot

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
LOAD_FRAME = 700
EXIT_FRAME = 1100
LOG = os.path.join(ROOT, ".tmp", "repro", "nsmbu.log")

PROFILE_ENV = {
    "NSMBU_NO_AUDIO": "1",
    "NSMBU_PROFILE": "1",
    "NSMBU_VK_STATS": "1",
    "NSMBU_LOG_SLOW_SWAP": "30",
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


def main():
    parser = argparse.ArgumentParser(description="Run a fixed-scene profile of NSMBU.")
    parser.add_argument("--exit-at", type=int, default=EXIT_FRAME, help=f"stop at this TV frame (default {EXIT_FRAME})")
    parser.add_argument("--load-at", type=int, default=LOAD_FRAME, help=f"load the save at this TV frame (default {LOAD_FRAME})")
    parser.add_argument("--log", default=LOG, help=f"log file path (default {LOG})")
    parser.add_argument("run_args", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    slot_num, course_pack, run_args = slot.take_args(args.run_args)
    if course_pack is not None:
        sys.exit("profile needs a save slot, such as 1 (not a course pack like 1-1)")
    if slot_num is None:
        slot_num = 1
    exe = nsmbu_exe()
    if not os.path.isfile(exe):
        sys.exit(f"missing {exe}; run: nsmbu build")
    try:
        game = gv.active_dir()
    except gv.GameVersionsError as e:
        sys.exit(str(e))
    if not os.path.isdir(game) or not os.path.isfile(os.path.join(game, "code", "red-pro2.rpx")):
        sys.exit("missing active dump; run: nsmbu extract / nsmbu game")
    os.makedirs(os.path.dirname(args.log), exist_ok=True)
    env = os.environ.copy()
    _, save = devdata.apply_env(env, ROOT)
    for key, value in PROFILE_ENV.items():
        env.setdefault(key, value)
    env["NSMBU_LOG_FILE"] = args.log
    env["NSMBU_STATE_LOAD_AT"] = f"{args.load_at}:{slot_num}"
    env["NSMBU_EXIT_AT_FRAME"] = str(args.exit_at)
    path = slot.state_path(slot_num)
    if not os.path.isfile(path):
        sys.exit(f"missing {path}. Save a state in slot {slot_num}.")
    cmd = [exe, "--game", game, *devdata.with_save_arg(run_args, save)]
    print(f"[profile] log {args.log}", flush=True)
    print(f"[profile] load slot {slot_num} at TV frame {args.load_at}; exit at {args.exit_at}", flush=True)
    os.chdir(ROOT)
    raise SystemExit(subprocess.run(cmd, env=env).returncode)


if __name__ == "__main__":
    main()

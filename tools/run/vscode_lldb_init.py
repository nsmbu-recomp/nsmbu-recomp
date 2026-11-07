#!/usr/bin/env python3
import argparse
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "tools", "lib"))

import devdata
import game_versions as gv

LAUNCH_DEBUG_ENV = {
    "NSMBU_PROFILE": "1",
    "NSMBU_VK_STATS": "1",
    "NSMBU_SYNC_STATS": "1",
}


def sh_quote(s):
    return "'" + s.replace("'", "'\\''") + "'"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--no-debug-env", action="store_true")
    args = parser.parse_args()
    try:
        game = gv.active_dir()
    except gv.GameVersionsError as e:
        sys.exit(str(e))
    exe = os.path.join(ROOT, "build", "debug", "nsmbu")
    if not os.path.isfile(exe):
        sys.exit(f"missing {exe}; run: nsmbu build")
    env = os.environ.copy()
    _, save = devdata.apply_env(env, ROOT)
    child_env = {"NSMBU_USER_DIR": env["NSMBU_USER_DIR"]}
    if not args.no_debug_env:
        for key, value in LAUNCH_DEBUG_ENV.items():
            if key not in env:
                child_env[key] = value
    tmp = os.path.join(ROOT, ".tmp")
    os.makedirs(tmp, exist_ok=True)
    out = os.path.join(tmp, "vscode_lldb.lldb")
    run_args = ["--game", game, "--save", save]
    lines = [
        f"target create {sh_quote(exe)}",
        "settings set target.env-vars "
        + " ".join(sh_quote(f"{k}={v}") for k, v in sorted(child_env.items())),
        "settings set target.run-args " + " ".join(sh_quote(a) for a in run_args),
        "process launch",
    ]
    with open(out, "w", encoding="utf-8") as handle:
        handle.write("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Play a .replay file in the debug game."""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
LAUNCH = os.path.join(ROOT, "tools", "run", "launch.py")

_COURSE_EQ = re.compile(r"^course=(\S+)")
_COURSE_DIR = re.compile(r"^[#!]\s*course\s+(\S+)")


def valid_pack(pack):
    if len(pack) < 3 or pack[1] != "-" or pack[0] < "1" or pack[0] > "9":
        return False
    course = pack[2:]
    if not course.isdigit() or course[0] == "0":
        return False
    n = int(course)
    return 1 <= n <= 256


def course_from_replay(path):
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        for raw in f:
            line = raw.strip()
            if not line:
                continue
            match = _COURSE_EQ.match(line) or _COURSE_DIR.match(line)
            if match:
                return match.group(1)
            if line[0].isdigit():
                break
    stem = os.path.splitext(os.path.basename(path))[0]
    if valid_pack(stem):
        return stem
    return None


def resolve_replay(token):
    names = [token]
    if not os.path.isabs(token):
        names.append(os.path.join(ROOT, token))
        names.append(os.path.join(ROOT, "tools", "replay", token))
        if not token.endswith(".replay"):
            names.append(os.path.join(ROOT, "tools", "replay", token + ".replay"))
    for name in names:
        if os.path.isfile(name):
            return os.path.abspath(name)
    sys.exit(f"missing replay file {token}")


def has_open(args):
    for arg in args:
        if arg == "--open" or arg.startswith("--open="):
            return True
    return False


def parse_args(argv):
    uncapped = False
    replay = None
    rest = []
    i = 0
    while i < len(argv):
        arg = argv[i]
        if arg == "--":
            rest.extend(argv[i:])
            break
        if arg == "--uncapped":
            uncapped = True
        elif arg.startswith("-"):
            rest.append(arg)
        elif replay is None:
            replay = arg
        else:
            rest.append(arg)
        i += 1
    if replay is None:
        sys.exit("usage: just replay FILE [--uncapped]")
    return replay, uncapped, rest


def main(argv=None):
    replay, uncapped, rest = parse_args(sys.argv[1:] if argv is None else argv)
    path = resolve_replay(replay)
    cmd = [sys.executable, LAUNCH]
    if not has_open(rest):
        course = course_from_replay(path)
        if not course:
            sys.exit(f"{path} has no course= line; pass --open W-C")
        if not valid_pack(course):
            sys.exit(f"course {course} is not a pack like 1-1")
        cmd.extend(["--open", course])
    cmd.extend(rest)
    env = os.environ.copy()
    env["NSMBU_REPLAY_INPUT"] = path
    if uncapped:
        env["NSMBU_UNCAPPED"] = "1"
    os.execve(sys.executable, cmd, env)


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Record a perf CPU profile of the game.

The samples land in .tmp/perf/nsmbu.data with a readable report next to it.
Send .tmp/perf/nsmbu.data, .tmp/perf/report.txt and .tmp/perf/report_flat.txt.

Invoked by `nsmbu profile --cpu`.
Direct use: python3 tools/run/perf.py [--seconds N] [--attach PID] [profile.py options...]

Attachments need `perf_event_paranoid` at 1 or lower. When it is higher, the
script prompts for sudo and sets 1 for this boot.
The binary that ran must still be on disk when you (or perf) read the data:
perf matches samples to it by build ID. Keep that binary unchanged until the data is read.
"""
import argparse
import os
import shutil
import signal
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import prof_digest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join(ROOT, ".tmp", "perf")
DATA = os.path.join(OUT, "nsmbu.data")
GAME_LOG = os.path.join(OUT, "game.log")
RATE = 997
STACK_BYTES = 12288


def fail(msg, code=1):
    print("[perf] " + msg, file=sys.stderr, flush=True)
    sys.exit(code)


def check_paranoia():
    if set_paranoia_needed():
        set_paranoia()


def set_paranoia():
    path = "/proc/sys/kernel/perf_event_paranoid"
    print("perf_event_paranoid is %d; sampling needs 1 or lower." % int(open(path).read()))
    print("setting it to 1 for this boot (sudo asks below):")
    result = subprocess.run(["sudo", "-p", "sudo password: ", "tee", path],
                            input="1\n", text=True)
    if result.returncode or int(open(path).read()) > 1:
        print("setting failed. Run manually, then rerun:")
        print("    echo 1 | sudo tee %s" % path)
        sys.exit(2)


def set_paranoia_needed():
    try:
        return int(open("/proc/sys/kernel/perf_event_paranoid").read().strip()) > 1
    except OSError:
        fail("cannot read /proc/sys/kernel/perf_event_paranoid")
        return True


def find_child(profile_pid):
    """The first direct child of profile.py that is the game binary."""
    deadline = time.time() + 20
    while time.time() < deadline:
        for entry in os.listdir("/proc"):
            if not entry.isdigit():
                continue
            try:
                fields = {}
                for line in open("/proc/%s/status" % entry, errors="replace"):
                    key, sep, value = line.partition(":")
                    if sep:
                        fields[key.strip()] = value.strip()
                if fields.get("PPid", "") == str(profile_pid) and \
                        fields.get("Name", "") == "nsmbu":
                    return int(entry)
            except OSError:
                continue
        time.sleep(0.2)
    return None


def wait_loaded(log, timeout):
    """The '[savestate] Loaded slot' line means the game reached the scene."""
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            text = open(log, errors="replace").read()
        except OSError:
            text = ""
        if "Loaded slot" in text:
            return True
        if ("missing " in text and "states/slot" in text) or "run: nsmbu extract" in text:
            fail("the profile run did not start; see " + GAME_LOG)
        time.sleep(0.5)
    return False


def record(pid, seconds):
    if os.path.exists(DATA):
        os.remove(DATA)
    cmd = ["perf", "record", "-F", str(RATE), "-e", "cycles:u",
           "--call-graph", "dwarf,%d" % STACK_BYTES, "-m", "128",
           "-p", str(pid), "-o", DATA]
    print("[perf] recording %d s at %d Hz from pid %s" % (seconds, RATE, pid), flush=True)
    proc = subprocess.Popen(cmd)
    time.sleep(seconds)
    if proc.poll() is None:
        proc.send_signal(signal.SIGINT)
    try:
        proc.wait(timeout=30)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait()
    if not os.path.exists(DATA):
        fail("perf wrote no data; try fewer/different options or a longer --seconds")


def write_reports():
    subprocess.run(["perf", "report", "-i", DATA, "--stdio", "--no-children",
                    "--call-graph", "fractal,5", "--percent-limit", "0.05"],
                   stdout=open(os.path.join(OUT, "report.txt"), "w"), check=False)
    flat = subprocess.run(["perf", "report", "-i", DATA, "--stdio", "--no-children",
                           "--call-graph", "none", "--sort", "comm,dso,symbol"],
                          capture_output=True, text=True, check=False).stdout
    with open(os.path.join(OUT, "report_flat.txt"), "w") as f:
        f.write(flat)
    lines = [line for line in flat.splitlines() if line.startswith("     ") and "%" in line]
    print("[profile] cpu")
    for line in lines[:8]:
        print(line.rstrip())
    print("[profile] cpu report %s" % os.path.join(OUT, "report.txt"))


def scene_cmd(args):
    cmd = [
        sys.executable,
        os.path.join(ROOT, "tools", "run", "profile.py"),
        "--no-digest",
        "--log",
        GAME_LOG,
        "--exit-at",
        str(args.exit_at),
        "--load-at",
        str(args.load_at),
    ]
    if args.run_args:
        cmd += args.run_args
    return cmd


def run(args):
    if not shutil.which("perf"):
        fail("perf is not on PATH (linux-tools or linux-tools-generic)")

    subprocess.run(["perf", "record", "--help"], capture_output=True)
    check_paranoia()

    profile = None
    attached = args.attach is not None
    try:
        if attached:
            pid = args.attach
        else:
            os.makedirs(OUT, exist_ok=True)
            profile = subprocess.Popen(scene_cmd(args), cwd=ROOT)
            pid = find_child(profile.pid)
            if not pid:
                fail("the game process did not appear; see " + GAME_LOG)
            print("[profile] game pid %s" % pid)
            if not wait_loaded(GAME_LOG, 180):
                fail("no 'Loaded slot' in " + GAME_LOG + "; recording anyway")

            record(pid, args.seconds)
    finally:
        if profile and profile.poll() is None:
            profile.terminate()
            try:
                profile.wait(timeout=15)
            except subprocess.TimeoutExpired:
                profile.kill()

    write_reports()
    if not attached and os.path.isfile(GAME_LOG):
        sys.stdout.write(prof_digest.format_digest(open(GAME_LOG, errors="replace").read(), GAME_LOG))
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__, allow_abbrev=False)
    parser.add_argument("--seconds", type=int, default=25)
    parser.add_argument("--attach", type=int)
    parser.add_argument("--exit-at", type=int, default=1100)
    parser.add_argument("--load-at", type=int, default=700)
    parser.add_argument("run_args", nargs=argparse.REMAINDER)
    raise SystemExit(run(parser.parse_args()))


if __name__ == "__main__":
    main()

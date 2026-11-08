---
name: verify
description: "Verify an NSMBU change on Linux with the USA 1.3.0 dump, or install the debug APK. Use when finishing a change, before claiming it works, when running a playtest or boot, when a person must play a level, or when you will rerun their input from the development-build [input] log."
---

# Verify

Full inventory: `docs/developer/verification.md`.

The default check is Linux with the USA 1.3.0 dump (`usa-1.3.0`).
A USB Android device is available.
Install the debug APK on it when the change is Android-only, or when the user asked for a device check.

## Choose the check

1. Run the check in this turn when a command can show a wrong result.
   A unit test, `nsmbu test`, `nsmbu scenario` / `compare-runs`, a scene boot, a screenshot, or `nsmbu apk` counts.
   Done when that command has finished and the reply states the result.
2. Rerun input from the development-build log when a command cannot produce the controller input and a person can do it once.
   The section below is the steps.
   Done when the replay log reports that the input played.
3. Hand the rest to the user when someone has to judge a level.
   Done when the reply ends with the verification steps and calls that part unverified.

Run every check a command can settle.
Record and rerun input when that is enough to settle the rest.
Leave player steps only for what still needs a person to judge.

## Linux

Read `nsmbu game --list` and use `usa-1.3.0`.
`nsmbu game usa-1.3.0` switches the active dump.
The `cli` skill owns `--recomp`, warm builds, playtests, scenario/compare-runs, and live look.
The `scenes` skill owns a short boot to the map, a course, or the title.

## Android

`nsmbu apk` builds the release-style setup APK from the current tree and installs it on the connected device.
The APK does not contain the game.
Leave the dump on USA 1.3.0 unless the check needs another version.
Pass `--device SERIAL` when more than one device is connected.
`python3 tools/android/apk.py --list-devices` prints the serials.
Flags are in `docs/developer/cli.md`.

## Rerun input from the log

Use this when you cannot script the input and a person can perform it once.
Development builds (`nsmbu open`, `build/debug`) append `[input]` lines to `nsmbu/logs/nsmbu.log` on each pad change.
Release builds do not.

1. Ask the person to do the action in that running game.
   Done when the log has `[input]` lines for the action.
2. Copy those lines into `.tmp/repro/<name>.replay` before the next launch.
   The next start renames `nsmbu.log` to `nsmbu-previous.log`.
   Drop the `[input] ` prefix.
   Keep the latest `version=1` group and stop before `course clock reset`.
   A line that starts with `frame` is title or map input.
   Build `NSMBU_PRESS` and `NSMBU_STICK` from it as `docs/developer/replay-format.md` describes, and skip the replay file.
3. Close the live game, then play the file back on the same course:

```bash
nsmbu close
NSMBU_REPLAY_INPUT="$PWD/.tmp/repro/<name>.replay" nsmbu open 1-1
```

   Add an `exit=none` line to the file when the game should stay open after the last sample.
   Done when the new log contains `[input] replay finished` or `[input] replay stopped`, and does not contain `[input] replay diverged`.

## Player steps

End the turn with these lines filled in:

- Host: Linux on USA 1.3.0, or the app just installed on the device.
- Place: the level or screen.
- Actions: what to do.
- Pass: what should happen.

Call that part unverified until those steps are done.

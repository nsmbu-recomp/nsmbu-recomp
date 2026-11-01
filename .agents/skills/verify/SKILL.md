---
name: verify
description: "Verify an NSMBU change on Linux with the USA 1.3.0 dump, or install the debug APK on the connected Android device. Use when finishing a change, before claiming it works, when running a playtest or boot, or when a person must play through a level."
---

# Verify

Full inventory: [docs/agents/verification.md](../../../docs/agents/verification.md).

The default check is Linux with the USA 1.3.0 dump (`usa-1.3.0`).
A USB Android device is available.
Install the debug APK on it when the change is Android-only, or when the user asked for a device check.

## Choose the check

1. Run the check in this turn when a command can show a wrong result.
   A unit test, `nsmbu test`, a scene boot, a screenshot, or `nsmbu apk` counts.
   Done when that command has finished and the reply states the result.
2. Hand the rest to the user when someone has to play through a level.
   Done when the reply ends with the verification steps and calls that part unverified.

Run every check a command can settle.
Leave player steps only for what still needs a person in the level.

## Linux

Read `nsmbu game --list` and use `usa-1.3.0`.
`nsmbu game usa-1.3.0` switches the active dump.
The `nsmbu` skill owns `--recomp`, warm builds, playtests, and live look.
The `scenes` skill owns a short boot to the map, a course, or the title.

## Android

`nsmbu apk` builds the release-style setup APK from the current tree and installs it on the connected device.
The APK does not contain the game.
Leave the dump on USA 1.3.0 unless the check needs another version.
Pass `--device SERIAL` when more than one device is connected.
`python3 tools/android/apk.py --list-devices` prints the serials.
Flags are in `docs/cli.md`.

## Player steps

End the turn with these lines filled in:

- Host: Linux on USA 1.3.0, or the app just installed on the device.
- Place: the level or screen.
- Actions: what to do.
- Pass: what should happen.

Call that part unverified until those steps are done.

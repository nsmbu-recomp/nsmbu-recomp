# Findings

## Report

The reporter plays on Windows with a Ryzen 7, 32 GB RAM, and RTX 4050.
At the first Bowser fight, Bowser renders invisible but the bridge drop still works.
Later, at the Peach's Castle arena before Peach's tower, the boss fight never starts.
They restart the game and lower FPS and resolution without fixing either symptom.
The issue body cites version 0.8.4 and attaches `nsmbu.log.txt`.

## Status

GitHub issue #77 is open with the `bug` label.
Richallyson opened the thread on 2026-11-14.
IrishBruse asked them to retest on the latest version.
Richallyson replied on 2026-11-15 that both bugs persist on a newer build.
They attached `nsmbu.log.txt` and `nsmbu-previous.log.txt`.
No maintainer has analyzed the logs or closed the thread.

## Changelog

`CHANGELOG.md` does not cite issue #77.
No release entry fixes invisible Bowser or a Peach's Castle boss that never starts.
Version 0.10.1 fixes the World 1 castle cannon crash (#82), not this invisible-or-stuck report.
Version 0.8.1 fixed course tiles vanishing on Metal (#43) on macOS only.
Version 0.9.0 added async shader compile and notes an object can be missing briefly while shaders compile.

## Likely code paths

Boss fight logic lives in decomp symbols such as `Boss::` and `BossDemo::` in `recomp/headers_names.txt`.
Peach's Castle finale actors are named `PeachFinalCastleKoopaJr*` in `vendor/decomp/include/actor/ProfileInfo.h`.
`runtime/src/hooks/` has no hook registered for those boss or final-castle symbols.
Issue #72's follow-up describes the same Peach's Castle boss trigger failure.
The repo has no port fix tied to that trigger.

## Assessment

This report pairs a rendering gap with a progression stop in late-game castle fights.
The invisible boss may resemble #43, but the platform and renderer differ.
The stuck arena matches #72 and remains unaddressed in changelog or runtime hooks.
Reproduction still needs log review or a save file from the reporter.
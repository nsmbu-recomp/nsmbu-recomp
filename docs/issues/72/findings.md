# Findings

## Report

- Reporter dogcool56 runs Windows 10.
- They say some levels need gyroscope input and are nearly impossible without it.
- They name the final Bowser ship section as an example.
- In a follow-up they say every Peach's Castle level works except the last.
- There the final boss does not trigger and its cutscene does not play.

## Status

- GitHub issue 72 is closed with label `enhancement`.
- dogcool56 opened the thread on 2026-11-11; it closed the same day.
- The maintainer replied that gyro is supported and linked `docs/user/gyro.md`.
- After the boss report they asked for a save file to reproduce it.
- No further comments appear in the cached metadata.

## Changelog

- `CHANGELOG.md` does not cite issue 72.
- It does not mention gyro aiming, motion input, or the final Bowser ship.
- No changelog entry covers a Peach's Castle boss trigger failure.

## Code

- Gyro aiming lives under `runtime/src/input/motion/` (`motion.cpp`, `fusion.cpp`, `dsu.cpp`, `motion.h`).
- `docs/user/gyro.md` documents sources, settings, and how `VPADRead` gets a virtual GamePad matrix.
- `runtime/src/hooks/game_hooks.cpp` defines `site_0261864C`, which feeds gyro motion in Pro Controller mode.
- Final-castle actors appear only in decomp data (`PeachFinalCastleKoopaJr*` in `vendor/decomp/`).
- The repo has no port hook or fix tied to that boss trigger.

## Assessment

- The original request is a missing-gyro complaint; the port already implements gyro aiming.
- The follow-up is a separate progression bug, not addressed or closed in the thread.
- Open issue 77 reports a similar Peach's Castle boss that never starts.
- Reproduction still needs the reporter's save file or a new tracked issue.
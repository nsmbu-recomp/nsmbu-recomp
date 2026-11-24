# Findings

## Report

- Reporter victorkutz plays on macOS.
- They say some stages need tilt controls to move platforms.
- They believe the port has not added that feature yet.
- They give no course name, reproduction steps, or logs.

## Status

- GitHub issue 74 is open with label `enhancement`.
- victorkutz opened it on 2026-11-13; metadata updated 2026-11-22.
- IrishBruse replied the same day and linked `docs/user/gyro.md`.
- They asked whether gyro support already covers the request.
- No further comments appear in the cached metadata.

## Related issue

- Issue 72 is the same request on Windows 10.
- It is closed; the maintainer pointed the reporter to `docs/user/gyro.md`.
- That thread added a separate Peach's Castle boss report with no resolution here.

## Changelog

- `CHANGELOG.md` does not cite issue 74.
- It has no entries for gyro, motion, or tilt controls.

## Code

- Gyro input lives under `runtime/src/input/motion/` (`motion.cpp`, `fusion.cpp`, `dsu.cpp`).
- `runtime/src/hle/system_stubs.cpp` fills `VPADRead` motion fields from `motion::vpad()`.
- `runtime/src/hooks/game_hooks.cpp` defines `site_0261864C` for Pro Controller gyro.
- The macOS app reads controller motion in `runtime/src/gfx/metal/input.mm`.
- `docs/user/gyro.md` documents sources and `gyro.*` settings; default source is off.

## Assessment

- The report matches issue 72's missing-tilt wording, not a distinct macOS gap.
- The port already maps controller tilt to a virtual GamePad when a gyro source is enabled.
- The thread is open because the reporter has not confirmed whether `docs/user/gyro.md` works on Mac.
# Findings

## Report (translated)

- When the reporter activates the camera with L1, the game will not let them turn the camera off again.
- The camera mode stays stuck on screen.
- They can still move, but only closing the game restores normal play.
- The report is in Portuguese and gives no platform, build, controller model, or reproduction steps.
- The reporter does not mention RecompLauncher.

## Status

- GitHub issue **#86** is **CLOSED** with title "RecompLauncher support".
- That title does not match the body; issue **#84** uses the same title for a different request.
- Author is `pveulices`; there are no labels.
- Created 2026-11-22; last updated 2026-11-23.
- `.tmp/issues/86.md` has no comments, so the closure reason is not recorded locally.

## Changelog

- `CHANGELOG.md` does not cite issue **#86**.
- It has no entries for L1, LB, shoulder buttons, or a stuck camera toggle.
- Existing camera notes cover frame interpolation and rendering, not an L-button gameplay camera.

## Code

- Default controller mapping binds Wii U `L` to `kPadLB` ("LB / L1") in `runtime/src/input/input_map.cpp`.
- Right-stick actions in that file are labeled for camera control.
- `apply_hold_camera` in `runtime/src/hooks/interp.cpp` handles render-camera interpolation between logic steps.
- The repo has no documented fix or hook for an L-button camera that will not release.

## Open questions

- Which in-game camera mode the reporter means is not stated.
- No logs, video, or follow-up comments are available in this checkout.
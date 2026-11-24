# Findings

## Report

The reporter says game logic runs at about 59.6 steps per second while the FPS counter stays locked to the selected target.
They also see uneven frame pacing: the FPS readout looks stable, but motion is not fully smooth.
They filed against v0.8.4 on Windows with an i9-13500 and RTX 4090 in World 1-1.
They note pacing improved since v0.8.0 but both problems remain visible in v0.8.4.

## Status

GitHub issue #64 is closed.
IrishBruse closed it on 2026-11-07 with one comment: "0.9.0 has some performance improvements."
The close reason is not spelled out in the comments.

## Changelog

v0.8.4 (2026-11-03) has no entries for logic rate or frame pacing.
v0.9.0 (2026-11-07), released the same day the issue closed, lists several related fixes.
It says 60 fps on a high-refresh display keeps 60 logic steps a second.
It says a steady 60 fps no longer shows as 59.5 on the performance overlay.
It adds logic steps per second to the performance overlay and changes the default Vulkan presentation mode to Vsync (smooth).
No changelog line cites #64.

## Code

Guest logic is designed for fixed 60 Hz steps; higher draw rates add in-between pictures.
See `docs/developer/framerate.md` and `vendor/decomp/docs/frame-loop.md`.
`runtime/src/hooks/interp.cpp` drives the logic step loop (`g_logic_steps`, `hook_029C5104` path).
`runtime/src/hooks/interp_pacing.h` holds fps caps and blend pacing.
`runtime/src/overlay/overlay.cpp` and `runtime/src/overlay/perf_average.h` compute and show logic steps/s in the performance overlay.
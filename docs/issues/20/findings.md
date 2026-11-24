# Findings

## Report

The reporter sees world-map shadows flicker on and off at 120 or 144 fps.
Trees, objects, and the player shadow alternate between visible and missing.
Repro uses Settings frame rate above 60 fps on the world map.
Report is on NSMBU v0.6.3, Windows, Europe 1.3.0.

## Issue state

Issue #20 is closed.
GitHub shows no comments.
It opened and closed on 2026-10-26, the day after v0.6.3 shipped.

## Changelog

CHANGELOG [0.7.0] (2026-10-26) lists a fix for world-map shadow flicker at 120/144 fps frame interpolation.
CHANGELOG [0.8.1] repeats the fix and notes the in-between present now waits for light maps.

## Code

`vendor/decomp/docs/map-shadow-interp.md` ties flicker to light-map rebuild timing on interpolated holds.
`runtime/src/hooks/interp.cpp` hook `hook_029C4FE4` skips `procDraw_` when `g_skip_map_draw` is set.
`runtime/src/hooks/interp_pacing.h` function `skip_settled_map_draw` sets that flag on the world map.
Playtest `map-shadow-120` in `tools/cli/src/playtest/cases/map-shadow-120.ts` guards this at 120 fps.

## Related work

Issue #44 reports the same symptom on macOS at v0.8.0 and cites #20 as possibly related.
# Findings

## Report

The reporter sees world-map object shadows flicker on and off at 144 fps on macOS Apple Silicon.
Shadows alternate every frame on Acorn Plains with no user action required.
The overlay shows near-zero Game CPU on shadow-off frames and normal Game CPU on shadow-on frames.
The reporter links the pattern to interpolated in-between frames rather than random drops.
Repro uses v0.8.0, Metal renderer, and 144 fps frame interpolation on a 165 Hz display.
The symptom matches issue #20.

## Issue state

Issue #44 is closed.
It opened and closed on 2026-10-31, one day after v0.8.0 shipped.
Maintainer comment points to a possible fix in v0.8.1 and asks verification at 144 fps.
No follow-up confirmation appears in the issue thread.

## Changelog

CHANGELOG [0.7.0] lists a fix for world-map shadow flicker at 120/144 fps interpolation.
The report still reproduces the symptom on v0.8.0, so that earlier fix was incomplete on macOS Metal.
CHANGELOG [0.8.1] repeats the fix and states the in-between present now waits for light maps.
The v0.8.1 entry names 120 fps only; the maintainer comment still asks for a 144 fps check.

## Code

`vendor/decomp/docs/map-shadow-interp.md` ties flicker to `LightMapMgr` rebuild timing on hold passes versus logic steps.
`runtime/src/hooks/interp.cpp` hook `hook_029C4FE4` skips `procDraw_` when `g_skip_map_draw` is set.
`runtime/src/hooks/interp_pacing.h` function `skip_settled_map_draw` sets that flag after a hold on the world map.
Game symbols include `LightMapMgr`, `ShadowRenderObjLayer`, and `CourseSelectLayerMgr::createLayers`.
Playtest `map-shadow-120` guards this at 120 fps, not 144.

## Relation to #20

Both issues report the same world-map shadow flicker above 60 fps.
Issue #20 closed with the 0.7.0 changelog entry.
Issue #44 is a macOS Metal follow-up on v0.8.0 that cites #20 as related.
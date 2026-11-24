# Findings

## Report

The reporter sees blocky, unsmooth cloud textures on 3D-rendered course clouds.
Repro is World 1-1, the third Dragon Coin special stage.
They tried changing in-game settings and saw no change.
They run v0.9.1 on Linux with an RTX 3060 Ti.
They classify it as a graphical bug, not a crash.

## Issue state

Issue #69 is open with label `bug`.
Author Joka2010 opened it on 2026-11-10.
No maintainer or reporter comments appear in the cached thread.

## Relation to #20 and #44

This report targets in-course 3D cloud texture sharpness.
Issues #20 and #44 cover world-map shadow flicker at high frame rates.
Those fixes live in `runtime/src/hooks/interp.cpp` and `interp_pacing.h`.
They do not touch course-level cloud drawing.

## Changelog

CHANGELOG.md has no entry for issue #69 or course cloud texture quality.
The only shadow-related notes address world-map flicker at 120/144 fps.

## Code

Optional 16× anisotropic filtering is off by default in `runtime/src/gfx/vulkan/settings.cpp` (`aniso_enabled`, `set_aniso`, `NSMBU_ANISO`).
The overlay exposes it under "16x anisotropic filtering" in `runtime/src/overlay/overlay.cpp`.
`runtime/src/gfx/vulkan/draw.cpp` function `sampler()` forces anisotropy only when the user enables it and `allowAniso` is true.
`allowAniso` requires `mips > 1` and `!gpuWritten` on the bound surface.
That gate would skip anisotropy for single-mip or GPU-written cloud textures.
`runtime/src/gfx/vulkan/surfaces.cpp` `target_scale()` applies `SHADOW_SCALE` only to multi-slice depth surfaces, not course cloud textures.
The game draws course clouds through `quad_cloud_shader` and profile `block_cloud` in `vendor/decomp`.
Likely symbols include `LiftNormalModelDraw::renderSmallCloud` and `SubjectMgr::setCloudOn`.
# Findings

## Report

The reporter sees blocky, unsmooth cloud textures on 3D-rendered course clouds.
Repro is World 1-1, the third Dragon Coin special stage.
They tried changing in-game settings and saw no change.
They run v0.9.1 on Linux with an RTX 3060 Ti.
They classify it as a graphical bug, not a crash.
The bands show up at higher internal resolutions.
At the Wii U picture size the same cloud wall does not show them.

## Repro

`nsmbu cloud-room` loads `nsmbu/states/slot1.nsstate`, opens 1-1, and jumps into the pipe under the coin bridge.
The player waits at about `(6162.9, -592, 3000)`, then lands in the secret room near `(448, -608, 3000)`.
The command writes `.tmp/cloud-room.png` after the pipe iris is gone.
Close a running game first.
`nsmbu cloud-room` refuses to start while the agent socket is active.

The local settings file has `resScale=2`, `fxaa=1`, `aniso=1`, and `scaleFilter=0`.
The shot from that run is 2560×1440.
That size is the 2× color target.
On scanline y=800 the sky color `(0, 145, 255)` stays constant for about 110 pixels.
The cloud then steps to a new color.
Shorter runs of one color sit in the cloud gradient next to that hole.
Those flat runs are the bands.
They get wider as the color target gets larger.

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

`target_scale()` in `runtime/src/gfx/vulkan/surfaces.cpp` draws a one-mip color target at `res_scale()`.
A texture with more than one mip stays at 1×.
The 2× shot is the color target itself.
The bands are in that target.
They are not added by a later upscale.
`CB_COLOR_CONTROL` has `DITHER_ENABLE` at bit 2.
`GX2SetColorControl` does not set that bit.
The pipeline key in `draw.cpp` keeps the blend and ROP bits and drops bits 0–7, so the Vulkan path does not dither the 8-bit target.
At 1× a one-level step is about one pixel.
At 2× the same step covers more pixels and reads as a band.

Optional 16× anisotropic filtering is off by default in `runtime/src/gfx/vulkan/settings.cpp` (`aniso_enabled`, `set_aniso`, `NSMBU_ANISO`).
The overlay exposes it under "16x anisotropic filtering" in `runtime/src/overlay/overlay.cpp`.
`runtime/src/gfx/vulkan/draw.cpp` function `sampler()` forces anisotropy only when the user enables it and `allowAniso` is true.
`allowAniso` requires `mips > 1` and `!gpuWritten` on the bound surface.
That gate would skip anisotropy for single-mip or GPU-written cloud textures.
`runtime/src/gfx/vulkan/surfaces.cpp` `target_scale()` applies `SHADOW_SCALE` only to multi-slice depth surfaces, not course cloud textures.
The game draws course clouds through `quad_cloud_shader` and profile `block_cloud` in `vendor/decomp`.
Likely symbols include `LiftNormalModelDraw::renderSmallCloud` and `SubjectMgr::setCloudOn`.

## Fix

An 8-bit color export gets a 4×4 ordered dither when the internal resolution is above 1×.
`uf_fragCoordScale.z` carries that switch.
At 1× the extra term is zero, so the Wii U picture size stays as it was.
The rewrite is `dither_unorm8_exports` in `runtime/src/gfx/vulkan/shaders.cpp`.
It applies to `R8`, `R8G8`, and `R8G8B8A8` UNORM and sRGB targets.
Alpha is left alone.
The shader cache recipe is `WWVKSC03` so old pipelines are rebuilt.

## Check

`nsmbu cloud-room` at `resScale=2` wrote `.tmp/cloud-room-after.png` (2560×1440).
Before the dither, cloud-blue rows held one color for 16 pixels or more in 2089 runs.
The longest run was 198 pixels.
After the dither, that count is 193 and the longest run is 93 pixels.
On scanline y=760 the gradient alternates neighboring 8-bit colors one pixel at a time.
At 1×, `uf_fragCoordScale.z` is 0, so the same shaders add nothing.
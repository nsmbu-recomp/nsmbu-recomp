# Findings

## Report

The reporter sees level blocks and assets render fully transparent on macOS Apple Silicon with the Metal renderer at v0.8.0.
Collision still treats those blocks as solid, so Mario can stand on invisible geometry.
World 1-1 (Acorn Plains) is the main example, and the reporter saw the same gaps in other worlds.
Most of the picture, HUD, and performance stay normal at 120–144 fps with 1.5x internal resolution.

## Issue status

GitHub issue #43 is closed (`bug`, `Mac/IOS`).
IrishBruse noted most macOS rendering code came from the upstream project.
Ethan (via Grok) asked the reporter to try v0.8.1 for a possible Metal fix at internal resolution above 1x.
bariccattion confirmed the fix on 2026-11-03 and closed the thread.

## Changelog

v0.8.1 (2026-10-31) lists a fix: course tiles stay visible on Metal when internal resolution is above 1x.
The entry does not cite #43.
v0.8.0 shipped the day before the report, so the reporter tested the unfixed build.

## Likely code paths

Course tiles use a separate ortho path from model uniforms; see `docs/developer/framerate.md`.
`runtime/src/hooks/interp.cpp` tracks tile projection slots (`keep_tile_proj`, `publish_tile_matrix`, `g_tile_proj`).
`runtime/src/gx2/gx2_core.cpp` rescales viewport uniforms when `target_aspect_factors` applies.
`runtime/src/gfx/metal/metal_draw.mm` calls `aspect::view::scale_guest_ortho` for edge-HUD draws.
`runtime/src/gfx/metal/metal_surfaces.mm` owns `res_scale()` and `target_aspect_factors`.
`runtime/src/gfx/aspect_view.h` defines `scale_guest_ortho`.
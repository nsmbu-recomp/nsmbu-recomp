# Findings

## Report

- Reporter BernabeLaurent played v0.7.2 on Linux with Vulkan on an Intel N100 mini PC at 1080p.
- The in-game Settings menu let them raise render resolution but not go below native.
- They asked for render scale below 1.0x, for example 0.5x or 0.75x, or 360p and 540p presets.
- They also asked for a separate lower GamePad resolution, or a way to turn the GamePad off when unused.
- They wanted the image upscaled to the output resolution with bilinear or nearest-neighbor filtering.
- They cited sluggish performance on low-power hardware as the motivation.

## Status

- GitHub issue 35 is closed.
- Label: enhancement.
- Reporter opened the issue on 2026-10-28.
- Maintainer IrishBruse closed it on 2026-10-30.
- IrishBruse wrote that 30 fps and lower resolution options shipped in v0.8.0.

## Changelog

- CHANGELOG.md does not cite issue 35 by number.
- Version 0.8.0, dated 2026-10-30, lists 360p and 540p internal resolution and a 30 fps preset.
- The same release notes say the GamePad screen starts off by default.
- No changelog entry describes a separate GamePad internal resolution scale.

## Code

- `runtime/src/gfx/res_presets.h` defines `kResPresets` from 360p (0.5x) through 2160p/4K (3.0x).
- `clamp_res_scale()` enforces a 0.5x to 4.0x range.
- `runtime/src/overlay/overlay.cpp` exposes resolution presets in the Settings Graphics tab.
- `runtime/src/gfx/vulkan/surfaces.cpp` and `runtime/src/overlay/hostui.h` implement `res_scale()` and `set_res_scale()`.
- `docs/developer/vulkan.md` documents the `R` key cycling presets including 360p and 540p.
- Display settings can hide the GamePad screen, but this search found only one shared internal scale.

## Likely files and symbols

- `gfx::kResPresets`, `gfx::clamp_res_scale()`, `gfx::next_res_preset()`
- `hostui::res_scale()`, `hostui::set_res_scale()`
- `gfxvk::requested_res_scale()`, `gfxvk::set_res_scale()` in `runtime/src/gfx/vulkan/surfaces.cpp`
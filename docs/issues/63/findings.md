# Findings

## Report

The reporter runs NSMBU on Android v0.8.3 with 8 GB RAM and a Mali-G57 MC2 GPU.
They blame a MediaTek processor and ask for broader MediaTek support.
The app blocks startup because the Vulkan driver lacks `VK_KHR_dynamic_rendering`.
The driver reports Vulkan 1.1.177 and driver 32.1.0.

## Status

The issue is closed (`state=CLOSED`).
A maintainer shipped test build `issue-63`, which allows Vulkan 1.1 GPUs without dynamic rendering.
The reporter confirmed the game starts on that build.
Later comments ask for performance tweaks, movable controls, and overworld framerate; those are out of scope.
The maintainer closed the issue on 2026-11-05 because the startup failure is fixed.

## Changelog

`CHANGELOG.md` records the fix under `[0.9.0]`:
Phones and PCs on Vulkan 1.1 or 1.2 without dynamic rendering can start the game (#63).

## Code

The root cause is not the CPU; the Mali-G57 Vulkan driver lacks dynamic rendering.
`runtime/src/gfx/vulkan/rendering_path.h` defines `choose_rendering_path()`.
When neither Vulkan 1.3 core nor `VK_KHR_dynamic_rendering` is available, it selects `RenderingPath::RenderPass`.
`runtime/src/gfx/vulkan/backend.cpp` calls that logic during device setup and logs the chosen path.
`docs/developer/vulkan.md` documents the same render-pass fallback for issue #63.
Related symbols: `pass_cache.h`, `NSMBU_VK_FORCE_RENDERPASS`.

## Open follow-ups

The reporter still noted poor overworld and castle performance on the test build.
The maintainer said an overworld framerate fix would ship in a later update.
Those items were not tracked on this issue.
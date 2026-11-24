# Findings

## Report

The reporter freezes entering Story Mode on Windows 11 with an RTX 5070.
On v0.6.0, Acorn Plains shows Peach's Castle but input and gameplay stop while music and ~60 FPS continue.
The F12 overlay still responds on v0.6.0.
On v0.6.1, the freeze usually happens earlier after "Begin Story Mode."
The v0.6.1 log reports `[gx2] render thread exception: mismatched Vulkan attachments (frame work aborted)`.

## Issue status

GitHub issue #14 is closed (`bug` label).
IrishBruse pointed the reporter to v0.6.2 on 2026-10-24.
The reporter confirmed Story Mode works on v0.6.2 but filed follow-up crash, Quick Save, and F12 save-state problems.
IrishBruse closed the issue on 2026-10-27 and asked for a new report if it returns on newer builds.

## Changelog

v0.6.1 lists fixes for world-map input and the course number after the opening cinematic.
v0.6.2 says the screen no longer freezes when a frame's pictures are different sizes.
v0.6.3 addresses high-FPS world-map course-entry crashes, Quick Save persistence after restart, and save states on the world map.

## Likely code paths

Vulkan attachment size checks live in `vendor/ZeldaWWHDRecomp/runtime/src/gfx/vulkan/draw.cpp` (`exception_report::raise("mismatched Vulkan attachments")`, `aoPrivateReplay` nulls mismatched color attachments).
Render-thread abort logging is in `runtime/src/gx2/gx2_core.cpp`.
World-map and CourseSelect probing use symbols in `runtime/src/hooks/cs_probe.cpp` (`kCourseSelectMgr`, `kCourseSelectMap`).
Save-state eligibility checks are in `runtime/src/save/savestate.cpp`.
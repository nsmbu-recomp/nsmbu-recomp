# Findings

## Report

The reporter on Windows v0.6.2 crashes when entering World 1-2 from the world map.
The crash hits the loading screen as the level title banner disappears.
Steps use a new EUR base save after clearing 1-1.
Three crash logs are attached.

## Status

GitHub marks issue #15 closed.
IrishBruse asked whether the EUR ROM had update patches applied.
The reporter found the crash is not update-related.
They tied it to frame rates above 60 fps, such as 120 or 165.
Locking the port to 60 fps let 1-2 load normally.
IrishBruse closed the issue on 2026-10-27 and asked for a new report if it persists on newer builds.

## CHANGELOG

v0.6.3 (2026-10-25) lists a fix for entering another course from the world map at a high frame rate.
That release shipped one day after the root-cause comment.
Later releases mention high frame rates on loading screens but do not cite #15.

## What the code shows

The fix likely sits in frame-interpolation pacing during course setup, not in 1-2 assets.

Likely symbols:

- `runtime/src/hooks/cs_probe.cpp`: `hook_024BD86C` calls `interp::begin_course_setup()` and `interp::end_course_setup()`
- `runtime/src/hooks/interp.cpp`: `g_course_setup`, `course_setup_active()`
- `runtime/src/hooks/interp_pacing.h`: `yield_blocked_draw_lock(bool course_setup, ...)`
- `runtime/src/gx2/gx2_core.cpp`: `capped_vsync_wait_for_wake` checks `interp::course_setup_active()`

Regression coverage:

- `tools/cli/src/playtest/cases/course-enter-120.ts`: `course-enter-120` playtest at 120 fps for #15
- `docs/developer/automation.md` links that case to this issue
# Findings

## Report

The reporter on Windows v0.10.0 crashes when starting Acorn Plains-2 (World 1-2).
The crash happens on the course introduction page.
Steps go to Acorn Plains and start Acorn Plains-2.
The base game is NSMBU + NSLU (USA).
Hardware is an Intel i7-7700HQ with 16 GB RAM.
One crash log is attached on GitHub and is not in this repo.

## Status

GitHub marks issue #87 OPEN with the `bug` label.
Author operashinobi opened it on 2026-11-24.
There are no issue comments.

## Relation to #15

Issue #15 reported the same course (1-2 / The Steep Cavern) on v0.6.2.
That report hit the loading screen when the level title banner disappeared.
Comments tied #15 to frame rates above 60 fps; 60 fps let 1-2 load.
IrishBruse closed #15 on 2026-10-27.
This report does not state a frame rate and uses v0.10.0, which postdates the v0.6.3 high-fps course-enter fix.

## CHANGELOG

`CHANGELOG.md` has no entry that cites #87 or names Acorn Plains-2 / World 1-2.
v0.6.3 notes entering another course from the world map at a high frame rate no longer crashes.
Later releases mention loading cards and high frame rates on loading screens but not 1-2 specifically.
v0.10.0, v0.10.1, and v0.11.0 list no new course-enter crash fix.
`[Unreleased]` is empty.

## Likely code

The course-enter path from the world map goes through story scene loading and course setup.
Likely symbols:

- `runtime/src/mods/sdk/nsmbu.cpp`: `scene_load_story_transition`, `kSetCourseStory`
- `runtime/src/hooks/cs_probe.cpp`: `hook_024BD86C`, `nsmbu_banner_entry_arm`, `nsmbu_fill_course_banner_digits`
- `runtime/src/hooks/interp.cpp`: `begin_course_setup`, `course_setup_active`
- `tools/cli/src/playtest/cases/course-enter-120.ts`: regression for #15 at 120 fps

Without the attached crash log, the repo does not show whether this matches #15, Intel graphics crashes (#60), or a separate cause.
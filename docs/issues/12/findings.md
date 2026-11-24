# Findings

## Report

The reporter says loading fails after they quicksave during play.
They finished course 1-1, saved, then tried to load.
They filed this on v0.6.0 on Nobara Linux with an Intel i7-6700K and RTX 4070.
They attached setup, crash, and nsmbu logs plus a screenshot titled "Loading Game is impossible?"

## GitHub status

The issue is **closed** as of 2026-10-27.
IrishBruse pointed the reporter at v0.6.1, then v0.6.2, then v0.6.3 with extra save-state debug logging.
The reporter retested on v0.6.1 and v0.6.2 and said the problem remained on AMD hardware too.
mattzildjian reported the same symptom when loading a v0.5.0 save after updating to v0.6.0+ on Windows.
The maintainer closed it without a confirmed fix on v0.6.3 and asked for a new report if newer builds still fail.

## Terminology

"Quicksave" in the steps may mean Shift+F1–F5 portable save states or the in-game Quick Save menu entry.
The title and the v0.5.0→0.6.0 comment may instead point at normal save-file load.
The issue body does not name F-keys, slots, or `rp_savedata.dat`.

## Changelog

`CHANGELOG.md` has no `#12` entry.
v0.6.3 lists Quick Save persistence after quit, save states on the map and in courses, and refusal of a portable state that does not match the active save slot.
v0.6.1 lists course-load and quit-to-map crash fixes only.
Those releases shipped the same week as this report; the reporter said v0.6.1 and v0.6.2 did not help.

## Likely code paths

Portable save states: `runtime/src/save/savestate.cpp` (`service_portable_load`, `apply_portable`, `copy_savedata`, `restore_savedata`).
Validation and checksum checks: `runtime/src/save/portable_state.cpp` (`file_slot`, `save file does not match this state`).
Game save I/O: `vendor/decomp/src/system/SaveMgr.cpp` (`loadSavedata`, `startReadGame`, quick-save hooks in `SaveMgr.h`).
Playtest helpers: `tools/cli/src/playtest/helpers.ts` (`quickSaveHere`).

## Assessment

The report is reproducible on multiple machines and OSes but was never confirmed fixed before close.
Current changelog and code show active work on save-state load guards and Quick Save disk persistence around v0.6.3.
Without the attached logs, this file cannot pin the failure to a specific message, crash site, or save kind.
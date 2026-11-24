# Findings

## Report (English)

The reporter asks to fix movement controls.
They say movement does not work in situations that require it.
They also ask for multiplayer in a future update.
They filed on Windows with port v0.7.2 and USA game v1.3.0.
Steps to reproduce, location, and useful logs are all missing.

## Status

GitHub marks issue #39 as CLOSED.
Labels are `bug` and `needs more info`.
IrishBruse asked for clearer examples on 2026-10-29.
They asked whether the problem is controller input or keyboard input.
The reporter did not reply before the issue closed.

## Changelog and code

`CHANGELOG.md` has no entry that references #39.
Recent notes cover controller menus, keyboard bindings, dead zones, and up to four local players.
None of those notes match this vague movement report.
Multiplayer is not mentioned in the changelog.

Likely input code lives under `runtime/src/input/` (`input_map.cpp`, `input.h`) and `runtime/src/platform/input_sdl.cpp`.
Game-side input routing may also pass through `vendor/decomp/src/input/InputMgr.cpp`.

## Assessment

The report is too thin to tie to a specific bug or fix.
Without repro steps or a follow-up reply, the closure as needs more info is reasonable.
No repo evidence shows this report was already resolved.
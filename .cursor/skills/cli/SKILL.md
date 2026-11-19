---
name: cli
description: "nsmbu CLI playtests, benches, boots, builds, dumps, settings.ini, and NSMBU_* variables. Use when running nsmbu test, bench, scenario, compare-runs, a warm build, a dump switch, a pose or interpolation check, nsmbu settings, nsmbu status, or nsmbu log."
---

# nsmbu CLI

TypeScript CLI under `tools/cli/`.
Run `nsmbu -s <id> --help` for the command list.
Pass `-s <id>` on every `nsmbu` command.

## Docs

Read `docs/developer/automation.md` for playtests, benches, course replay, `nsmbu scenario` / `compare-runs`, and boot recipes.
Frame time, CPU samples, GPU timestamp intervals, and world-map benches are the `profile` skill.
Development builds write `[input]` pad lines.
The line layout is in `docs/developer/replay-format.md`.
Read `docs/developer/cli.md` for install, build/launch, live control, and `nsmbu settings`.
Read `docs/developer/env.md` when you add, rename, or read an `NSMBU_*` variable.
Read Frame interpolation in `docs/developer/env.md` before changing a model pose, a camera picture, or interpolation.
The `file-select`, `world-map`, and `course` skills own a short boot to that place.
Read `game/README.md` for dump folder layout.

## Settings

`nsmbu settings` changes `nsmbu/settings.ini`.
The next start loads that file.
Close a running game first.
That process keeps the settings it already loaded, and its next save replaces the file.
`nsmbu settings` prints the keys and the accepted values.

## Dump

Use flags on `nsmbu game`.
Bare `nsmbu game` opens the TUI.

```bash
nsmbu -s <id> game --list
nsmbu -s <id> game eu-1.3.0 --recomp
nsmbu -s <id> build
```

`--list` prints JSON (`active`, `versions`, `loose_images`).
`nsmbu game <id>` writes `game/active` and skips prompts.
Add `--recomp` to refresh `build/gen` from that dump's RPX.
Reuse `build/gen` unless `recomp/recomp.py` changed, you switch game version, or a `recomp/hooks/*.jsonc` file changed.
Runtime-only edits need `nsmbu build` only.

## Warm build

`tools/build/build.py` runs `cmake -S` before every `cmake --build`.
A warm build, same compilers and flags, should run `cmake --build` only.
Every build also regenerates `build/debug/generated/nsmbu_guest_stubs.c` and relinks.
The line `0 hooks` in that step is normal.
Hooks live in `build/gen`.
Regenerate the stubs only when a `recomp/hooks/*.jsonc` file or `recomp/recomp.py` changed.
A runtime-only edit compiles the changed `.cpp` and links.

## Sessions

`-s <id>` names the live game this command talks to.
Use the same id on `open`, `status`, `press`, and `close`.
A different id is a different game, socket, save, and settings under `.tmp/agent/<id>/`.
An agent game stays in the background.
Press, click, and screenshot do not bring its window forward.
If `nsmbu -s <id> open` refuses because that session is already running, run `nsmbu -s <id> status`.
`nsmbu -s <id> close` stops that game.
`nsmbu test` refuses to start while any live agent session is up.
Playtest cases use their own sockets and may run several games at once.
Finish the test or close the games before `nsmbu game … --recomp` or a `game/active` switch.

## Screen

The picture is the `screen=` word on the `nsmbu status` line.
Read that word before you read a screenshot.
When the line has no `screen=`, run `nsmbu status` again and read the new line.
`scene=` is the scene name.
`player=` is the player snapshot.
`scene=` can still name the previous place after the picture has changed.
`screen=course` is the course.
`screen=gamepad` is the GamePad popup.
`screen=saves` is the three save files.
`screen=title` is the title.
`screen=world-map` is the world map.
`screen=world-map-iris` is that map while the iris still covers it.
`screen=start-menu` names the selected start-menu button.
`screen=pause` names the selected pause row.
`screen=controller` is the Change Controller page.
`screen=settings` is this port's overlay.
`behind=` names the page under that overlay.
`screen=loading` is the loading card.

## Log

`nsmbu log --around` prints the status line, then the log lines for that picture.
`--lines N` sets how many of those lines to keep.
The default is 40.
`--tag a,b` keeps lines whose brackets contain one of those words.
`--around` keeps the tags for that picture.
A bracket word outside that list stays in the log file and out of this output.
`[vulkan]` and `[prof]` stay out until `--tag` names `vulkan` or `prof`.
The latest `[scene]` line stays in an `--around` list.

`input` is a pad change.
`down=` and `up=` name the buttons that changed.
`lstick=` and `rstick=` name a stick that moved.
A course line starts with the step.
Any other place starts with `frame`.
`scene` is a scene change.
`open` is a course opening.
`menu` is the world-map start menu.
`pause` is the course pause menu.
`overlay` is this port's settings overlay.
`screenshot` is a saved picture.
`boot` is process start.
`agent` is the control socket.
`gfx` is a frame dump or the title picture.
`prof` is a frame-time sample.
`vulkan` is a Vulkan line.
`docs/developer/cli.md` has the same list.

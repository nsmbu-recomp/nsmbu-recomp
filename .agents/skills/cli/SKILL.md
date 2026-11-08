---
name: cli
description: "nsmbu CLI playtests, benches, boots, builds, dumps, and NSMBU_* variables. Use when running nsmbu test, bench, scenario, compare-runs, a scripted boot, a warm build, a dump switch, or a pose or interpolation check."
---

# nsmbu CLI

TypeScript CLI under `tools/cli/`.
Run `nsmbu --help` for the command list.

## Docs

Read `docs/developer/automation.md` for playtests, benches, course replay, `nsmbu scenario` / `compare-runs`, `nsmbu scripted`, and boot recipes.
Development builds write `[input]` pad lines.
The `verify` skill is how you ask a person to play an input once and rerun it.
The line layout is in `docs/developer/replay-format.md`.
Read `docs/developer/cli.md` for install, build/launch, and live control commands.
Read `docs/developer/env.md` when you add, rename, or read an `NSMBU_*` variable.
Read Frame interpolation in `docs/developer/env.md` before changing a model pose, a camera picture, or interpolation.
The `scenes` skill owns a short boot to the map, a course, or the title.
Read `game/README.md` for dump folder layout.

## Dump

Use flags on `nsmbu game`.
Bare `nsmbu game` opens the TUI.

```bash
nsmbu game --list
nsmbu game eu-1.3.0 --recomp
nsmbu build
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

## One live game

`nsmbu open` and the live look commands share one control socket.
If `nsmbu open` refuses because a game is already running, run `nsmbu status` or `nsmbu close`.
`nsmbu test` refuses to start while that live agent is up.
Playtest cases use their own sockets and may run several games at once.
Finish the test or close the game before `nsmbu game … --recomp` or a `game/active` switch.

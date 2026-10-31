---
name: nsmbu
description: "Playtests, benches, boots, builds, env vars, and game dumps for the nsmbu CLI. Use when running nsmbu test or nsmbu bench, a scripted or hidden boot, a warm build, an NSMBU_* variable, a usa/eu dump switch, nsmbu status, a Release replay, or a model pose, camera picture, or interpolation jitter."
---

# nsmbu

TypeScript CLI under `tools/cli/`.
Run `nsmbu --help` for the command list.

## Docs

Read `docs/automation.md` for playtests, `nsmbu scripted`, replay inputs, and boot recipes.
Add a new boot recipe there when a command is worth running again.
Read `docs/cli.md` for install, build/launch, and the short live control commands.
Read `docs/env.md` when you add, rename, or read an `NSMBU_*` variable.
Read `game/README.md` for dump folder layout.

## Pose and camera pictures

Read Frame interpolation in `docs/env.md` before changing a model pose, a camera picture, or interpolation.
That section is the done check: `interp_picture_test`, then two consecutive TV frames from `NSMBU_DUMP_FRAMES`.

## Dump (no TUI)

Agents must not run bare `nsmbu game` (that opens the TUI).
Use flags only.

```bash
nsmbu game --list
nsmbu game eu-1.3.0 --recomp
nsmbu build
```

`--list` prints JSON (`active`, `versions`, `loose_images`).
`nsmbu game <id>` writes `game/active` and skips prompts.
Add `--extract` to extract (or re-extract) the disc.
Add `--recomp` to refresh `build/gen` from that dump's RPX.
`--recomp` is slow, reuse `build/gen` unless `tools/recomp/recomp.py` changed, you switch game version (e.g. USA  Europe), or `hooks.txt` / `hooks_*.txt` changed.
Runtime-only edits need `nsmbu build` only.
Then `nsmbu build` (and playtests / `nsmbu open`) use that dump.

## Warm build

`tools/build/build.py` runs `cmake -S` before every `cmake --build`.
The SDL configure summary is that second configure, not the compile.
A warm build, same compilers and flags, should run `cmake --build` only.
Print the SDL summary when the cache is created.

Every build also regenerates `build/debug/generated/nsmbu_guest_stubs.c` and relinks.
The line `0 hooks` in that step is normal.
Hooks live in `build/gen`.
Regenerate the stubs only when `hooks.txt`, `hooks_*.txt`, or `tools/recomp/recomp.py` changed.
A runtime-only edit compiles the changed `.cpp` and links.

## Hidden boot

Boot `build/debug/nsmbu --game game/<id>`, where `<id>` is the text in `game/active` or the `active` id from `nsmbu game --list`.
`--game game` has no `red-pro2.rpx` and exits at once.
`nsmbu scripted` and `tools/run/scripted-run.sh` pass that `game/` path.
Copy `nsmbu/save` into `.tmp/<name>/save` and set `NSMBU_USER_DIR` to `nsmbu/`.
`NSMBU_LOG_FILE` does not capture `fprintf` from the render thread.
That text stays on the terminal.
Title and file-select press lists are in `docs/automation.md`.
Enter `FileSelect` directly when the check is the menu.
Do not play the title for 1000 frames to reach it.

Issue playtests that use `Scene` open / apply-save need a `*-1.3.0` (or combo) dump.
Use `*-base` to check boot and title-version messaging on the base disc; Scene paths still need a build with those hooks.

Other non-interactive forms:

```bash
nsmbu game --import path/to/disc.wux --id eu-base --extract --recomp
nsmbu game --create eu-base
```

## Playtest

Repro and regression work is a case, not a live control chat.

1. Reuse or extend helpers in `tools/cli/src/playtest/helpers.ts` (`reachMap`, `loadPack`, `leaveCourse`, `enterCourse`, `quickSaveHere`, and the other exports).
2. Map round-trips use `leaveCourse` / `enterCourse`; do not build a case on `Scene.leave_course` alone.
   - `leaveCourse` is the pause-menu Quit path.
   - `Scene.leave_course` returns to the world map with the camera stuck off the path.
   - Keep `Scene.leave_course` only for the direct-open path in `opened-course`, which already calls it on purpose.
   - The repo save starts on node `W1M0`.
   - `A` on `W1M0` does not enter a course.
   - The first course node is `W101`, one stick-right away.
   - The only unlocked neighbour of `W101` is back to `W1M0`.
   - The world map reports `scene=CourseSelect` with `map=1`.
   - On the map means `map=1`, including `CourseSelect`.
   - Treating on-the-map as only `scene=Map` makes a successful leave look like a failure.
   - The next `A` then walks back into the course.
   - Agent `press` frame counts are logic steps.
   - A multi-step `Down` on the pause menu repeats and lands on Change Controller.
   - Menu cursor moves (`Down`, `Right`, `Up`) need a one-sample tap (`press(button, 1, true)`).
   - `Plus` and `A` still need a short hold.
   - Idling in `1-1` long enough for a late portable-save frame lets the Goomba kill Mario and drop the player on the map.
   - A course portable save should `open` the course and fire the save soon after play starts.
3. Add a `PlayCase` under `tools/cli/src/playtest/cases/` and register it in `cases/index.ts`.
4. Run `nsmbu test [name]`.
   `nsmbu test --list` prints the names (only `--list` is a valid flag).
5. Done when that case exits 0 (or fails with an assertion you can fix).

Each case uses `.tmp/playtest/<name>/` (save, control socket, userdata).
Selected cases run in parallel (default 2; override with `NSMBU_TEST_JOBS`).
Scene moves and map reads that need guest Lua stay inside the case through `Session` helpers.

## Performance bench

Traces belong in `nsmbu bench`, not `nsmbu test`.
`nsmbu bench --list` prints the names (only `--list` is a valid flag).
The window is visible.
One game runs at a time.
`nsmbu bench load-overworld` presses A through the title and the save file, then loads the world map.
The trace is `.tmp/bench/load-overworld/trace.json`.
It passes when the log reaches `CourseSelect from save` and six `[prof]` windows are on the map.

## Status

`nsmbu status` is the observation line while writing a case or checking a stuck run.

```text
frame=120 step=80 pid=1 scene=CourseSelect map=1 course=0 pack=1-1 entering=1-5 file=0 cp=-1 node=4 world=0 player=none
```

`scene` is `Boot`, `Flow`, `FileSelect`, `SaveSelect`, `Title`, `Menu`, `Map`, `CourseTask`, or `CourseSelect`.
`map=1` / `course=1` mean those tasks are alive.
`pack` and `entering` are course ids.
`world` / `node` / `cp` are map fields when `map=1`.
Fields that are not ready are left off the line.
`nsmbu screenshot [path]` when the question is what is on screen.

## One live game

`nsmbu open` / live look commands share one control socket.
If `nsmbu open` refuses because a game is already running, run `nsmbu status` or `nsmbu close`.
`nsmbu test` refuses to start while that live agent is up, playtest cases use their own sockets and may run several games at once.

Do not run `nsmbu game … --recomp` or change `game/active` while a playtest or `nsmbu open` is in flight.
Serialize dump switches: finish the test or close the game, then switch and rebuild.

## Live look

`open` / `press` / `stick` / `screenshot` / `wait` are for a short look or one pad pulse while writing the case.
They block until done, use `wait --scene NAME` or `wait --step N` instead of shell sleeps.
A lasting check belongs in the playtest step above.

## Inputs

A long pad timeline stays on `record` and `replay`.
Use `replay --release` for a measured course.

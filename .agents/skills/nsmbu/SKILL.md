---
name: nsmbu
description: "Playtests, benches, boots, builds, env vars, and game dumps for the nsmbu CLI. Use when running nsmbu test or nsmbu bench, nsmbu scenario or nsmbu compare-runs, a scripted or hidden boot, a warm build, an NSMBU_* variable, a usa/eu dump switch, nsmbu status, a course replay playtest, or a model pose, camera picture, or interpolation jitter."
---

# nsmbu

TypeScript CLI under `tools/cli/`.
Run `nsmbu --help` for the command list.

## Docs

Read `docs/developer/automation.md` for playtests, course replay logs, run comparison (`nsmbu scenario` / `compare-runs`), `nsmbu scripted`, and boot recipes.
Add a new boot recipe there when a command is worth running again.
Read `docs/developer/cli.md` for install, build/launch, and the short live control commands.
Read `docs/developer/env.md` when you add, rename, or read an `NSMBU_*` variable.
Read `game/README.md` for dump folder layout.

## Pose and camera pictures

Read Frame interpolation in `docs/developer/env.md` before changing a model pose, a camera picture, or interpolation.
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
`--recomp` is slow, reuse `build/gen` unless `recomp/recomp.py` changed, you switch game version (e.g. USA  Europe), or `hooks.txt` / `hooks_*.txt` changed.
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
Regenerate the stubs only when `hooks.txt`, `hooks_*.txt`, or `recomp/recomp.py` changed.
A runtime-only edit compiles the changed `.cpp` and links.

## Hidden boot

Boot `build/debug/nsmbu --game game/<id>`, where `<id>` is the text in `game/active` or the `active` id from `nsmbu game --list`.
`--game game` has no `red-pro2.rpx` and exits at once.
`nsmbu scripted` and `tools/run/scripted-run.sh` pass that `game/` path.
Copy `nsmbu/save` into `.tmp/<name>/save` and set `NSMBU_USER_DIR` to `nsmbu/`.
`NSMBU_LOG_FILE` does not capture `fprintf` from the render thread.
That text stays on the terminal.
Title and file-select press lists are in `docs/developer/automation.md`.
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
2. Map round-trips use `leaveCourse` / `enterCourse`.
   - `leaveCourse` is the pause-menu Quit path.
   - A direct-open quit uses `Session.leave`.
   - `Session.leave` calls `requestCourseOut_Normal` on the guest thread.
   - The map node name, the node position, and the map player come from agent status.
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
   `nsmbu test --list` prints the names.
   `nsmbu test --show [name]` leaves the game window on screen and runs one case at a time.
5. Done when that case exits 0 (or fails with an assertion you can fix).

Each case uses `.tmp/playtest/<name>/` (save, control socket, userdata).
Selected cases run in parallel (default 2; override with `NSMBU_TEST_JOBS`).
Cases start in fast forward (`NSMBU_UNCAPPED=1`).
The frame-rate cases set `NSMBU_UNCAPPED=0`.
Scene moves that need guest Lua stay inside the case through `Session` helpers.
Map place and the course quit use `status` and `leave`.

## Run comparison (not playtests)

Use this when two builds (or the same build twice) should match step-by-step under an open-loop `NSMBU_PRESS` / stick timeline.
Do not add a `PlayCase` for one-off bisect or build-vs-build checks.
Ship regressions still belong in `nsmbu test`.

Implementation lives in `tools/cli/src/gametest/` (`scenario.ts`, `compare.ts`, `compare-runs.ts`).
Unit tests: `node --test tools/cli/test/compare-runs.test.ts` (also in Linux/Windows CI).

`nsmbu scenario OUT [VAR=value …]` copies the save into `OUT`, boots hidden with `NSMBU_TEST_STATUS_TRACE`, waits for scene `ALIGN` (default `Title`), then records `STEPS` logic steps (default 300).
Runner env: `NSMBU_BIN`, `GAME`, `SAVE`, `ALIGN`, `STEPS`, `FIXED_SEED` (default 1), `SHADER_CACHE`, `BOOT_TRIES`, `BOOT_LIMIT`, `RUN_LIMIT`.
Extra `VAR=value` tokens after `OUT` become game env (same as the old shell runner).
Artifacts: `OUT/status.txt` (one line per logic step: scene, map, course, pack, player x/y/z), `log`, `console.log`, `perf.txt`.
With `FIXED_SEED=1`, press/stick/dump frame numbers count from the align scene (TV frame 1800 at that logic step); boot presses stay below raw frame 1800.

`nsmbu compare-runs RUN_A RUN_B [--scene NAME] [--steps N]` rebases both folders on the align scene and diffs traces.
Exit 0 identical, 1 first mismatch (printed), 2 never reached the scene.
`--scene` defaults to `ALIGN` or `Title`.

Compare uses the env-timeline path, not the playtest agent socket.
Identical gameplay from `Session.press` is not guaranteed to match a scenario trace.

Example (full recipe in `docs/developer/automation.md`):

```bash
ALIGN=CourseTask nsmbu scenario .tmp/runs/a NSMBU_PRESS='1800-1820:8000'
NSMBU_BIN=build/other/nsmbu ALIGN=CourseTask nsmbu scenario .tmp/runs/b NSMBU_PRESS='1800-1820:8000'
nsmbu compare-runs .tmp/runs/a .tmp/runs/b --scene CourseTask
```

`NSMBU_TEST_*` trace variables are documented in `docs/developer/env.md` under scripted compare runs.

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

`open` / `leave` / `press` / `stick` / `screenshot` / `wait` are for a short look or one pad pulse while writing the case.
They block until done, use `wait --scene NAME` or `wait --step N` instead of shell sleeps.
A lasting check belongs in the playtest step above.

## Inputs

A recorded course pad log is a playtest, not a separate command.
`nsmbu test replay-1-1` plays `tools/cli/src/playtest/replays/1-1.replay` and passes when the player path matches.
`replay-2-1` does the same for `2-1.replay`.
`replay-bug-1` opens `2-1` with the intro skip, quits with `leave`, and requires the map player on `W201`.
`[prof]` in `.tmp/playtest/replay-1-1/nsmbu.log` is the 1-1 course measurement.
The case uses the debug build.
`nsmbu test --show replay-1-1` leaves that window on screen.
Add a new log under `tools/cli/src/playtest/replays/` and a `PlayCase` that calls `runReplay` when a course recording should keep passing.

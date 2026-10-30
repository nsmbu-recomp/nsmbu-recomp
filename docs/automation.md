# Automation runs

Issue and release game regressions belong in playtests.
Use `nsmbu scripted` when you need exit-at-frame dumps or a fixed `NSMBU_PRESS` timeline without a playtest case.
Long pad timelines stay on `record` / `replay`.

## Playtests

`nsmbu test` boots a debug game per case and checks issue and release regressions.
Selected cases run in parallel (default 2 at a time; set `NSMBU_TEST_JOBS` to change).
Add or extend a case under `tools/cli/src/playtest/cases/` and register it in `cases/index.ts`.
Reuse helpers in `tools/cli/src/playtest/helpers.ts` (`reachMap`, `loadPack`, `leaveCourse`, `enterCourse`, `quickSaveHere`, and the other exports) before inventing a new flow.
Map round-trips use `leaveCourse` / `enterCourse`, not `Scene.leave_course` alone.
Scene moves that need guest Lua stay inside the case through `Session` helpers.

`nsmbu test --list` prints the case names and summaries (the live list).
`nsmbu test opened-course` runs one case.
Only `--list` is a valid flag; other options are rejected.
Each case uses `.tmp/playtest/<name>/` for its save, control socket, and log file, so `nsmbu/save` and the live agent socket stay unread.
Shader cache and settings stay in the shared `nsmbu/` userdata.

Issue links that the cases cover:

- `startup` — [#6](https://github.com/nsmbu-recomp/nsmbu-recomp/issues/6)
- `opening-cinematic`, `save-roundtrip`, `course-enter`, `save-state` — [#14](https://github.com/nsmbu-recomp/nsmbu-recomp/issues/14) (Quick Save / story map / high fps)
- `course-enter-120` — [#15](https://github.com/nsmbu-recomp/nsmbu-recomp/issues/15) at 120 fps
- `map-shadow-120` — [#20](https://github.com/nsmbu-recomp/nsmbu-recomp/issues/20) at 120 fps (world map stays open)
- `opened-course` — 0.6.1 course-open / leave-to-map regressions

Closed-issue coverage outside playtests:

- [#9](https://github.com/nsmbu-recomp/nsmbu-recomp/issues/9) multiplayer bindings — `ctest -R input_mapping` (`test_issue_9_multiplayer_bindings`).
- [#11](https://github.com/nsmbu-recomp/nsmbu-recomp/issues/11) Windows common-key paste — `tools/installer/test_setup.py` (`test_issue_11_windows_common_key_paste`).
- [#7](https://github.com/nsmbu-recomp/nsmbu-recomp/issues/7) choppy gameplay has no deterministic playtest; shader precompile and the Vulkan smoke suite cover the shipped mitigations.
- 0.6.2 mismatched color-target freeze — Vulkan renderer smoke (`mismatched_color_targets`).

Map reads use the USA v1.3.0 addresses in `docs/nsmbu/course-open.md` and `vendor/decomp/include/course_select/NOTES_map_spawn.md`.
The run needs `game/` and a debug build.
It is not part of `ctest`.
Run it before a release when this machine has a dump.

## Scripted boots

Headless boots use `nsmbu scripted <name>` from the repo root (wraps `tools/run/scripted-run.sh`).
The script copies `nsmbu/save` into `.tmp/<name>/save`, then starts `build/debug/nsmbu` (or another build dir if present) with that save.
Frame dumps land in `.tmp/<name>/` as `frame_<n>.png`.
The repo `nsmbu/save` stays unread by the game.

### Steps

1. Set `NSMBU_EXIT_AT_FRAME`.
   Set `NSMBU_DUMP_FRAMES` to the frames you will look at.
   Set `NSMBU_PRESS` and `NSMBU_STICK` when the boot must press buttons.
   `NSMBU_RES_SCALE` defaults to 1.
2. Run `nsmbu scripted <name>` from the repo root.
   The run is done when the process exits and each dumped `frame_<n>.png` is in `.tmp/<name>`.
3. Read one dumped frame.
   Continue only when that frame is the scene you mean.
4. For a shake check, dump the same consecutive frames at scale 1 and at scale 2.
   Run `python3 tools/frames/flicker.py FIRST LAST .tmp/<name>`.
   The check is done when both scales have a result and you have looked at one frame from each.

### Environment the script sets

`tools/run/scripted-run.sh` always exports these values:

| Variable | Value | Role |
| --- | --- | --- |
| `NSMBU_USER_DIR` | `.tmp/<name>/user` unless already set | Settings and controls are copied from the repo userdata tree; the shader cache starts empty |
| `NSMBU_HIDDEN_WINDOWS` | `1` | Keeps game windows off screen |
| `NSMBU_NO_HOST_INPUT` | `1` | Ignores keyboard, mouse, and gamepad; scripted input only |
| `NSMBU_VK_PRESENT_MODE` | `immediate` | Uncapped present for faster boots |
| `NSMBU_NO_AUDIO` | `1` | Skips opening an audio output device |
| `NSMBU_RES_SCALE` | `1` unless already set | Internal resolution scale (0.5–4) |

The binary also receives `--game` set to `game/` and `--save .tmp/<name>/save`.
That `game/` folder has no `red-pro2.rpx`, so the process exits before the first frame.
A hidden boot that must draw uses `build/debug/nsmbu --game game/usa-1.3.0`.

### Environment you set

Set these on the command line before the script:

| Variable | Required | Format / notes |
| --- | --- | --- |
| `NSMBU_EXIT_AT_FRAME` | yes | TV frame number; the process quits in order at that frame |
| `NSMBU_DUMP_FRAMES` | when you need PNGs | Comma-separated TV frames, e.g. `7000` or `800,1200,2000` |
| `NSMBU_PRESS` | optional | `from-to:bits` list; see below |
| `NSMBU_STICK` | optional | `from-to:x:y` list; see below |
| `NSMBU_RSTICK` | optional | Same form as `NSMBU_STICK` for the right stick |
| `NSMBU_KEYS` | optional | `from-to:key[+key…]` list of host key ids |
| `NSMBU_RES_SCALE` | optional | Overrides the script default of `1` |
| `NSMBU_MOD_MANAGER_DIR` | when mods must load | Absolute path to userdata that holds `mods/` and `mods.json` |

With `NSMBU_NO_HOST_INPUT` set, the mod manager stays off unless `NSMBU_MOD_MANAGER_DIR` points at a storage tree.
For a normal repo userdata tree, use `NSMBU_MOD_MANAGER_DIR=<repo>/nsmbu`.

### Scripted buttons

`NSMBU_PRESS` holds VPAD bits for inclusive TV-frame ranges.

```
from-to:hexbits,from-to:hexbits,…
```

`8000` is A.
Use a short pulse, about 20 frames.
A hold that is already down when a dialog opens does not press the button again.
Combine bits with a sum.

- A `8000`, B `4000`, X `2000`, Y `1000`
- L `0020`, R `0010`, ZL `0080`, ZR `0040`
- Plus `0008`, Minus `0004`, Home `0002`
- Up `0200`, Down `0100`, Left `0800`, Right `0400`

### Scripted sticks

`NSMBU_STICK` and `NSMBU_RSTICK` set stick axes for inclusive TV-frame ranges.

```
from-to:x:y,from-to:x:y,…
```

Positive x is right.
Positive y is up.
Values are typically `-1` to `1`.

### Example

`nsmbu/save` is already past the opening cutscene.
This recipe starts from that save.

```
NSMBU_PRESS='1800-1820:8000,2600-2620:8000,3400-3420:8000,4200-4220:8000,5000-5020:8000,6300-6340:8000' \
NSMBU_STICK='5400-6000:1:0' \
NSMBU_DUMP_FRAMES=7000 \
NSMBU_EXIT_AT_FRAME=7100 \
tools/run/scripted-run.sh w11
```

Frame 7000 is the course.
The picture is the big tree, the bricks, and Mario on the ground.
A world-map boot that stops before that last A is in Boot recipes below.

### Compare two scripted runs

`tools/gametest/run_scenario.sh OUT [VAR=value ...]` is the compare path.
It boots hidden and muted on a copy of the save, with the user folder, the shader cache, and the states under `OUT`.
It waits for the first logic step of scene `ALIGN` (default `Title`; others are `Map` and `CourseTask`), then records `STEPS` more logic steps (default 300).
A boot that never reaches the scene is retried (`BOOT_TRIES`, default 3) and its log stays next to `OUT` as `OUT.bootfailN.log`.

`OUT/status.txt` has one line per logic step: step, scene, map, course, pack, and player x, y, z.
`-` marks a value the game does not have yet.
The runtime writes it when `NSMBU_TEST_STATUS_TRACE` is set.

`tools/gametest/compare.py RUN_A RUN_B --scene CourseTask` aligns both runs on the log line `[test] align at logic step N scene=NAME`, rebases the step numbers, and diffs the lines.
It prints the first differing step.
The exit status is 0 for identical runs, 1 for a difference, and 2 when a run never reached the scene.

```
ALIGN=CourseTask tools/gametest/run_scenario.sh .tmp/runs/a NSMBU_PRESS='1800-1820:8000'
ALIGN=CourseTask tools/gametest/run_scenario.sh .tmp/runs/b NSMBU_PRESS='1800-1820:8000'
python3 tools/gametest/compare.py .tmp/runs/a .tmp/runs/b --scene CourseTask
```

With `NSMBU_TEST_FIXED_SEED=1` (the runner default) `NSMBU_PRESS`, `NSMBU_KEYS`, `NSMBU_STICK`, `NSMBU_RSTICK`, and `NSMBU_DUMP_FRAMES` count from the align scene.
The TV frame of that logic step counts as frame 1800.
Frames before it stay raw TV frames, so keep boot presses below 1800.
Dumped files are named with the rebased frame.
`NSMBU_EXIT_AT_FRAME` stays on raw TV frames.
The switch changes no guest state; it only rebases those frame numbers.

### Mute

The script sets `NSMBU_NO_AUDIO=1` so no output device is opened.
The mix still runs.
Interactive mute is in the settings overlay (` or F12) > Audio.

## Boot recipes

Add a recipe here when a boot, pad timeline, or profile command works a second time.
Each entry is the command, the log line that shows it worked, and what a later press would do.
Full variable lists stay in [env.md](env.md).
Course replay benchmarks stay in [performance.md](performance.md).

### World map

Launch `build/debug/nsmbu` yourself when the run must use `nsmbu/settings.ini` (165 fps interpolation, resolution scale, present mode).
`nsmbu scripted` forces an immediate present, resolution scale 1, hidden windows, and a copy of the save.

`NSMBU_USER_DIR` must be the repo `nsmbu/` directory.
A `portable.txt` beside the binary wins over that variable.
Without either one, settings and the shader cache come from `~/.config/nsmbu`.

On a 165 Hz debug boot from `nsmbu/save` (file 0 on the Acorn Plains map), 200 frames between pulses is enough.
The slower 800-frame list in the scripted example above is for walking into a course.

```bash
NSMBU_USER_DIR="$PWD/nsmbu" \
NSMBU_LOG_FILE="$PWD/.tmp/repro/map.log" \
NSMBU_PRESS='600-620:8000,800-820:8000,1000-1020:8000,1200-1220:8000,1400-1420:8000,1600-1620:8000,1800-1820:8000,2000-2020:8000,2200-2220:8000,2400-2420:8000' \
NSMBU_EXIT_AT_FRAME=5600 \
NSMBU_PROFILE=1 NSMBU_VK_STATS=1 NSMBU_NO_AUDIO=1 NSMBU_INTERP_PASS_STATS=1 \
./build/debug/nsmbu --game "$PWD/game/usa-1.3.0" --save "$PWD/nsmbu/save"
```

Done when the log contains `[scene] CourseSelect from save` and later `[prof]` lines are the map.
File select appears around frame 600.
Course select is up before frame 3000.
A settled debug map is about 165 swaps/s, acquire 1.00 per frame, about 60 hold frames out of 120, and about 82 logic steps/s.
Another A after that scene enters the course.
The stick and final A in the scripted example do that on purpose.

`NSMBU_STATE_LOAD_AT` at TV frame 700 does not reach this save.
The guest thread in the state is not alive yet, and a portable load of `slot1.nsstate` fails its checksum against the current save.

### Title and file select

Same save and hidden-window setup as the world-map boot.
`--game` is `game/usa-1.3.0`.
`NSMBU_ASPECT=21:9` makes the TV target 1707×720.
`NSMBU_DUMP_FRAMES` writes `frame_<n>.png` in the working directory.
A value can list frames or inclusive ranges, for example `480,760-767`.
`NSMBU_DUMP_TITLE=8` writes `title_0000_logic.png` or `title_0000_blend.png` for that many pictures.
It waits until the title course has a player on screen and the TV picture is open past the iris.
`logic` is a game step.
`blend` is an in-between frame drawn at 120 fps.
`NSMBU_VK_PRESENT_MODE=immediate`, `NSMBU_HIDDEN_WINDOWS=1`, `NSMBU_NO_HOST_INPUT=1`, `NSMBU_NO_AUDIO=1`, and `NSMBU_RES_SCALE=1` keep the boot short.

The controller screen is up by frame 200.
The title picture is up by frame 480.
“Press 2!” is on screen from about frame 760 through 920.
One A at frames 860–880 (`8000`) opens the file select by frame 1000.

```bash
NSMBU_USER_DIR="$PWD/nsmbu" \
NSMBU_HIDDEN_WINDOWS=1 NSMBU_NO_HOST_INPUT=1 NSMBU_VK_PRESENT_MODE=immediate \
NSMBU_NO_AUDIO=1 NSMBU_RES_SCALE=1 NSMBU_ASPECT=21:9 \
NSMBU_PRESS='860-880:8000' \
NSMBU_DUMP_FRAMES=1000 NSMBU_EXIT_AT_FRAME=1040 \
./build/debug/nsmbu --game "$PWD/game/usa-1.3.0" --save "$PWD/.tmp/file-select/save"
```

Copy `nsmbu/save` into `.tmp/file-select/save` first.
Done when `frame_1000.png` is the file-select menu.
A menu check should enter `FileSelect` directly instead of waiting out this title.

### Other boots

- `nsmbu replay --release 1-1` is the course benchmark.
  The baseline is in [performance.md](performance.md).
- `nsmbu status` is the live scene line.
  `scene=CourseSelect` with `map=1` is the world map.
- `NSMBU_LOG_FILE=0` skips the log file.
  Any other path replaces `nsmbu/logs/nsmbu.log`.
  Profile logs belong under `.tmp/`.

## Related test aids

These are separate from `scripted-run.sh` and appear in benches and device checks:

- `NSMBU_RECORD_INPUT` — path of a text log of pad changes, keyed by guest logic step
- `NSMBU_RECORD_COURSE` — course named in the log's `course=` header
- `NSMBU_REPLAY_INPUT` — play that log back and ignore the live pad
- `NSMBU_REPLAY_EXIT` — `none` keeps the game running on live input when the replay input ends
- `NSMBU_REPLAY_TRACE` — log the replay clock arming, control gate, and early step positions
- `NSMBU_TEST_ORIGIN`, `NSMBU_TEST_PRESS`, `NSMBU_TEST_STICK`, `NSMBU_TEST_RSTICK`, `NSMBU_TEST_MODE`, `NSMBU_TEST_END` — timed scenarios after a logic-step origin
- `NSMBU_DUMP_PRESENT`, `NSMBU_DUMP_LOAD_FRAMES`, `NSMBU_CAPTURE` — present / load-path captures (see `docs/vulkan.md`)
- `NSMBU_STATE_LOAD_AT`, `NSMBU_PORTABLE_SAVE_AT` — save-state load points (see `docs/portable-save-states.md`)

## Input replays

The `.replay` format is a text input log; [replay-format.md](replay-format.md) lists each line, key, and field.
`NSMBU_RECORD_INPUT` writes one line each time the pad changes.
The step count starts when in-level play begins (`AreaTask` is live), then follows guest logic steps (one per simulation tick, not per rendered frame).
New files start with `version=1` and use `replays/<name>.replay` paths from `nsmbu record` / `nsmbu replay`.
Metadata lines are `key=value`; `#` starts a comment.
`course=` names the course; `nsmbu replay` launches it.
`origin=control` (what new files write) arms the step count when the game hands control to the player, so a slow boot or an uncapped run does not shift the recording.
Files without `origin=` fall back to arming when the player reaches a recorded `pos=`, which depends on how long the load takes.
`NSMBU_RECORD_COURSE` supplies that name when the runtime writes the file.
Each pad line ends with the player position (`x y z`).
Start config lines record `player=`, `lives=`, `player_coins=`, `coins=`, and `mode=`, one per line; replay starts the player with the recorded power-up mode.
`mode=` is `small`, `normal`, `fire`, `mini`, `propeller`, `penguin`, `ice`, `squirrel`, or `psquirrel`; numeric `0`–`8` still loads.
The buttons field is `-` when no button is down, otherwise `+`-joined names such as `A+Y`; legacy hex values still load.
`exit=input` (the default) quits when the recorded input ends; `exit=none` keeps the game running and hands control back to live input.
`--no-exit-on-input` sets `NSMBU_REPLAY_EXIT=none` for one run.
Replay logs the first step where position differs by more than 5 cm, and a match or mismatch count when it finishes.
A slow boot does not shift later lines.
Legacy `!` and `#` directive files, including `# nsmbu-input` version 1 boot logic steps and v3 `@` witness lines, still load when pointed at by `NSMBU_REPLAY_INPUT`.
`NSMBU_REPLAY_INPUT` applies the latest line whose step is at or before the current step.
Live keyboard, mouse, and gamepad input is replaced for that run.
Intro-skip button pulses stay outside the file, so play the file back with the same course pack.

```
nsmbu record 1-1
nsmbu record 2-1 bug-1
nsmbu replay 1-1
nsmbu replay bug-1
nsmbu replay --release 1-1
```

`nsmbu record 1-1` writes `replays/1-1.replay` from the start of that course.
`nsmbu record 2-1 bug-1` writes `replays/bug-1.replay` with `course=2-1`.
`nsmbu replay 1-1` plays `replays/1-1.replay` on `build/debug/nsmbu`.
`nsmbu replay bug-1` plays that file on the course named in its header.
`nsmbu replay --release 1-1` plays it on `build/release/nsmbu`.
Both set `NSMBU_PROFILE`, `NSMBU_VK_STATS`, `NSMBU_SYNC_STATS`, and `NSMBU_CRASH_RECOVERY` when those variables are unset.
The command prints `[replay] log <path>`.
The default path is `nsmbu/logs/nsmbu.log`.
`--uncapped` sets `NSMBU_UNCAPPED=1`.
`--exit-at N` quits at that TV frame.
`--log PATH` writes that file instead.
The game quits once the guest passes the last recorded step.

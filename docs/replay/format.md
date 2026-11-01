# Replay file format

A `.replay` file in this repo is a plain text input log, not the game's binary `RPRH` replay.
`nsmbu record` writes it and `nsmbu replay` reads it.
`NSMBU_RECORD_INPUT` and `NSMBU_REPLAY_INPUT` point the runtime at the same files directly.

The file holds initial config lines, then one line for each change of the pad state in a course.
Lines are read with a 512-byte buffer, so keep each line below 511 bytes.
Blank lines are ignored.
Keys may appear in any order.

## Initial config

These lines set up the run.
Only `version` is required among the keys, and only for new files.
Start snapshot keys are optional.

- `version=1` — format marker. Any value marks the current format, and writers use `1`
- `course=1-1` — course name such as `1-1` or `2-1`. `nsmbu replay <name>` launches this course
- `origin=control` — arms the step clock when the game hands control to the player
- `exit=none` — keeps the game running on live input after the last line. Without the key, the replay quits when the input ends
- `player=0` — player index `0`–`3` for the start snapshot
- `lives=5` — remaining lives in the start snapshot
- `player_coins=0` — coins for that player, from the per-player `coin_cnt` in `FieldGameData`
- `coins=0` — course-wide coin total, from `FieldGameData::mCoinNum`
- `mode=fire` — power-up mode in the start snapshot

`nsmbu record` writes `version`, `course`, and `origin` first, then the start snapshot values, then a `# step buttons lx ly rx ry touch tx ty x y z` legend line, then the pad lines.
The `exit` key is optional and is not written by the recorder.
When no player is active at the first pad read, `nsmbu record` omits the start snapshot.

When `course` is absent, `nsmbu replay` uses the file name when it looks like `W-L`.
Otherwise the command stops and asks for a `course` line.
`NSMBU_RECORD_COURSE` supplies the course value when the runtime writes the file.
`NSMBU_REPLAY_EXIT` overrides the `exit` key for one run.

The start snapshot describes the player at step 0.
Writers emit one key on each line.
`mode` is `small`, `normal`, `fire`, `mini`, `propeller`, `penguin`, `ice`, `squirrel`, or `psquirrel`, and the numeric form `0`–`8` still loads.
The replay sets that mode before the first pad line plays.

A step is one guest simulation tick, not one rendered frame, so an uncapped run and a capped run produce the same steps.
With `origin=control`, the step count starts when the game gives control to the player.
A slow boot, a title screen wait, or a long course load does not shift the lines.
The clock waits for input control with a 20 s guard, then starts anyway.
Without `origin=control`, the clock starts as soon as course play starts.

## Per step config

Each line describes the pad state from that step until the next line.

```
<step> <buttons> <lx> <ly> <rx> <ry> <touch> <tx> <ty> [<x> <y> <z>]
```

- `<step>` — simulation tick from the step clock
- `<buttons>` — button set, see below
- `<lx> <ly>` — left stick, `-1` to `1`
- `<rx> <ry>` — right stick, `-1` to `1`
- `<touch>` — `0` or `1` for touch screen contact
- `<tx> <ty>` — touch point
- `<x> <y> <z>` — player position after the change. Optional

The replay applies the newest line whose `step` is at or before the current step.
Lines must be in ascending step order, because the reader only moves forward.
`nsmbu record` writes one line for each change of buttons, sticks, or touch, and writes the position with `%.9g`.
It also writes the first line at the step where the pad state is first read, so a replay can start with no button down.

### Buttons

`-` means no button is down. `none` also parses.
Otherwise the field is a `+`-joined set of action names, for example `A+Y` or `B+ZR`.
Names ignore case.
The ids come from `runtime/src/input/input_map.cpp`:

`A`, `B`, `X`, `Y`, `L`, `R`, `ZL`, `ZR`, `Plus`, `Minus`, `Home`, `DpadUp`, `DpadDown`, `DpadLeft`, `DpadRight`, `LeftStickClick`, `RightStickClick`.

Stick directions are fields, not button names.
A legacy hexadecimal value still loads, with or without a `0x` prefix, for example `00008000`.
Writers emit names while every set bit matches an action in the table, and fall back to an 8-digit hex value when a bit does not.

### Position checks

The optional `x y z` fields are witness positions.
At that step the replay compares the live player position with the recorded position.
The game reads the pad more than once during a step.
A later read is after that step's movement.
The check uses the first read, which is the position stored when the pad state was recorded.
A difference of 0.05 or less on each axis matches.
A larger difference up to 4 on each axis is logged, and the replay keeps playing.
A difference above 4 on any axis logs `[input] replay diverged at step <n>` and stops.
With `exit=none`, that same large gap hands control to live input instead of quitting.

When the last line has played, the replay logs a match or a mismatch count and quits, unless `exit=none` is set.
The counts cover start-snapshot misses and witness misses.
The loader logs `[input] replaying <n> samples from <path> (<m> position checks)` when it reads the file.

## Legacy directive files

Older files use `!` or `#` directives, which still load through `NSMBU_REPLAY_INPUT`.
`#` also starts a comment, so only the directive names below are read.

- `nsmbu-replay` — current format and course-relative steps
- `nsmbu-input <n>` — format version. Version `1` uses absolute boot steps; version `2` and later use course-relative steps
- `course <name>` — course name
- `origin <value>` — `control` arms the clock at control handover
- `start ...` — inline start snapshot, same keys as above
- `@ <step> <x> <y> <z>` — witness position for the next pad line with that step

Directive arguments use spaces, not `=`, except inside an inline `start` line.
An older file may also carry a `pos=<x> <y> <z>` key.
The replay reads it to wait for the player at that point and to check the start position.

## Example

```
version=1
origin=control
course=1-1
player=0
lives=5
player_coins=0
coins=0
mode=fire
# step buttons lx ly rx ry touch tx ty x y z
17 - 1 0 0 0 0 0 0 600 -848 3000
24 Y 1 0 0 0 0 0 0 602.590027 -848 3000
175 A+Y 1 0 0 0 0 0 0 984.219849 -848 3000
```

The initial config sets the course and the start snapshot.
The first pad line holds from step 17 to step 23.
At step 24 the `Y` button goes down and position check `602.590027 -848 3000` applies.
The left stick stays at `1 0`, which the recorder captured at the start.

## Related

- [../../replays/README.md](../../replays/README.md) is the short key and field summary next to the files.
- [../automation.md](../automation.md) describes `nsmbu record`, `nsmbu replay`, and the input environment variables.
- [../cli.md](../cli.md) lists the command forms.

---
name: course
description: "Open a course pack such as 1-1 and leave it. Use when the check is a course, scene=CourseTask, or entering a level from the world map."
---

# Course

`nsmbu open 1-1` passes `--open 1-1`.
Any `world-pack` token works the same way (`1-2`, `2-1`).
A `course_res_pack` archive works too.
`nsmbu open game/content/Common/course_res_pack/1-27.szs` starts `1-27`.
The native skip applies save file 0, loads that pack, and skips the title.
Pass only the pack.
`nsmbu goto 1-1` loads that pack when a game is already open.

`nsmbu open` uses the repo `nsmbu/` settings and the active dump.
A `*-base` dump only boots.
A scene check needs a `*-1.3.0` dump, such as `usa-1.3.0`.
If open reports a game already running, read `nsmbu status` or run `nsmbu close`, then open again.

```bash
nsmbu open 1-1
nsmbu wait --scene CourseTask --timeout 180
nsmbu status
```

The log line `[open]` ends with `course up`.
Done when status shows `scene=CourseTask`, `course=1`, `map=0`, `pack=1-1`, and `player=` is not `none`.
`entering=` is absent on that line.
While the loading card is up, status can still show the map, and `entering=` names the pack that is loading.
Check soon after play starts.
A long idle in `1-1` lets the Goomba hit Mario.

`nsmbu leave` quits the live course.
Done when status shows `scene=CourseSelect` and `map=1`.
The in-level pause menu is the `pause-menu` skill.
That menu is not the world-map start menu.
`nsmbu test replay-1-1` plays the recorded 1-1 pad log and checks the player path.
That recording is USA.
On an EU dump the path diverges by the flag.
A quit from `2-1` stands on `W201`.
The `world-map` skill covers the camera that can stay on `W2M0`.
`[prof]` in `.tmp/playtest/replay-1-1/nsmbu.log` is the course measurement.
Use `nsmbu open 1-1` when the question is the live course.

## From the world map

Start from the `world-map` skill.
The repo save stands on `W1M0`.
`A` on `W1M0` stays on the map.
The first course node is `W101`.

```bash
nsmbu stick 1 0 --frames 40
nsmbu press A --frames 25
nsmbu wait --scene CourseTask --timeout 120
nsmbu status
```

Done on the same course line as the direct open, with `pack=1-1`.
The only unlocked neighbour of `W101` is back to `W1M0`.

A check that must keep passing is a playtest case.
The `cli` skill owns playtests.
When the check is controller input you cannot script, `docs/developer/replay-format.md` has the line layout.

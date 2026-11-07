# Course

`nsmbu open 1-1` passes `--open 1-1`.
Any `world-pack` token works the same way (`1-2`, `2-1`).
A `course_res_pack` archive works too: `nsmbu open game/content/Common/course_res_pack/1-27.szs` starts `1-27`.
The native skip applies save file 0, loads that pack, and skips the title.
Do not press through the menus for a direct open.
Do not pass a save slot and a pack together.

```bash
nsmbu open 1-1
nsmbu wait --scene CourseTask --timeout 180
nsmbu status
```

The log line `[open]` ends with `course up`.
Done when status shows `scene=CourseTask`, `course=1`, `map=0`, `pack=1-1`, and `player=` is not `none`.
`entering=` should be gone.
Check soon after play starts.
A long idle in `1-1` lets the Goomba hit Mario.

`nsmbu test replay-1-1` plays the recorded 1-1 pad log and checks the player path.
`[prof]` in `.tmp/playtest/replay-1-1/nsmbu.log` is the course measurement.
Use `nsmbu open 1-1` when the question is the live course.

## From the world map

Start from [overworld.md](overworld.md).
The repo save stands on `W1M0`.
`A` there does not enter a course.
The first course node is `W101`.

```bash
nsmbu stick 1 0 --frames 40
nsmbu press A --frames 25
nsmbu wait --scene CourseTask --timeout 120
nsmbu status
```

Done on the same course line as the direct open, with `pack=1-1`.
The only unlocked neighbour of `W101` is back to `W1M0`.
Returning to the map is the pause-menu Quit path (`leaveCourse` in `docs/developer/automation.md`).
`scene=CourseSelect` with `map=1` means the map is up again.

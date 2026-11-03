# World map

The overworld is `scene=CourseSelect` or `scene=Map` with `map=1`.
The repo save starts on node `W1M0` in world 0 (Acorn Plains).

This boot keeps `nsmbu/settings.ini` (frame cap, resolution, present mode).
Leave `NSMBU_INTERP_FPS`, `NSMBU_RES_SCALE`, `NSMBU_VK_PRESENT_MODE`, and `NSMBU_HIDDEN_WINDOWS` unset when the check is that session.

```bash
nsmbu open
nsmbu wait --scene FileSelect --timeout 120
nsmbu press A --frames 20
nsmbu wait --scene Title --timeout 90
nsmbu press A --frames 20
nsmbu wait --scene Menu --timeout 60
nsmbu press A --frames 20
nsmbu wait --scene CourseSelect --timeout 120
nsmbu status
```

The boot GamePad popup is the first `FileSelect`.
The title is the next scene.
One A on the title opens the save-file menu (`scene=Menu`).
One more A there confirms file 0 and opens the map.
If a wait is already on the next scene, continue.
Course select is up well before a few thousand frames.
Do not hold A across the whole title.

Done when status shows `map=1` and `scene=CourseSelect` or `scene=Map`.
`nsmbu screenshot .tmp/repro/overworld.png` when the question is the picture.

`A` on `W1M0` does not enter a course.
One stick-right reaches `W101`.
That walk is in [course.md](course.md).

`nsmbu scripted` forces a hidden window, immediate present, and resolution scale 1.
Use it only for a log that must match [automation.md](../../../../docs/developer/automation.md).
The press list there is the same map, measured in TV frames.

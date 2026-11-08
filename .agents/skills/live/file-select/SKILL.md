---
name: file-select
description: "Open the save-file menu and tell the GamePad popup from the three save files. Use when the check is file select, the title, the GamePad popup, or scene=Menu."
---

# File select

Boot order is `Boot`, then `Flow`, then `FileSelect`, then `Title`.
`scene=FileSelect` is the GamePad popup.
The title is the next scene.
One A on the title opens the save-file menu.
That menu is `MenuScene`, so status reads `scene=Menu`.
The menu shows three save files.
One A there confirms file 0.
The next screen can be Mario's "Yes! Let's go!" confirm.
One more A there opens the world map.
The `world-map` skill owns that map.
`scene=SaveSelect` is the challenge menu.
That menu is not the three story files.
`nsmbu goto file` stops on `scene=FileSelect`.
That stop is the popup.

`nsmbu open` uses the repo `nsmbu/` settings and the active dump.
A `*-base` dump only boots.
A scene check needs a `*-1.3.0` dump, such as `usa-1.3.0`.
If open reports a game already running, read `nsmbu status` or run `nsmbu close`, then open again.
`nsmbu bench` and `nsmbu game … --recomp` also refuse while that game is running.

```bash
nsmbu open
nsmbu wait --scene Title --timeout 120
nsmbu press A --frames 20
nsmbu wait --scene Menu --timeout 60
nsmbu status
nsmbu screenshot .tmp/repro/file-select.png
```

The first `scene=Title` can still be the iris.
Mario and Luigi run in before the logo and the "Press 2!" line.
Press A when that line is on screen.
Press A for those 20 frames, then wait.
A hold across the title confirms the file and skips the menu.
On a 21:9 boot the title iris still stops at the 16:9 circle, with black on the left and right, until the next scene fills the frame.
Done when status shows `scene=Menu` and the screenshot is the three save files.
The popup is done at the first `scene=FileSelect`.
The title picture is `scene=Title`.
Stop on the scene you need.
If that wait lands on `CourseSelect`, the file was already confirmed and the map is up.

A hidden frame dump of this path is in `docs/developer/automation.md`.
That recipe copies `nsmbu/save` and presses A once around the "Press 2!" line.
Use it when you need `frame_*.png` instead of a live window.

A check that must keep passing is a playtest case.
The `cli` skill owns playtests.
When the check is controller input you cannot script, `docs/developer/replay-format.md` has the line layout.

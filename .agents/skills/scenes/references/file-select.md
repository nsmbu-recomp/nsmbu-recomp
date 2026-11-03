# Title and file select

Boot order is `Boot`, then `Flow`, then `FileSelect` (the GamePad popup), then `Title`.
The file menu comes after one A on the title.
It is not the early `FileSelect` popup.

```bash
nsmbu open
nsmbu wait --scene Title --timeout 120
nsmbu press A --frames 20
nsmbu wait --scene Menu --timeout 60
nsmbu status
nsmbu screenshot .tmp/repro/file-select.png
```

The save-file menu is `MenuScene`, so status reads `scene=Menu`.
Done when status shows `scene=Menu` and the screenshot is the three save files.
The popup is done earlier, at the first `scene=FileSelect`.
The title picture is `scene=Title`.
Stop on the scene you need.
If that wait lands on `CourseSelect`, the file was already confirmed and the map is up.
Do not play the title for a thousand frames to reach the menu.

A hidden frame dump of this path is in [automation.md](../../../../docs/developer/automation.md).
That recipe copies `nsmbu/save` and presses A once around the “Press 2!” line.
Use it when you need `frame_*.png` instead of a live window.

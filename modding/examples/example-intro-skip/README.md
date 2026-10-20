# Intro skip

The skip reads the Save file option.
That option is 1, 2, or 3.
The default is 1.
It reads the Course pack option.
The default is `1-1`.
A non-empty pack enters that stage with `scene.load_pack` after the save opens.
While the boot GamePad setup popup is up (`file_select()`), it presses **A** about every 30 logic steps.
After four presses, if the popup is still up, it calls `open_title()` until the title or map phase is current.
It calls `apply_save(file)` after the boot logo.
Applying the save during the logo sends the flow into course select before that task has a course node, and course select then crashes.
That call does not open the world map.
`file` is the option minus 1.

`Hooks.onSceneChange` only sets pending flags from factory transitions.
`Hooks.onLogicStep` runs `apply_save`, `load_pack`, and `open_title`, and pulses **A** while the file-select popup is pending.

1. On factory `Scene.Factory.FileSelect`, enter popup mode: pulse **A** (up to four times), then retry `open_title()` if needed.
2. After the boot logo (`Scene.Factory.Boot` is gone), call `apply_save` until the save is applied.
3. Call `scene.load_pack` once. The default pack is `1-1`.
4. Stop when factory `Scene.Factory.CourseTask` is current or `course_task()` is true.

Forcing menu factory `Scene.Factory.Menu` early shows the time-attack trial layout.
This example never requests that factory.
Factory `Scene.Factory.SaveSelect` (`ChallengeSelectScene`) logs as unexpected and stops the skip.

[docs/modding/intro-skip.md](../../../docs/modding/intro-skip.md) records the boot tasks behind these probes.
That doc also covers open work on which confirm button the popup layout uses.

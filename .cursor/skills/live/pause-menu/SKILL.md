---
name: pause-menu
description: "Operate the in-level pause menu. Use when the check is the course pause menu, Continue, Quit, or Change Controller."
---

# Pause menu

The pause menu is the in-level menu in a story course.
It is not the world-map start menu.
The `world-map` skill owns that start menu.
The `course` skill gets a course on screen.

`nsmbu status` names this menu in `screen=`.
`screen=pause` names the selected row.
`screen=settings` is the overlay on top of it.
`behind=` names the page under that overlay.
`screen=controller` is the Change Controller page.
The story rows are Continue, Quit, and Settings.
Settings is a new button injected on the Change Controller row.
Change Controller stays in the layout and is hidden.
The blue panel keeps its original height.
Continue returns to the course.
Quit leaves the course.
`nsmbu leave` is that quit.
Done when status shows `scene=CourseSelect` and `map=1`.

That Settings row starts on.
Press `` ` `` or F12.
Open Debug.
Under World map, turn off Settings button on the pause menu to hide it.
The game builds the menu when the course loads.
Leave the course and enter it again after you change the option.
A course that loaded with the switch off keeps the three original rows.
`NSMBU_INGAME_SETTINGS=1` is required for this button.
A or Enter on Settings opens the overlay and hides this pause menu.
It does not open the controller menu.
Closing the overlay shows this pause menu again.
The course stays paused.
Continue returns to the course.
A or Enter on Settings opens the overlay again.
`nsmbu click close` clicks the X in the top right of this overlay.

## Visual check

`nsmbu test pause-settings --show` loads save slot 1.
It opens Settings, closes that overlay, then presses Continue.
It opens the pause menu again.
Close a live game first.
`nsmbu test` refuses while that game is up.
The shots stay in `.tmp/playtest/pause-settings/`.
`04-after-close.png` is the pause menu after the overlay closes.
`06-continue-a-window.png` is the screen just after Continue.
`08-pause-again.png` is the next pause menu.
Done when `08-pause-again.png` shows one panel with Continue, Quit, and Settings.
The yellow cursor is on the selected row in `04-after-close.png`, `05-continue-row.png`, and `08-pause-again.png`.
A course with no pause menu in that shot is the broken second pause.
A second copy of that panel is the broken menu.
`06-continue-a-window.png` stays on the course.
The controller page and the broken cursor stay out of that shot.
A bare `nsmbu test` does not run this case.

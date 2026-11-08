---
name: pause-menu
description: "Operate the in-level pause menu. Use when the check is the course pause menu, Continue, Quit, or Change Controller."
---

# Pause menu

The pause menu is the in-level menu in a story course.
It is not the world-map start menu.
The `world-map` skill owns that start menu.
The `course` skill gets a course on screen.

The story rows are Continue, Quit, and Settings.
Settings replaces Change Controller.
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
A or Enter on Settings plays the pause click.
The press animation finishes, then the port overlay opens.
It does not open the controller menu.
Closing the overlay returns to this pause menu.
It still does not open the controller menu.
The pause buttons stay hidden while the overlay is open.
The course stays paused.
The cursor can move again after the overlay closes.

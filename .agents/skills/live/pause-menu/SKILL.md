---
name: pause-menu
description: "Operate the in-level pause menu. Use when the check is the course pause menu, Continue, Quit, or Change Controller."
---

# Pause menu

The pause menu is the in-level menu in a story course.
It is not the world-map start menu.
The `world-map` skill owns that start menu.
The `course` skill gets a course on screen.

The story rows are Continue, Quit, and Change Controller.
Continue returns to the course.
Quit leaves the course.
`nsmbu leave` is that quit.
Done when status shows `scene=CourseSelect` and `map=1`.

Settings is a fourth row under Change Controller when Settings, Debug, Settings button on the pause menu is on.
That option starts off.
The game builds the menu when the course loads.
Leave the course and enter it again after you change the option.
A or Enter on Settings plays the pause click and opens the port overlay.
The pause buttons stay hidden while the overlay is open.
The course stays paused.
The cursor can move again after the overlay closes.

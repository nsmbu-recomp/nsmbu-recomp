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
The story rows are Continue, Quit, and Change Controller.
Continue returns to the course.
Quit leaves the course.
`nsmbu leave` is that quit.
Done when status shows `scene=CourseSelect` and `map=1`.
Change Controller opens the controller page.

---
name: scenes
description: "Boot NSMBU to a known place for a short check: the world map (overworld, CourseSelect), a course such as 1-1, or the title and file select. Use when verifying a scene, reproducing what is on screen, opening the overworld or a course, or checking frame rate in a specific place."
---

# Scenes

Read one file, run that boot, and stop when its done line matches `nsmbu status`.

- World map, overworld, or Acorn Plains: [references/overworld.md](references/overworld.md)
- A course pack such as `1-1`: [references/course.md](references/course.md)
- Title, GamePad popup, or file select: [references/file-select.md](references/file-select.md)

`nsmbu open` uses the repo `nsmbu/` settings and the active dump.
If it reports a game already running, read `nsmbu status` or run `nsmbu close`, then open again.
Poll with `nsmbu wait`.
Save a picture with `nsmbu screenshot .tmp/repro/<name>.png`.

When the check is controller input you cannot script, ask the person to do it on this debug boot and rerun the `[input]` log.
The `verify` skill has the steps.
A check that must keep passing is a playtest case.
The `cli` skill owns playtests and `nsmbu scenario` / `compare-runs`.

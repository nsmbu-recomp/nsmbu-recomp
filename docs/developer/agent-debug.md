# Check a change

Pick the smallest loop that can fail the change.
A loop is small when it can fail this change and it skips every larger check.
The steps are the `debug` skill (`.cursor/skills/debug/SKILL.md`).
This page is the inventory of the four loops, in order.

## 1. Host code

This loop uses no game dump and no game process.
It can fail a change in host code or a Python tool.

- `ctest`
- Python unittest: `python3 -m unittest`

The `ctest` recipe and the Python files are under Tier A in [verification.md](verification.md).
The loop is done when the command exits and no game process is running.

## 2. One guest function

This loop can fail one guest function.
Hand that function to the decompile subagent.
The procedure is [decomp.md](decomp.md).
A written body is checked with the decomp build.
The decompile agent runs that check.

## 3. A behavior that should match another run

This loop can fail a behavior that should match another run.
Read [automation.md](automation.md) for `NSMBU_PRESS`, `NSMBU_STICK`, and `NSMBU_REPLAY_INPUT`.
Run `tools/run/scripted-run.sh` twice, with `NSMBU_EXIT_AT_FRAME` set, and compare the logs it writes.
The run stays hidden.
There is no `nsmbu scenario` command.

## 4. A live picture

This loop can fail a live picture of a course, the world map, file select, or the pause menu.
The live-control interface is `--mcp` on the debug executable.
There is one live game.
Start it with `python3 tools/run/launch.py -- --mcp`.
That process is muted.
Close the game with `session_close` when you finish.

Do one action.
Then call `status`.
Then call `screenshot`.
Read `scene=`, `course=`, and `map=` on the status line before you read the screenshot.
Read the matching skill under `.cursor/skills/live/`:

- `course` — a course, `scene=CourseTask`, or a level from the world map
- `world-map` — the world map, the overworld, Acorn Plains, `scene=CourseSelect`, or the start menu
- `file-select` — file select, the title, the GamePad popup, or `scene=Menu`
- `pause-menu` — the course pause menu, Continue, Quit, or Change Controller

Do not run a `nsmbu` shell command.
The executable flag `--open 1-1` is how a course pack starts.

### MCP tools

- `session_close` — quits this process
- `status` — one line of `frame`, `step`, `scene`, `course`, `map`, `pack`, `player`
- `press` `{ button, frames? }`
- `stick` `{ x, y, frames? }`
- `rstick` `{ x, y, frames? }`
- `screenshot` `{ path? }` — returns the png path and the image
- `log` `{ tag?, lines? }`
- `leave`
- `goto` `{ target }`
- `ui`
- `actors` `{ limit?, radius? }`
- `picture` `{ paths }`

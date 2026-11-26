---
name: cli
description: "Build, launch, and script the native NSMBU executable with just. Use when running just build, just launch, just mcp, just game, just scripted, a warm build, or a dump switch. A live game is the debug executable with --mcp."
---

# Native game

There is no TypeScript CLI.
`just` with no arguments lists the common commands (`build`, `launch`, `decomp`, `mcp`, `game`, `extract`, `recomp`, `scripted`).
`just decomp` refreshes the decomp function list, retranslates `build/gen-decomp` when that list differs, builds `build/decomp-debug`, then starts that game.
`just decomp -- --mcp` is the muted MCP launch of that binary.
Read `docs/developer/cli.md`.
Read `docs/developer/flags.md` when you add, rename, or pass a game setting flag.
A new setting is one `ENV` line in `runtime/src/core/env.h`, passed as a `--flag` on the game binary.
Read Frame interpolation in `docs/developer/flags.md` before changing a model pose, a camera picture, or interpolation.
The `file-select`, `world-map`, and `course` skills own a short boot to that place.
Read `game/README.md` for dump folder layout.

## Warm build

`tools/build/build.py` runs `cmake -S` before every `cmake --build`.
A warm build, same compilers and flags, should run `cmake --build` only.
Every build also regenerates `build/debug/generated/nsmbu_guest_stubs.c` and relinks.
The line `0 hooks` in that step is normal.
Hooks live in `build/gen`.
Regenerate the stubs only when a `recomp/hooks/*.jsonc` file or `recomp/recomp.py` changed.
A runtime-only edit compiles the changed `.cpp` and links.

## Live game

`--mcp` is compiled into the debug executable only.
A release build does not contain the server.
The process listens on `127.0.0.1` and writes the URL to `.tmp/mcp.url`.
There is no token.
The game starts muted (`NSMBU_NO_AUDIO=1`).
One game.
Close it with the MCP tool `session_close`.

```bash
python3 tools/run/launch.py -- --mcp --open 1-1
```

Tools: `status`, `press`, `stick`, `rstick`, `screenshot`, `log`, `leave`, `goto`, `ui`, `actors`, `picture`, `session_close`.
`status` is one line of `frame`, `step`, `scene`, `course`, `map`, `pack`, and `player`.
Read that line before you read a screenshot.
The screenshot tool returns the PNG.

## Scripted boot

`tools/run/scripted-run.sh` needs `NSMBU_EXIT_AT_FRAME`.
Pad input is `NSMBU_PRESS` and `NSMBU_STICK` on the process.
Course pad logs are `tools/replay/1-1.replay` and `tools/replay/2-1.replay`, played with `NSMBU_REPLAY_INPUT`.

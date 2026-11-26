# Native executable

There is no TypeScript CLI and no `nsmbu` command on `PATH`.
The game binary is the debug or release executable under `build/`.
Agents talk to a running debug game with `--mcp`.

## Build and launch

`just` runs the common commands. `just` with no arguments lists them.

```bash
just build
just launch
just decomp
just mcp --open 1-1
```

`just decomp` refreshes the decomp function list, retranslates `build/gen-decomp` when that list differs, builds `build/decomp-debug`, then starts that game.
`python3 tools/run/launch.py --decomp` starts the binary without building.
`just decomp -- --mcp` starts that binary muted, with MCP.

The same scripts, without `just`:

```bash
python3 tools/build/build.py
python3 tools/run/launch.py
python3 tools/run/launch.py -- --mcp --open 1-1
```

`tools/run/launch.py` sets `NSMBU_PROFILE`, `NSMBU_VK_STATS`, and `NSMBU_SYNC_STATS` when those variables are unset.
`--no-debug` leaves them unset.
Extra arguments after `--` go to the executable.

A warm build, same compilers and flags, can run `cmake --build build/debug` only.
`tools/build/build.py` runs `cmake -S` first.

## `--mcp`

Debug builds only.
`--mcp` is compiled into debug builds only.
A release build has no MCP server, and `--mcp` prints that it is not in that build.
The debug server listens on `127.0.0.1` only and sets `NSMBU_NO_AUDIO=1`.
There is no token.
The log line `[mcp]` names the URL, and `.tmp/mcp.url` holds that same URL.
`POST /mcp` with no `Authorization` header.
The host must be `127.0.0.1`, `localhost`, or `[::1]`.

Tools:

- `status` — one line: `frame`, `step`, `scene`, `course`, `map`, `pack`, `player`
- `press` — `button` (name or hex) and optional `frames`
- `stick` / `rstick` — `x`, `y`, optional `frames`
- `screenshot` — optional `path`; returns the PNG
- `log` — optional `tag` and `lines`
- `leave` — leave the course
- `goto` — `target` place, such as `1-1`
- `ui`
- `actors` — optional `limit` and `radius`
- `picture` — `paths` array of PNG paths
- `session_close` — quit this process

Button names: `A` `B` `X` `Y` `L` `R` `ZL` `ZR` `Plus` `Minus` `Home` `Up` `Down` `Left` `Right`.
Any other token is hex.

Start the game yourself.
`session_close` is how you stop it.
There is no second session id.

## Scripted boot

`tools/run/scripted-run.sh` runs one hidden boot.
Set `NSMBU_EXIT_AT_FRAME` or the script refuses.
`NSMBU_PRESS` and `NSMBU_STICK` are the pad timeline.
Course pad logs live in `tools/replay/`.
Point `NSMBU_REPLAY_INPUT` at one of those files.

## Other Python tools

- `recomp/analyze.py` — branch islands in recomp output
- `tools/rpx/ppcdis.py` — disassemble guest `.text`
- `tools/frames/flicker.py` — compare frame dumps
- `tools/bench/bench_lib.py` — save-state A/B statistics
- `tools/shader/shaderprep.py` — Metal shader head-start cache

Host tests are CTest targets.

```bash
ctest --test-dir build/debug --output-on-failure
```

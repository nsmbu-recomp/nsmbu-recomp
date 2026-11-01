# Contributing

The last merge from `upstream/main` is commit `defb89f21607345e2e97b2c145a071a3f362640c`.

These commands are for a source checkout.
Players use a [release](README.md#install).

Install Node 24 or newer, CMake 3.20 or newer, Python 3, and Clang.
Link the developer CLI once: `cd tools/cli && npm link`.
After that, every command is the global `nsmbu` on `PATH`.

Put your dump in `game/`.
The file layout is in [game/README.md](game/README.md).
Initialize the LuaJIT submodule before the first build: `git submodule update --init vendor/luajit`.

| Command | Result |
|---------|--------|
| `nsmbu` | List commands (`nsmbu` with no args prints help) |
| `nsmbu game` | Pick/create a dump under `game/<id>/`, extract when ready, optional recomp |
| `nsmbu extract` | Extract the active dump only (prefer `nsmbu game`) |
| `nsmbu build` | Configure `build/debug` in Debug and build `nsmbu` |
| `nsmbu build --release` | Build in Release |
| `nsmbu build --sanitizer` | Build with AddressSanitizer and UBSan |
| `nsmbu launch` | Start `build/debug/nsmbu` with `--game` set to `game/` |
| `nsmbu launch --no-debug` | Start without the debug environment defaults |
| `nsmbu launch --trace` | Start with `--trace` for HLE logging |
| `nsmbu run` | Run `nsmbu build`, then `nsmbu launch` |
| `nsmbu run 3` | Same, and load save slot 3 |
| `nsmbu installer [--version VERSION] [--no-open]` | Build this Linux host's GitHub Release zip folder at `build/installer`, then open setup; `--no-open` returns after the package is ready |
| `nsmbu recomp` | Translate `game/code/red-pro2.rpx` into `build/gen` |
| `nsmbu launch --gdb` | Start the game under gdb |
| `nsmbu launch --lldb` | Start the game under lldb |

When `game/code/red-pro2.rpx` is newer than the generated code, `nsmbu build` runs the recompiler.
When that file is absent, the build uses stubs so the tree can link.

`nsmbu launch` sets `NSMBU_PROFILE`, `NSMBU_VK_STATS`, `NSMBU_SYNC_STATS`, and `NSMBU_CRASH_RECOVERY` when those variables are unset.

Agent control and the full command list are in [docs/developer/cli.md](docs/developer/cli.md).

Build notes and the upstream base are in [docs/developer/nsmbu/port.md](docs/developer/nsmbu/port.md).
Changes are in [CHANGELOG.md](CHANGELOG.md).

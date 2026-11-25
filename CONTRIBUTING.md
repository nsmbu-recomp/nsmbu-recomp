# Contributing

The last merge from `upstream/main` is commit `defb89f21607345e2e97b2c145a071a3f362640c`.

These commands are for a source checkout.
Players use a [release](README.md#install).

Install CMake 3.20 or newer, Python 3, and Clang.
Build and launch with `just build` and `just launch` (`just` lists every recipe).
A live agent session is the debug executable with `--mcp`.

Put your dump in `game/`.
The file layout is in [game/README.md](game/README.md).
Initialize the build submodules before the first build: `git submodule update --init vendor/fmt vendor/imgui vendor/xxHash vendor/metal-cpp`.

Build, launch, and `--mcp` are in [docs/developer/cli.md](docs/developer/cli.md).

When `game/code/red-pro2.rpx` is newer than the generated code, the build runs the recompiler.
When that file is absent, the build uses stubs so the tree can link.

`python3 tools/run/launch.py` sets `NSMBU_PROFILE`, `NSMBU_VK_STATS`, and `NSMBU_SYNC_STATS` when those variables are unset.

Build notes and the upstream base are in [docs/developer/nsmbu/port.md](docs/developer/nsmbu/port.md).
Changes are in [CHANGELOG.md](CHANGELOG.md).

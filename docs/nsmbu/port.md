# NSMBU port notes

## Upstream base

Use this commit as the last upstream recomp commit merged into this tree.

Repository: the upstream recompilation project, linked in the fork note in [README.md](../../README.md).

Branch: `main`

Commit: `defb89f21607345e2e97b2c145a071a3f362640c`

Subject: `Recompiler: mulli, mullw and neg without signed overflow in the generated C (#82)`

Author date: Fri Oct 9 11:22:57 2026 +0200

The remote name in this clone is `upstream`.

The next update is one merge, not a rebase.

Follow `.agents/skills/upstream-merge/SKILL.md`.

## What you can reuse

The upstream port and NSMBU are both Wii U titles.

`tools/recomp/recomp.py` turns one RPX into C.

`runtime/` implements Cafe OS calls and GX2 on Vulkan.

On Linux the build uses Vulkan.

The executable name in the runtime is `red-pro2.rpx`.

That file is `code/red-pro2.rpx` in the NSMBU filesystem.

The public headers in `references/headers` match NSMBU v1.3.0.

Each function comment in those headers has a guest address.
[names.md](names.md) maps those names and addresses to the names the port uses.

## Fork leftovers

These parts still need NSMBU-specific work.

- Language packs and many mods still follow upstream Cemu pack folder names.
- The build writes weak stubs for symbols the runtime still names from upstream.
  CMake runs `tools/recomp/guest_stubs.py` and compiles the output.

The upstream built-in mods and cheats are removed.

## Game files

User files live under `game/`.
Drop a `.wux` into `game/` and put keys in `game/keys.txt` (Cemu format: `common=` plus title-id lines).
`nsmbu game` renames the image to `New Super Mario Bros. U (USA).wux` or `(Europe).wux`, extracts into a version folder, and can recomp.
Recommended ids are `usa-base`, `usa-1.3.0`, `eu-base`, `eu-1.3.0`, `nsmbu-nslu-usa`, and `nsmbu-nslu-eu`.
Keep the base disc and the 1.3.0 merge in separate folders (`*-base` vs `*-1.3.0`).
A version folder only needs `code/`, `content/`, and `meta/` to play.
Disc images and `keys.txt` stay at the `game/` root.
See [game/README.md](../../game/README.md) for the layout.
`nsmbu game usa-1.3.0` activates that folder; add `--recomp` to recomp without the menu.
It writes `game/active`.
Build, launch, and playtests resolve the active folder with `game_versions.active_dir()` (for example `game/usa-1.3.0/`).
`nsmbu extract` still extracts the active dump alone if you need that step by itself.

Inside a version folder:

- `update/` — optional matching 1.3.0 update (USA title version 64, or Europe 80), with `code/`, `content/`, `meta/`.
  Without it, a known USA or Europe base-disc RPX (title version 0) is allowed.
- `code/`, `content/`, `meta/` — extract output (base disc, or base plus update for `*-1.3.0`)

Keys: `game/keys.txt`, or `WIIU_COMMON_KEY` for the common key only.

`.gitignore` ignores all of `game/` except `game/README.md`.

Extract (from `nsmbu game` or `nsmbu extract`) checks `update/` under the active dump when present.
It extracts the matching root `.wux` into `code/`, `content/`, and `meta/`.
It applies `update/` over those folders when present, then checks that `code/red-pro2.rpx` is a known build.
See [docs/builds.md](../builds.md) for the address map between those builds.

`nsmbu build` configures `build/` in **Debug** and builds `nsmbu`.

If the active dump has `code/red-pro2.rpx`, `nsmbu build` runs `recomp.py` into `build/gen` when the RPX is newer than the generated code.
A change to the contents of `hooks.txt` or a `hooks_*.txt` file does the same.

Otherwise it runs `stubgen.py` so the tree links without your RPX.

`nsmbu build --release` uses **Release**.

`nsmbu build --sanitizer` adds AddressSanitizer and UBSan.

Clang needs a GNU `libstdc++` (for example `libstdc++-14-dev` on Ubuntu).
`tools/build/build.py` adds the matching `-L` path when it finds `libstdc++.so` under `/usr/lib/gcc/`.

The `nsmbu` CLI exports `NSMBU_USER_DIR` to the gitignored `nsmbu/` folder on dev commands.
`nsmbu launch` starts `build/debug/nsmbu` with `--game` set to `game/` and `--save nsmbu/save`.
Default debug env vars (`NSMBU_PROFILE`, `NSMBU_VK_STATS`, `NSMBU_SYNC_STATS`, `NSMBU_CRASH_RECOVERY`) are set when they are not already in the environment.

`nsmbu launch --no-debug` skips those debug defaults.
It still uses `nsmbu/`.

Portable installer releases keep their own `data/nsmbu` (settings and `save/`) via `portable.txt`.
The setup importer can also read `<repo>/nsmbu` from a source checkout.

`nsmbu launch --trace` passes `--trace` for HLE logging.

`nsmbu launch --gdb` and `nsmbu launch --lldb` run the game under a debugger.

`nsmbu run` runs `nsmbu build` then `nsmbu launch`.

`nsmbu run --release` runs `nsmbu build --release` then `nsmbu launch --no-debug`.
This is the configuration the released installs use.

`nsmbu launch` / `nsmbu run` symlink each `modding/examples/` package into `nsmbu/mods/` under its folder name before start.
Real folders already in mods are not replaced.
Enable packages in Mods.
Set `NSMBU_NO_EXAMPLE_MODS=1` to skip those links.

`nsmbu run 3` loads save slot 3 after boot.
The number is the save slot.
Slots are 1 through 5.
The load starts at TV frame 200 and waits until the boot threads exist.

`nsmbu run 1-1` does not load a save state.
It passes `--open 1-1` to the native boot skip.
The native skip applies save file 1 and enters the pack.
Any `N-M` token with that shape works the same way.
A `course_res_pack` file works the same way.
`nsmbu open game/content/Common/course_res_pack/1-27.szs` starts course `1-27`.
The loading card needs the pending course, not the save's current course.
That state is in [course-open.md](course-open.md).
You cannot pass a slot and a course pack on one launch.

`nsmbu profile` runs `build/debug/nsmbu` with the fixed-scene profile env (`NSMBU_NO_AUDIO`, profile and Vulkan stats, slow-swap log, log file under `.tmp/repro/`).
Default load is slot 2 at TV frame 700; the run stops at TV frame 1100.
Pass a slot number, or `--exit-at`, `--load-at`, or `--log`.
Use a Release binary (`nsmbu build --release`) for hitch checks.

`nsmbu recomp` runs `recomp.py` only.

Install the CLI with `cd tools/cli && npm link`; see [cli.md](../cli.md).

Put an extracted game you own under `game/<id>/` and run `nsmbu game` to activate it.

Tools then use that folder’s `code/`, `content/`, and `meta/` directly.

The headers match the USA v1.3.0 executable (title `0005000010101d00`, title version 64).

A disc image without that update has a different `red-pro2.rpx`.

Addresses in `references/headers` will not match that older file.

`game/` and `*.rpx` are gitignored.

## Recompile

Use Clang.

The generated C uses `musttail`.

```bash
python3 tools/recomp/recomp.py game/usa-1.3.0/code/red-pro2.rpx build/gen
cmake -S . -B build -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
cmake --build build
```

The NSMBU executable does not call the guest stub symbols.

A generated game function with the same name replaces the stub.

The first run will miss Cafe OS imports the upstream title never called.

Add those imports in `runtime/src/hle/` after the recompiler report lists them.

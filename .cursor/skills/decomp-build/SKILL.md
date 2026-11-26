---
name: decomp-build
description: "Swap a verified C++ body from vendor/decomp in for its recompiled function. Use when adding the decomp build, when a decompiled body should run in the game, or when the user names NSMBU_DECOMP or a decomp function list."
---

# Decomp build

The normal build runs the recompiled function.
The decomp build runs the C++ body from `vendor/decomp` in its place.
The normal build stays the default.
The normal link omits `vendor/decomp`.

The build is in the tree.
`tools/decomp_build/gen.py` writes the function list.
`tools/decomp_build/drop.txt` names sources that stay recompiled.
`cmake/Decomp.cmake` compiles the wrappers.
`recomp/recomp.py --decomp-hooks` writes `build/gen-decomp`.

## Run

1. Refresh the list.
   `python3 tools/decomp_build/gen.py --out .tmp/decomp-kept --drop tools/decomp_build/drop.txt`
   Done when `.tmp/decomp-kept/decomp_hooks.txt` exists.

2. Translate the USA game into `build/gen-decomp`.
   Pass that list to `recomp/recomp.py --decomp-hooks`.
   Leave `build/gen` unchanged.
   Done when `build/gen-decomp/decomp_hooks.txt` matches the list.

3. Build `build/decomp-debug`.
   `python3 tools/build/build.py --dir build/decomp-debug -- -DNSMBU_DECOMP=ON -DGEN_DIR=$PWD/build/gen-decomp`
   The binary is `build/decomp-debug/nsmbu`.
   `tools/run/scripted-run.sh` picks `build/debug/nsmbu` first, so name this binary yourself.
   Done when that binary exists.

4. Check with two hidden boots from `docs/developer/agent-debug.md` loop 3.
   Use the same `NSMBU_TEST_ORIGIN` input on the normal build and the decomp build.
   Ask before you launch the game.
   Done when you can point at the first step where the two runs differ, or at a matching trace.

## Slots

Default is every listed slot on.
`NSMBU_DECOMP=none` runs `f_ADDR_orig` for every slot.
`NSMBU_DECOMP_FUNCS` is a comma-separated address list, and only those slots stay on.
`NSMBU_DECOMP_EXCLUDE` is a comma-separated address list, and those slots stay off.
A value other than `all` turns every slot off.
`none` wins over `NSMBU_DECOMP_FUNCS`.

A source in `drop.txt` stays recompiled.
A call from a compiled body to a function this build does not provide hits `decomp_missing`.
That prints `decomp: missing` and aborts.
Turn that slot off, or name the function before you add a guest trampoline.

The frame loop (`seadGameFrameworkCafeFrameLoop.cpp`) calls guest function pointers as host pointers.
Those calls fault while that slot is on.
Name that function before you add a trampoline for it.

A decompiled caller that calls a port-hooked function by C++ name skips the port hook.
Guest entry to that address still hits the port hook.

## Limits

The C++ body uses guest addresses.
It does not use Wind Waker's `gabi.h`.
The body runs on the host stack.
Add Wind Waker's unit isolation (`ld -r`) or its guest stack-frame rebuild only when a function needs that behavior.
Say which function needs it before you add it.
Generated glue and the function list stay in the build directory.
Game files stay gitignored.

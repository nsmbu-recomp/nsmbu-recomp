---
name: decomp-build
description: "Swap a verified C++ body from vendor/decomp in for its recompiled function. Use when adding the decomp build, when a decompiled body should run in the game, or when the user names NSMBU_DECOMP or a decomp function list."
---

# Decomp build

The normal build runs the recompiled function.
The decomp build runs the C++ body from `vendor/decomp` in its place.
The normal build stays the default.
The normal link omits `vendor/decomp`.

This build is not in the tree yet.
Follow the steps below to add it.

## Read first

Read these files before you edit.

- `vendor/ZeldaWWHDRecomp/docs/decomp-build.md`
- `vendor/ZeldaWWHDRecomp/cmake/Decomp.cmake`
- `vendor/ZeldaWWHDRecomp/runtime/src/decomp/decomp.h`
- `vendor/ZeldaWWHDRecomp/tools/recomp/recomp.py` (`--decomp-hooks`)
- `recomp/recomp.py` (a hooked function is `hook_ADDR` plus `f_ADDR_orig`)
- `docs/developer/env.md` (one new setting is one `ENV` line)

Done when you can name the function list, the hook, and the kept list.

## Steps

1. Collect the candidates from `vendor/decomp`.
   A candidate has a definition.
   The line above that definition is `// Address: 0xXXXXXXXX`.
   A declaration with no body stays recompiled.
   Done when the function list is those addresses, one per line, in the build directory.

2. Give the decomp build its own generated code directory.
   Leave the normal `GEN_DIR` unchanged.
   Done when the two directories are different paths.

3. Teach `recomp/recomp.py` a `--decomp-hooks FILE` flag, as Wind Waker does.
   Each listed entry becomes `f_ADDR`, which calls `hook_ADDR`.
   The game body becomes `f_ADDR_orig`.
   Copy `FILE` to `GEN_DIR/decomp_hooks.txt`.
   Refuse a `GEN_DIR` whose copy does not match `FILE`.
   An address that is not a function entry stays recompiled.
   A function that contains an instruction hook (`@ADDR` in `recomp/hooks.txt`) stays recompiled.
   Record each kept address where the runtime can read it.
   Wind Waker records that list as `g_recomp_decomp_kept`.
   Done when a listed function compiles to `hook_ADDR` and `f_ADDR_orig`.

4. Keep every address already in `recomp/hooks.txt` or `recomp/hooks_*.txt` on the port hook.
   That function stays recompiled.
   Done when the decomp slot for that address is off.

5. Write `hook_ADDR` in the runtime.
   When the slot is on, the hook calls the C++ body.
   When the slot is off, the hook calls `f_ADDR_orig`.
   Read the parameters from the guest call.
   Match them to the C++ signature.
   Write the return value back into the guest return register.
   Done when one listed function takes that path.

6. Add the switches in `runtime/src/core/env.h`.
   Default is every listed slot on.
   `NSMBU_DECOMP=none` runs `f_ADDR_orig` for every slot.
   `NSMBU_DECOMP_FUNCS` is a comma-separated address list, and only those slots stay on.
   `NSMBU_DECOMP_EXCLUDE` is a comma-separated address list, and those slots stay off.
   Done when each name is one `ENV` line and the hook reads it.

7. Check with two hidden boots from `docs/developer/agent-debug.md` loop 3.
   Use the same `NSMBU_TEST_ORIGIN` input on the normal build and the decomp build.
   Done when you can point at the first step where the two runs differ, or at a matching trace.

## Limits

The C++ body uses guest addresses.
It does not use Wind Waker's `gabi.h`.
The body runs on the host stack.
Add Wind Waker's unit isolation (`ld -r`) or its guest stack-frame rebuild only when a function needs that behavior.
Say which function needs it before you add it.
Generated glue and the function list stay in the build directory.
Game files stay gitignored.

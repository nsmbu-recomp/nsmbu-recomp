---
name: guest
description: "Guest code f_XXXXXXXX, hooks, recomp build/gen, guest_stubs, PPC_MEM_BASE and ea, headers_names.txt, names.py, regional rebuild addresses, threads. Use when editing generated game code, hooks, recompiler output, stubs, guest memory, or naming symbols by USA v1.3.0 address."
---

# Guest code

Each guest function is `void f_XXXXXXXX(Cpu* c)`.

`XXXXXXXX` is the address in USA v1.3.0 `red-pro2.rpx`.

That address is the id in hooks and in the runtime.

A regional rebuild shifts addresses through the map in `docs/builds.md` and keeps these symbol names.

Guest address `ea` is the host address `PPC_MEM_BASE + ea` (`0x200000000000` in `runtime/include/ppc.h`).

Loads and stores are host accesses plus a byte swap.

One guest thread runs on an emulated core at a time.

`runtime/src/core/threads.cpp` applies Cafe OS priority on host threads.

`tools/recomp/recomp.py` writes `build/gen/code_*.c`, `table.c`, and `imports.c`.

`hooks.txt` and `hooks_*.txt` name the call sites the runtime replaces.

`guest_stubs.py` writes weak stubs for symbols the runtime still imports.

CMake runs that script on every build.

A generated game function with the same name replaces the stub.

A normal build reads `tools/recomp/headers_names.txt` from git.

`names.py` refreshes that file from a local `references/headers` checkout.

`references/` checkouts are gitignored and are not part of the build.

## Docs

Read `docs/names.md` to name guest symbols (USA v1.3.0 address is the key), or for sead, particles, G3d, Miis, layouts, sound, or Nintendo file-format trees.
Read `docs/how-it-works.md` for the runtime split.
Read `docs/builds.md` for regional address maps.
Read `docs/vulkan.md` when you change the Vulkan backend.
Read `docs/modding/README.md` when you change mod load, content redirects, or the Lua API.

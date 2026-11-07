---
name: decompile
description: "Decompile NSMBU USA v1.3.0 into vendor/decomp with ppcdis.py. Use when decompiling functions, answering an open question on a decomp function, or running the decomp work queue."
disable-model-invocation: true
---

# Decompile (REDPRO2)

## Objective

Continue the `vendor/decomp` decompilation of NSMBU USA v1.3.0, working fully autonomously: spawn subagents to implement decompilation, keep finding new work for further subagents, and commit each piece as it lands.

No responses needed from the user, keep going until the queue is empty or blocked.

## Requirements

- Decompiled C++ must be written into `vendor/decomp/src` and `vendor/decomp/include`.
- A discovery about original game code is a comment on that function's declaration in `vendor/decomp/include`.
  State what the function does, and any side effect or requirement for calling it.
  The `.cpp` body keeps the `// Address:` marker and the code.
- Never invent sead/agl names when no tree has the symbols, keep the address and describe the site.
- Decompiled code must match existing `vendor/decomp` style (`// Address: 0x...` markers, `L_` labels, raw offsets).

## Decisions

- Use `python3 tools/rpx/ppcdis.py ADDR [COUNT] [RPX]` plus `recomp/headers_names.txt` as the only naming sources.
  `game/active` selects the dump.
- Unresolved helpers get `sub_<ADDRESS>` names plus an `// Address:` comment rather than invented names.
- An open question is a comment on the function declaration it asks about.
  The answer replaces that comment on the same declaration.
- Three nested repos exist: parent `nsmbu-recomp`, `vendor/decomp`, and `vendor/decomp/include` (submodule `headers.git`, branch `recomp`).
  Each needs its own commit.

## Repos and commits

Commit order on every landed batch:

1. `vendor/decomp/include` - new or updated headers, including the declaration comment.
2. `vendor/decomp` - `.cpp`.
3. Parent - submodule gitlink bumps.

Push all three when the user expects sync (autonomous runs: push after each batch).

`vendor/decomp` has no build system.
Added `.cpp` and `.h` files are documentation only and are never compiled.

## Naming and disassembly

1. Resolve the symbol in `recomp/headers_names.txt` (class + name for the USA v1.3.0 address).
2. Disassemble: `python3 tools/rpx/ppcdis.py 0xADDR [instruction_count]`.
3. For cross-refs and rodata, sweep these PowerPC address materialisations together: `addi` (14), `addic` (12), record-form addic (13), and `ori` (24).
   An `addi`-only sweep misses `lis` + `addic` sites and yields a false "no reference".
4. Name call targets from `headers_names.txt` or `ppcdis` annotations.
   Everything else is `sub_029B1234` with `// Address: 0x029B1234`.
5. Optional sead/agl/nw names only when the matching tree exists under `.tmp/nsmbu-sead/` or documented vendor paths (see [reference.md](reference.md)).

## Code style

Match nearby decomp files:

- File-level and per-function `// Address: 0x...` on decompiled functions and anonymous helpers.
- Member access via `reinterpret_cast` and byte offsets, filler arrays like `_1C[(0x3D0-0x1C)/4]`.
- Internal asm labels as `L_029B5450` (or project convention in the file you extend).

Style references:

- Small complete unit: `vendor/decomp/src/sound/SndAudioMgr.cpp`
- Full method: `vendor/decomp/src/course_select/CourseSelectLayerMgr.cpp`
- Large function + `sub_` helpers: `vendor/decomp/src/graphics/ReflectionRenderObjLayer.cpp`
- Header layout: `vendor/decomp/include/graphics/ReflectionRenderObjLayer.h`

Leave known-inaccurate legacy decomp alone unless the task explicitly fixes it (e.g. `vendor/decomp/src/system/GameFrame.cpp` and root task at `+0x1C`).

## Open questions

An open question is one comment on the function declaration in `vendor/decomp/include`.
The answer replaces that comment.
Sweep `addi` (14), `addic` (12), record-form `addic` (13), and `ori` (24) together.
The compiler materialises a static address with `lis` + `addic` often enough that an `addi`-only sweep reports a false "no reference".

## Autonomous work loop

Plan for bounded parallel batches, avoid duplicate symbols across agents.

### Build the queue

Named symbols in `recomp/headers_names.txt` minus addresses already present as `// Address: 0x` in `vendor/decomp/src` and `vendor/decomp/include`.
Regenerate that missing-symbol list after each batch before spawning the next wave.

### Subagent instructions (decomp batch)

Hand each subagent one class, at most 25 functions:

- Disassemble each address and write `.h` / `.cpp` in repo style.
- Unknown names stay `sub_` plus the address.
- The orchestrator commits.

### Subagent instructions (open question)

One question from a comment on a function declaration.
Research with `ppcdis`, rodata sweeps, and reloc inspection.
Replace that comment with the answer on the same declaration.
The orchestrator commits.

### Orchestrator

1. Spawn decomp batches and open-question subagents in parallel where claims do not overlap files.
2. Review landed diffs for style and naming rules.
3. Commit include, then decomp, then the parent gitlink bumps, and push.
4. Regenerate the queue and take the next class batch.

## Scratch tools

Temp files under `.tmp/` only (not `/tmp/`):

- `.tmp/dumpw.py`, `.tmp/cstr.py` - data-word and C-string readers when needed.
- `.tmp/nsmbu-sead/` - sead, agl, nw_ptcl sources for verified naming.

## Domain reference

Tables, singleton addresses, struct layouts, and PPC pitfalls: [reference.md](reference.md).

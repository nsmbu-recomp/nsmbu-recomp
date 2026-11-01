---
name: decompile
description: >-
  Continue vendor/decomp (REDPRO2) decompilation of NSMBU USA v1.3.0: disassemble
  with ppcdis.py, write C++ under vendor/decomp/src and include, manage docs/TODO.md
  claims, triple-repo commits, and autonomous subagent batches from headers_names.txt.
  Use when decompiling game functions, expanding REDPRO2, answering decomp TODOs, or
  running the decomp work queue.
disable-model-invocation: true
---

# Decompile (REDPRO2)

## Objective

Continue the `vendor/decomp` decompilation of NSMBU USA v1.3.0, working fully autonomously: spawn subagents to implement decompilation, keep finding new work for further subagents, and commit each piece as it lands.

No responses needed from the user; keep going until the queue is empty or blocked.

## Requirements

- Decompiled C++ (not only prose notes) must be written into `vendor/decomp/src` and `vendor/decomp/include`.
- One claim per TODO item; checked items are removed from `vendor/decomp/docs/TODO.md` and the answer goes into the note it came from.
- Never invent sead/agl names when no tree has the symbols; keep the address and describe the site.
- Decompiled code must match existing `vendor/decomp` style (`// Address: 0x...` markers, `L_` labels, raw offsets).
- Add each change to `CHANGELOG.md` under `[Unreleased]`.
- Discoveries about original game code go in `vendor/decomp/docs` (per `AGENTS.md`).

## Decisions

- Use `python3 tools/rpx/ppcdis.py ADDR [COUNT] [RPX]` plus `recomp/headers_names.txt` as the only naming sources; `game/active` selects the dump.
- Unresolved helpers get `sub_<ADDRESS>` names plus an `// Address:` comment rather than invented names.
- Treat `vendor/decomp/docs/TODO.md` as the open-claim list.
- Three nested repos exist: parent `nsmbu-recomp`, `vendor/decomp`, and `vendor/decomp/include` (submodule `headers.git`, branch `recomp`). Each needs its own commit.

## Repos and commits

Commit order on every landed batch:

1. `vendor/decomp/include` — new or updated headers.
2. `vendor/decomp` — `.cpp`, docs under `docs/`, `docs/TODO.md` edits.
3. Parent — submodule gitlink bumps plus `CHANGELOG.md` `[Unreleased]`.

Push all three when the user expects sync (autonomous runs: push after each batch).

`vendor/decomp` has no build system; added `.cpp`/`.h` are documentation only and are never compiled.

## Naming and disassembly

1. Resolve the symbol in `recomp/headers_names.txt` (class + name for the USA v1.3.0 address).
2. Disassemble: `python3 tools/rpx/ppcdis.py 0xADDR [instruction_count]`.
3. For cross-refs and rodata, sweep PowerPC address materialisation together: `addi` (14), `addic` (12), `addic.` (13), and `ori` (24). An `addi`-only sweep misses `lis` + `addic` sites and yields false "no reference".
4. Name call targets from `headers_names.txt` or `ppcdis` annotations; everything else is `sub_029B1234` with `// Address: 0x029B1234`.
5. Optional sead/agl/nw names only when the matching tree exists under `.tmp/nsmbu-sead/` or documented vendor paths (see [reference.md](reference.md)).

## Code style

Match nearby decomp files:

- File-level and per-function `// Address: 0x...` on decompiled functions and anonymous helpers.
- Member access via `reinterpret_cast` and byte offsets; filler arrays like `_1C[(0x3D0-0x1C)/4]`.
- Internal asm labels as `L_029B5450` (or project convention in the file you extend).

Style references:

- Small complete unit: `vendor/decomp/src/sound/SndAudioMgr.cpp`
- Full method: `vendor/decomp/src/course_select/CourseSelectLayerMgr.cpp`
- Large function + `sub_` helpers: `vendor/decomp/src/graphics/ReflectionRenderObjLayer.cpp`
- Header layout: `vendor/decomp/include/graphics/ReflectionRenderObjLayer.h`

Leave known-inaccurate legacy decomp alone unless the task explicitly fixes it (e.g. `vendor/decomp/src/system/GameFrame.cpp` and root task at `+0x1C`).

## TODO claims

Before research-only work, read `vendor/decomp/docs/TODO.md`.

- At most one open checkbox per research question.
- Subagent completes the answer in the linked `docs/*.md` note, removes the checkbox from `TODO.md`, commits in the decomp repo.

## Autonomous work loop

Plan for bounded parallel batches; avoid duplicate symbols across agents.

### Build the queue

Named symbols in `recomp/headers_names.txt` minus addresses already present as `// Address: 0x` in `vendor/decomp/src` and `vendor/decomp/include`.

```bash
python3 - <<'PY'
import re
from pathlib import Path

root = Path("vendor/decomp")
addrs_decomped = set()
for p in list(root.glob("src/**/*.cpp")) + list(root.glob("include/**/*.h")):
    addrs_decomped.update(re.findall(r"// Address: 0x([0-9A-Fa-f]+)", p.read_text()))

names = []
for line in Path("recomp/headers_names.txt").read_text().splitlines():
    parts = line.split()
    if len(parts) >= 3 and parts[0].startswith("0x"):
        addr = parts[0][2:].upper()
        if addr not in addrs_decomped:
            names.append((parts[0], parts[1], parts[2]))

from collections import Counter
by_class = Counter(cls for _, cls, _ in names)
for cls, n in by_class.most_common(40):
    if n >= 5:
        print(f"{n:4d}  {cls}")
Path(".tmp/decomp_queue_summary.txt").write_text(
    "\n".join(f"{n}\t{cls}" for cls, n in by_class.most_common())
)
missing = [x for x in names if x[1] == "RenderObjRenderMgr"]  # example filter
Path(".tmp/decomp_batch_RenderObjRenderMgr.txt").write_text(
    "\n".join(f"{a}\t{cls}::{sym}" for a, cls, sym in missing)
)
print(f"named={len(names)+len(addrs_decomped)} missing={len(names)} decomped={len(addrs_decomped)}")
PY
```

Regenerate the missing-symbol list after each batch before spawning the next wave.

### Subagent instructions (decomp batch)

Hand each subagent a bounded list (e.g. one class, ≤25 functions) with:

- Disassemble each address; write `.h` / `.cpp` in repo style.
- Do not invent sead/agl names; use `sub_` + address for unknowns.
- Do not commit (orchestrator commits).

### Subagent instructions (TODO item)

One claim from `TODO.md`: research via `ppcdis`, rodata sweeps, reloc inspection; write the answer into the target doc; remove the TODO line; do not commit unless orchestrator.

### Orchestrator

1. Spawn decomp batches and TODO subagents in parallel where claims do not overlap files.
2. Review landed diffs for style and naming rules.
3. Commit include → decomp → parent + `CHANGELOG.md`; push.
4. Regenerate queue; queue next classes (e.g. `ActorMgr`, `ScoreMgr`, `SubjectMgr`, `PlayerBase`, `Enemy`, `LineMgr`, `Shell`, `Kuribo` after mgr batches).

## Scratch tools

Temp files under `.tmp/` only (not `/tmp/`):

- `.tmp/dumpw.py`, `.tmp/cstr.py` — data-word and C-string readers when needed.
- `.tmp/nsmbu-sead/` — sead, agl, nw_ptcl sources for verified naming.

## Domain reference

Tables, singleton addresses, struct layouts, and PPC pitfalls: [reference.md](reference.md).

---
name: decomp-port
description: "Migrate one decompiled function from vendor/decomp recomp onto origin/master, verify boot, commit on decomp-port branches. Use when porting decomp slots one at a time, NSMBU decomp migration, or spawning Composer subagents for the next function."
---

# Decomp port (one function at a time)

The port runs on published `origin/master` in `vendor/decomp`, not the old `recomp` / `ffdf282` pin.
The parent repo stays on `main`.
Guest sources and headers land on `decomp-port` in `vendor/decomp`, `vendor/decomp/include`, and optionally `nsmbu-recomp` only when the user asks to bump the submodule on `main`.

Read `decomp-build` and `debug` before each slot.
Read `cli` for `just decomp`.

## Branches

| Tree | Branch | Remote |
|------|--------|--------|
| `vendor/decomp` | `decomp-port` | `origin/decomp-port` |
| `vendor/decomp/include` | `decomp-port` | `origin/decomp-port` |
| `nsmbu-recomp` | `main` | do not switch away unless asked |

Reference the old bodies from a local export of `recomp` (for example `.tmp/decomp-recomp` from `git archive recomp`).

## Order

1. Pick the next address from `.tmp/recomp-hooks/decomp_hooks.txt` that is not already in `.tmp/decomp-kept/decomp_hooks.txt`.
2. Spawn a **Composer** subagent (`composer-2.5`) with the address, source path on `recomp`, and the rules below.
3. Build and boot-verify before any commit.

## Subagent rules (one slot)

- Add **one** `// Address: 0x........` body per commit.
- Copy from `.tmp/decomp-recomp` or `git show recomp:path`.
- Add headers from `git show recomp:include/...` only when this slot needs them.
- Use `decomp::be32` / `decomp::call` / `decomp::make_fn` for guest words and guest calls.
- **Placement-new hooks** call `new (guest) Class()`.
  If the class has a virtual destructor or host-sized members (`OffsetList`, etc.), fix the header first (guest-sized storage, non-virtual destructor) like `AttentionMgr`.
- Stub link-only virtuals without an Address tag when the vtable must link but the slot stays recompiled.

## Verify

1. `python3 tools/build/build.py --dir build/decomp-debug -- -DNSMBU_DECOMP=ON -DGEN_DIR=$PWD/build/gen-decomp`
   (`just decomp` also refreshes the hook list and retranslates when it changed.)
2. Close any live game with MCP `session_close`.
3. `python3 tools/run/launch.py --decomp -- --mcp`
4. Poll `status` until `scene=Map`, `scene=Menu`, or `scene=Title` with no `CRASH` and no `[sched] ... waiting >10s`.
5. `session_close`.

Done when the new slot is on in `[decomp] N functions, N on` and the scene check passes.

## Commit and push (decomp-port only)

Commit in this order:

1. `vendor/decomp/include` when headers changed.
2. `vendor/decomp` (sources + include gitlink).
3. `nsmbu-recomp` only when the user asks to update the submodule pointer on `main`.

Push `decomp-port` on headers and decomp after each verified slot.
Do not commit game files.

## Pitfalls

- Stale `build/gen-decomp` lists from the full `recomp` tree crash or hang boot until `build.py` retranslates.
- `AttentionMgr` froze Prepare Thread until `OffsetList` was removed from the header.
- `just decomp -- --mcp` needs `--` before game flags (`launch.py` strips it).
- Full `recomp` does not compile with the current `drop.txt`; migrate slowly, do not bulk-checkout `recomp`.
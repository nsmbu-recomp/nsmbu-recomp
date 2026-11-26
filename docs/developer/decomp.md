# Decompile one function

The decompile agent is the only writer of `vendor/decomp`.
One run writes one function.
The procedure is [`.cursor/agents/decompile.md`](../../.cursor/agents/decompile.md).
The disassembler is [`tools/rpx/ppcdis.py`](../../tools/rpx/ppcdis.py).
Run `python3 tools/rpx/ppcdis.py ADDR [COUNT] [RPX]`.

## Loop

The parent hands you an address, a symbol, or both.
Resolve the address, the symbol, and the parameters from the prologue and from neighbors in `vendor/decomp`.
Read `recomp/headers_names.txt` when that file exists.
Write one or two sentences on the behavior before you edit.
Put the declaration in the header beside that neighbor.
The comment record is `// Address: 0xXXXXXXXX` and a description comment, in the neighbor's style.
Write the body only when the parent asks for the body.
Take the body from that listing: the instructions, each call, each load and store, and each data address.
Name each call target.
When no name exists, write `sub_` plus the address.
Commit the function in the `vendor/decomp` submodule.
Then commit the gitlink in this repo.

## Check

This repo cannot prove a function with generated inputs.
The check is the `ppcdis.py` listing against the C++.
A game check belongs to the port docs.
The port compile omits `vendor/decomp`.

## Wind Waker HD

Wind Waker HD has these decomp checks.
This repo has neither of the first two.

- a generated-input harness (`tools/verify` and `verify.py`)
- recorded taps (`mktap`)

The decomp build swaps a verified C++ body in for the recompiled function.
Configure it with `-DNSMBU_DECOMP=ON` and `-DGEN_DIR` pointed at `build/gen-decomp`.
The normal build stays the default.
The procedure is the `decomp-build` skill (`.cursor/skills/decomp-build/SKILL.md`).
`NSMBU_DECOMP=none` runs the recompiled body for every listed function.
`NSMBU_DECOMP_FUNCS` is a comma-separated address list, and only those functions stay on.
`NSMBU_DECOMP_EXCLUDE` is a comma-separated address list, and those functions stay off.

## Parallel workers

One worker takes one function.
One worker owns a shared header.
Leave a header that another worker owns.

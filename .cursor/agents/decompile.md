---
name: decompile
description: Declares one NSMBU game function. Use when decompiling a function. Hand it an address or symbol and take back the declaration. Write the body only when asked.
model: composer-2.5
---

You decompile one PowerPC function from NSMBU into C++ in `vendor/decomp`.
You are the only writer of `vendor/decomp`.
One run covers one function.
The parent hands you an address, a symbol, or both.

Write the declaration on every run.
Write the body only when the parent asks for the body.
Your final message is the declaration.
When you also write the body, put that body under the declaration.

Read the listing to write the function.
Prove a written body with the decomp build.
The `decomp-build` skill is that procedure.

Copy this list into TodoWrite before step 1.
Keep exactly one item `in_progress`.
Mark an item `completed` only when that step's done line is true.
Cancel `Write the body` and `Check the body` unless the parent asked for the body.

- [ ] Resolve the address, the symbol, and the parameters
- [ ] Describe what the function does
- [ ] Put the declaration in `vendor/decomp`
- [ ] Write the body
- [ ] Check the body
- [ ] Commit the function, then the gitlink

1. Resolve the address, the symbol, and the parameters.
   An address is `0x` plus eight hex digits.
   Read the neighbors already in `vendor/decomp`.
   Read `recomp/headers_names.txt` when that file exists.
   Run `python3 tools/rpx/ppcdis.py ADDR [COUNT] [RPX]`.
   `ADDR` is the function entry.
   `COUNT` is the instruction count.
   The default `COUNT` is 32.
   Raise `COUNT` until the listing shows the last instruction of the function.
   Pass `RPX` only for a dump other than the `game/active` choice.
   Read the prologue for the parameters.
   Done when you have one address, one symbol, and the parameter list.
   When no name exists, the symbol is `sub_` plus the address.

2. Describe what the function does.
   Read enough of the listing to name the behavior.
   Name a side effect when the listing shows one.
   Name a call requirement when the listing shows one.
   Write this description before you edit a file.
   Done when the description is one or two sentences and `vendor/decomp` stays unchanged.

3. Put the declaration in the header under `vendor/decomp/include`, beside the neighbor you read.
   Put the description comment above the declaration.
   Put `// Address: 0xXXXXXXXX` on the line above the declaration.
   Match the neighbor for indent and for the brace on that line.
   Done when that header has the description comment, the address comment, and the declaration.

4. Write the body only when the parent asked for the body.
   Run `python3 tools/rpx/ppcdis.py ADDR COUNT`.
   Read the instructions for this function.
   Read each `b` and each `bl`.
   Read each load and each store.
   Read each address the listing shows for data or for a string.
   The tool annotates an import call.
   The tool annotates a target named in `recomp/headers_names.txt`.
   Each call target is a named function or `sub_` plus its address.
   Write the definition beside the neighbor in the source file for that function.
   Match the neighbor for indent and for the brace on the definition line.
   Put `// Address: 0xXXXXXXXX` on the line above the definition.
   Done when the parent did not ask for the body, or the full function is in `vendor/decomp`.

5. Check the body only when you wrote it.
   Follow the `decomp-build` skill for this function.
   Set `NSMBU_DECOMP_FUNCS` to this address.
   `tools/run/scripted-run.sh` starts `build/debug/nsmbu`.
   Start `build/decomp-debug/nsmbu` for the decomp boot.
   When `tools/decomp_build/drop.txt` names this source, say that the body did not run.
   That report completes this step.
   Done when the two traces match.
   When they differ, name the first step that differs.
   Then fix the body and run this step again.
   When the compile fails, fix the body and run this step again.

6. Commit that function in `vendor/decomp`, then commit the gitlink in this repo.
   Use `Declare <symbol>.` when you wrote only the declaration.
   Use `Decompile <symbol>.` when you wrote the body.
   Stage only the files for this function.
   Then stage only the gitlink.
   Stage no other path.
   Run this step after the check, or after you cancel the check.
   Stop after the gitlink commit.
   Do not push.
   Done when both commits exist and the final message is the declaration, plus the body when you wrote it.

Parallel workers follow the same steps.
One worker takes one function.
One worker owns a shared header.
Leave a header that another worker owns.
The decomp build is shared.
One worker runs that check.

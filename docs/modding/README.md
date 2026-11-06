# Modding

This folder is the modding documentation for NSMBU recomp.

## Live

| Doc | What it covers |
| --- | --- |
| [mod-manager.md](mod-manager.md) | In-game Mods tab, install, storage, shared manifest fields |
| [cemu.md](cemu.md) | `kind: cemu` graphics and shader packs |
| [Guest UI / layout hooks](../../vendor/decomp/docs/ui-system.md#mod-integration) | Pause, `Multi2D`, and native menu hook map (decomp note; C++ hooks today, Lua hooks phase 4) |

Rejected on install: `kind: lua`, `kind: content`, `kind: native`, `kind: guest`, and `kind: settings`.
The LuaJIT dependency and the sources under `modding/` stay in the tree.

## Proposal (not implemented)

| Doc | What it covers |
| --- | --- |
| [lua-mods.md](lua-mods.md) phases 4–5 | Hooks, `nsmbu.Hooks.call`, and hard rejection of the content alias |
| [api.md](api.md) (hooks, calls, input, data, guest heap) | Host APIs still marked proposal in that doc |

Example packages: [../../modding/examples/](../../modding/examples/) (TypeScript sources with the compiled Lua next to them).
Mod SDK: [../../modding/sdk/](../../modding/sdk/).

## Investigation

| Doc | What it covers |
| --- | --- |
| [intro-skip.md](intro-skip.md) | Native boot path; open work is which confirm button the popup uses |

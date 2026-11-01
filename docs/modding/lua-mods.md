# Lua mods: replace native, guest, and content packages

Status: **Phase 3 implemented** (`content_dir` on Lua packages; `kind: content` is a one-release read-only alias and migrates to `lua` on scan).
The runtime embeds LuaJIT 2.1 and loads `kind: lua` packages.
Logic-step callbacks, config, log, status, and guest memory work now.
Optional `content_dir` on a Lua package drives the same file-replacement map as the old content kind.
See [api.md](api.md) for the live surface and for APIs that are still proposal.

Later phases stay proposal: hooks and `nsmbu.call`, and hard rejection of `kind: content` after the alias window.
Cemu graphics packs stay.
This plan retires Mod SDK v2 and Native SDK v1.
It also retires `kind: content` as its own package kind (alias and migration land in phase 3; hard reject follows).

The player-facing manager stays.
The file-replacement behaviour that content packages used stays inside the engine.
Lua packages are the way to turn that behaviour on, apart from Cemu packs.
`kind: content` is accepted as a read-only alias this release and rewritten to `lua` on scan.

The script surface is [api.md](api.md).

## Decision

Ship one scripting package kind, `lua`.
Delete the other package kinds except `cemu`.

| Package kind today | Target |
| --- | --- |
| `native` (SDK v1, `dlopen` of a host library) | Removed. Lua covers the same jobs (phase 2 logic-step path). |
| `guest` (SDK v2, PowerPC ELF, install-time translation) | Removed. Lua covers hooks and calls (phase 4). |
| `content` (SDCafiine-style file replacement) | Alias this release, then removed as a kind. A Lua package can ship a `content/` tree. |
| `settings` | Removed. Stays rejected. |
| `cemu` | Unchanged. |

One package is one folder or one `.nsmbumod` zip.
It has `manifest.json` at the root.
It may contain `main.lua`, a `content/` tree, or both.
It does not contain a host `.so`, `.dll`, or `.dylib`.
It does not contain a PowerPC ELF.
Mod sources are TypeScript compiled to `main.lua`; see [../../modding/README.md](../../modding/README.md).

## Why one scripting kind

Native v1 and guest v2 solve different problems with two toolchains.

Native v1 (`runtime/include/nsmbu_mod.h`) is a C ABI.
The mod exports `nsmbu_mod_init_v1`.
The runtime loads a library built for that OS and CPU.
The host calls `on_frame` once per logic step.
The mod can read options, log, set a status line, and read or write guest RAM.
The player must confirm the library hash before it loads.
A mod author who wants Linux, Windows, and macOS must ship three binaries.
The API cannot hook a game function.

Guest v2 is a removed archive.
The author writes C for 32-bit big-endian PowerPC.
The package is one ELF on every OS.
At startup the port translates that ELF to C and compiles it with the local toolchain.
Hooks and replacements use `PPC_MOD_HOOK` in the generated game functions.
The author needs clang with a PowerPC target and `ld.lld`.
The player needs a game build with `--mod-hooks`, and often a rebuild through `mods/code_mods.cpp`.
The prototype is off by default.
Android guest modules are unsupported.
The trust dialog treats the ELF like native code.

Content packages do not run code.
They replace files the game opens (`runtime/src/mods/content.cpp`).
They need a restart.
They have no options and no dependencies.
Fan translations and model swaps use this path.
The importer also accepts loose packs and simple `content/` trees.

Those three paths duplicate install, enable, conflict, and profile logic in `runtime/src/mods/packages.cpp`.
They also force three different author workflows.

Lua is one workflow.

- The package is text and data files.
- The same files run on every desktop OS.
- A logic-step callback replaces native `on_frame`.
- Named hooks replace guest `RECOMP_HOOK` / address hooks.
- A `content/` directory replaces `kind: content`.
- The engine embeds the interpreter.
- Mod folders do not get `dlopen`.

Cemu packs stay a separate kind.
They are not scripts.
They select shader and resolution rules for the renderer (`runtime/src/mods/cemu_pack.cpp`).
A Lua mod must not become a second shader compiler.

## What Cemu keeps

Do not fold Cemu into Lua.

Keep these behaviours as they are in [mod-manager.md](mod-manager.md) and [content.md](content.md):

- `kind: cemu` and `cemu_dir`.
- `rules.txt` versions 4 and 5.
- USA title id `0005000010101d00`.
- Preset categories as dropdowns.
- Width, height, depth, format filters, and `overwriteWidth` / `overwriteHeight`.
- GLSL shader replacement on Vulkan, including hash-named `vs` / `ps` files.
- The official NSMBU aspect-ratio table mapped to the port projection adapter.
- Restart to enable, disable, or change a preset.
- Rejection of `patches.txt` and other instruction patches.
- No native-code confirmation for Cemu packs.

Lua packages must not ship `rules.txt`, shader files, or `patches.txt`.
The importer refuses a mixed Lua and Cemu archive the same way it refuses a content pack that also contains a code patch.

## What Lua must cover

The replacement is complete only when a Lua package can do every job that native, guest, and content packages do today.
Cemu jobs are out of scope.

### From native v1

Phase 2 covers this table.

| Native v1 | Lua |
| --- | --- |
| `on_frame(logic_step)` once per logic step, not per interpolated draw | `function nsmbu.Hooks.onLogicStep(step)` |
| (no native equivalent) guest `Scene::setNextScene` | `function nsmbu.Hooks.onSceneChange(factory)` |
| `onConfigChanged` | `function nsmbu.Hooks.onConfigChanged()` |
| `onUnload` | `function nsmbu.Hooks.onUnload()` |
| `get_string` / `get_number` / `get_bool` | `nsmbu.Config` reads |
| `log` and `status` | `nsmbu.log` and `nsmbu.status` |
| `read_guest` / `write_guest`, big-endian, at most 1 MiB | `nsmbu.Guest` typed helpers |
| Common course reads (timer, coins, score, lives, power-up, pos, speed, grounded) | `nsmbu.Level` and `nsmbu.Player` |
| Options, dependencies, conflicts in the manifest | Same manifest fields |
| Runs on the game thread | Same rule |

Callbacks must not start worker threads that touch guest memory.
The runtime drops the library-unload problem because there is no host library.

### From content packages

Phase 3 implemented.
`kind: content` remains a one-release read-only alias and migrates to `kind: lua` on scan.

| Content package | Lua package |
| --- | --- |
| `content_dir` of game-relative files | Optional `content_dir` on a `lua` manifest |
| Restart to activate | Same. The file map is fixed for the process. |
| Case-insensitive replacement of existing reads | Same engine map in `content.cpp` |
| No new files, no deletes, no writes through the replacement | Same limits |
| Conflicts if two packages replace one path | Same check, across Lua packages |
| SDCafiine, loose `.pack`, and single-file import | Importer writes a `lua` package, not `kind: content` |
| Fan translations (`permanent_2d_*.pack`, `Title_00.szs`) | Same files. The package can omit `main.lua`. |

A content-only Lua package has a manifest and a `content/` tree.
It has no script.
The loader only registers files.
Language-pack behaviour in [content.md](content.md) stays.
The package kind name in the UI is Lua.

### From guest SDK v2

Phase 4 proposal (hooks and calls).

| Guest v2 | Lua |
| --- | --- |
| Entry hook on a game function | `nsmbu.Hooks.hook(address, "entry", fn)` |
| Return hook | `nsmbu.Hooks.hook(address, "return", fn)` |
| Replace a function | `nsmbu.Hooks.replace(address, fn)` |
| Call the original body | `nsmbu.Hooks.original()` from inside a replacement |
| Call another game function | `nsmbu.Hooks.call(address, args)` |
| Read config at startup | `nsmbu.Config` (live; no restart for script options) |
| `nsmbu_log` and friends | `nsmbu.log` |
| Per-mod guest heap | `nsmbu.Guest.alloc` / `free`, or a Lua table when the data must not live in guest RAM |
| `nsmbu_file_read` / `write` under `mod-data/<id>` | `nsmbu.Data` |
| `nsmbu_input_read` | `nsmbu.Input` |
| `nsmbu_logic_dt` / `nsmbu_logic_step` | `nsmbu.logic_dt()` and the step argument |
| One replacement per function, many hooks | Same conflict rule |
| Port hooks stay outside mod hooks | Same. Frame interpolation runs first. |
| Save-state identity of enabled mods | Lua package id and version, not a guest ELF region |

Guest v2 also planned a HUD service.
That service was not in phase 1.
Lua now provides `nsmbu.Gui` for shapes and text.
It is not required to retire v2.

Game calls use guest addresses from the public headers (`references/headers`, see [names.md](../developer/names.md)).
The Lua runtime should ship a generated `nsmbu.Fn` table of those names.
A mod can still pass a raw address.
USA addresses are the authoring target.
The European build map in SDK v2 (`tools/recomp/builds`) must still translate hook targets and `nsmbu.Hooks.call` targets.
Object field offsets are not translated.
That limit is the same as in SDK v2.

## Package

```json
{
  "$schema": "../../sdk/manifest.schema.json",
  "format_version": 2,
  "id": "example.coin-counter",
  "name": "Coin counter",
  "version": "1.0.0",
  "game_id": "nsmbu-usa",
  "kind": "lua",
  "lua": {
    "api_version": 1,
    "entry": "main.lua"
  },
  "content_dir": "content",
  "dependencies": [],
  "conflicts": [],
  "options": [
    {
      "id": "label",
      "name": "Status prefix",
      "type": "string",
      "default": "Coins"
    }
  ]
}
```

Phase 3 accepts `kind: lua` with an optional `content_dir`.
`lua.entry` is optional when `content_dir` is set.
`content_dir` is optional when `lua.entry` is set.
At least one of them is required.
A content-only package (manifest plus `content/`, no script) is valid.
`kind: content` is accepted as a read-only alias this release and migrates to `lua` on scan.
`format_version` becomes 2 when `kind: lua` is the only non-Cemu kind.
A version 1 manifest with `native` or `guest` fails install with a message that names this document.
After the alias window, `content` fails the same way.
`abi_version` and `binaries` are not valid on a Lua package.
`guest` is not valid.

`game_id` stays `nsmbu-usa` for packages that target the USA executable.
The runtime still checks the RPX identity, as it does today.

### Example script

These snippets use the calls in [api.md](api.md).
This script matches the native fixture in `runtime/test/mod_fixture.cpp`, plus a guest read.
The address is a placeholder.
A real mod uses a named field from the generated address table.

```lua
local step = 0

function nsmbu.Hooks.onLogicStep(logic_step)
  step = step + 1
  if step % 30 ~= 0 then
    return
  end
  local coins = nsmbu.Guest.read_u32(nsmbu.Fn.example_coin_count)
  local prefix = nsmbu.Config.string("label", "Coins")
  nsmbu.status(prefix .. ": " .. tostring(coins))
end
```

A hook that replaces the guest `cLib_addCalc2` example looks like this.
The address is `0200ED84` (`NSMBU_ADDR_cLib_addCalc2`).
The body is illustrative.
`nsmbu.Fn.cLib_addCalc2` is a generated name.
It exists only when the header export includes that function.

```lua
nsmbu.Hooks.replace(nsmbu.Fn.cLib_addCalc2, function(ctx)
  nsmbu.Hooks.original()
end)
```

`ctx` holds r3–r10 and f1–f8.
A return hook sees those registers after the original function.
A replacement that does not call `nsmbu.Hooks.original` skips the game body.
The port's own hooks in `hooks*.txt` stay outside this call.

### Files the package may contain

| Path | Role |
| --- | --- |
| `manifest.json` | Required. |
| `main.lua` and other `.lua` files | Script. `require` searches only inside the package. Optional when `content_dir` is set. |
| `content/...` | Optional file replacements. Same layout rules as today. |
| `mod-data/<id>/` | Created by the runtime for `nsmbu.Data`. Not shipped in the zip. |

Reject `.so`, `.dll`, `.dylib`, `.elf`, `.rpx`, `patches.txt`, `.asm`, shader files, and `rules.txt` inside a Lua package.
Reject `code/`, `meta/`, and DLC trees inside `content/`, as the content importer does today.

## Runtime shape

The `nsmbu` executable embeds LuaJIT 2.1 (MIT license), a git submodule at `vendor/luajit` that is built static with FFI disabled.
It uses Lua 5.1 semantics.
Do not load a Lua shared library from the mod folder.

The standard library is base, `coroutine`, `string`, `table`, `math`, and `bit`.
There is no `utf8` library and no `bit32` library.
Do not use syntax or helpers from other Lua dialects: no type annotations, no `continue`, no `+=`, no generalized iteration (`for k, v in t` is invalid; use `ipairs` / `pairs`), and no `table.create`, `table.clone`, `string.split`, or `math.clamp`.

Each enabled Lua package gets its own state.
Packages cannot see each other's globals.
Call order follows the existing dependency order in `packages.cpp`.
Hooks on one function run in that same order.
Return hooks run in reverse order, as guest v2 does.

Run every Lua callback on the game thread.
A callback error is a package fault.
Log the Lua message, unload that package, and leave the others running.
Do not abort the process.

A package with `content_dir` needs a restart so the file map and any script start together.
A script-only package (no `content_dir`) still loads live on enable.
With `hot_reload` on in `mods.json`, a script-only package also reloads while enabled when its `.lua` files change.
Reload runs `nsmbu.Hooks.onUnload`, then creates a new Lua state and runs the entry script again.
A failed reload disables that package the same way a failed first load does.

### Hooks without a PowerPC toolchain

Keep the cheap flag check from SDK v2 if measurements stay acceptable.
`recomp.py` emits `PPC_MOD_HOOK` at function entry.
The flag byte is set only when a Lua mod hooks or replaces that function.
An unhooked function pays one load and a not-taken branch.
SDK v2 already measured that cost and kept it.

Delete the rest of the guest pipeline.

On a hit, the runtime builds a small register table and calls the Lua function.
It does not translate mod PowerPC.
It does not place mod code in guest RAM.
Mod functions are not guest function pointers.
If a game function pointer must point at mod code, that remains unsupported.
Document it as a limit.
Guest v2 solved it by copying mod code into the guest region at `0x7F000000`.
Lua does not get that region.

`nsmbu.call` writes the argument registers and calls the existing translated function (`ppc_dispatch` or the direct `f_XXXXXXXX` entry).
The call is slower than a compiled guest mod.
It is fast enough for gameplay scripts.
It is the wrong tool for a replacement of a function that runs thousands of times per frame.
The mod author uses `nsmbu.replace` for that function so the Lua body runs in place of the game body.

Compile the hook flags into the default game build once Lua hooks exist.
Then delete the opt-in code-mod rebuild (`runtime/src/mods/code_mods.cpp` and the Settings control that calls it).
Players should not rebuild the game to turn scripting on.

### Guest memory

Keep the native v1 bounds.

- Reads and writes cover MEM2, MEM1, and the foreground bucket.
- One request is at most 1 MiB.
- Multi-byte values are big-endian.
- Typed helpers (`read_u8`, `read_u16`, `read_u32`, `read_f32`, and the matching writes) hide the byte order.

A bad address returns an error to Lua.
It does not crash the host.
Do not expose a raw pointer into guest RAM to Lua.

`nsmbu.Guest.alloc` is optional.
Use it only when the game must hold a pointer to mod bytes.
The block lives in a per-mod guest heap with the same caps SDK v2 planned (default 256 KiB, maximum 8 MiB).
Pure Lua data stays in the Lua heap and is not part of the guest save image.

### Content files

`content.cpp` stays.
`content::activate` and `content::replacement` stay.
The package scanner migrates installed `kind: content` records to `kind: lua` on scan.
On startup it collects `content_dir` from enabled Lua packages and builds the same immutable map.
Script code cannot add or remove replacements after startup.
A script that needs a different file set edits the package and restarts, which is the rule today.

The importer stays.
It accepts a `content/` tree, an SDCafiine layout, a loose language pack, and a known loose file.
It writes `kind: lua`, a generated `id`, and `content_dir`.
It does not write a fake `main.lua`.
A content-only package is valid.

### Data files and input

`nsmbu.Data.read` and `nsmbu.Data.write` use `mod-data/<id>/` under the host config directory.
Flat names only.
No `..`, symlinks, or absolute paths.
Maximum 1 MiB per call.
This matches the guest v2 file service.

`nsmbu.Input` returns the current pad snapshot.
It is read-only.
It does not inject buttons.
Button injection can be a later API with its own design.
It is not required to replace v1 or v2.

## Sandbox and trust

Native and guest packages run as native code after confirmation.
`mods.json` stores `native_trust` as a SHA-256 of the library or ELF.
Lua packages do not get that dialog.
They also do not get full process rights.

The base libraries available to a package are the ones listed below.
Everything else is absent, including `io`, `os`, `package.loadlib`, `debug`, and any FFI.

| Allowed | Purpose |
| --- | --- |
| `nsmbu.*` | The API in this document. |
| `string`, `table`, `math` | Language basics. |
| `coroutine` | Coroutines. |
| `bit` | Bitwise operations. |
| `require` | Other `.lua` files in the same package. |

Guest writes can still change the game.
That is the point of a gameplay mod.
The Mods tab should say that a Lua package can change guest memory and replace files.
That sentence is not the native-code confirmation.
There is no host-library hash to store.
Delete `native_trust` after migration.
Delete `guest_regions`.

Do not offer a supported way to load a native library from Lua.
A later "escape hatch" would recreate SDK v1 and the trust dialog.
This proposal does not include that hatch.

## Save states

Guest mods record enabled mod id and version in the portable state (`runtime/src/save/portable_state.cpp`, section handled with `guestmods::ModIdentity`).
Replace that list with enabled Lua packages (id and version).
Keep the warning when the set differs.
Drop guest RAM regions for mod code and the guest heap from the state format, unless `nsmbu.Guest.alloc` exists.
If the heap exists, store it as a per-id blob with a size cap.
A content-only package is part of the set because it changes what the game reads.
Cemu packs stay out of that list unless they already affect a state.
They do not change guest code.

Old states that contain guest-mod records still load.
Show the existing mod-set warning.
Do not try to run the old ELF.

## Removal

Do this only after Lua covers the tables above and the tests below exist.
Guest SDK v2 is already removed.
Native SDK v1 is already removed.
This document is the reason to stop extending either path.

### Documents and examples

| Item | Action |
| --- | --- |
| `docs/modding/mod-manager.md` | Keep. Live kinds are `lua` and `cemu`; file replacement is documented in [content.md](content.md). |
| `examples/guest-mods/` | Removed. Packages are in `modding/examples/`. |

### Guest SDK v2 code

| Item | Action |
| --- | --- |
| `tools/guestmod/` | Removed. |
| `runtime/guest/` | Removed for the guest-mod ABI path. A Lua address table needs a new generator later. |
| `runtime/src/mods/guest_mods.cpp` and `guest_mods.h` | Removed. A Lua hook dispatcher can replace the entry later if the flag check remains. |
| `guest_validation.h`, `guest_heap.h`, `guest_files.h`, `guest_build.h`, `guest_identity.h`, `guest_state_section.h` | Removed. |
| `runtime/src/mods/code_mods.cpp` and `code_mods.h` | Removed with the rebuild UI. |
| `runtime/test/guest_*_test.cpp`, `code_mods_test.cpp` | Removed. |
| `.github/workflows/guestmods.yml` | Removed. |
| `PPC_MOD_HOOK` / `--mod-hooks` | Removed for guest mods. Reintroduce only if a Lua hook dispatcher needs the flag check. |
| `Cpu::mod_skip` and `g_mod_bodies` | Removed with the guest loader. Reintroduce only if Lua replacements call the original body through that skip. |

### Native SDK v1 code

| Item | Action |
| --- | --- |
| `runtime/include/nsmbu_mod.h` | Removed. |
| `runtime/test/mod_fixture.cpp` | Removed. Replace with a Lua fixture when Lua lands. |
| `kind == "native"` load path in `packages.cpp` (`dlopen` / `LoadLibrary`, `nsmbu_mod_init_v1`) | Removed. |
| Native confirmation dialog and `native_trust` | Removed after installed native packages are rejected. |
| `NSMBU_TEST_TRUST_NATIVE_MODS` | Removed. |

### Content kind

| Item | Action |
| --- | --- |
| `kind: content` in the manifest parser | Alias this release; migrate to `lua` on scan; reject after the alias window. |
| `runtime/src/mods/content.cpp` | Keep. Call it from the Lua startup path. |
| Content tests (`mod_content_*`) | Retarget them at Lua packages with `content_dir`. |
| Importer for SDCafiine, loose packs, and single files | Keep. Emit `kind: lua`. |

### Manager and states

| Item | Action |
| --- | --- |
| `packages.cpp` kinds | Allow `lua` and `cemu` (plus `content` alias this release). |
| `guest_regions` in `mods.json` | Ignore, then drop on the next save. |
| `native_trust` | Ignore, then drop on the next save. |
| Restart rules | Content files and Cemu still need a restart. Script hooks and options apply on enable when the package has no `content_dir`. A package with both restarts, so the file map and the script start together. |

### Player migration

Installed `native` and `guest` packages do not convert.
On the first run after the change, disable them and show "This package uses a removed mod format. Install a Lua package instead."
Installed `content` packages are rewritten in place to `kind: lua` with the same `content_dir` and the same id.
Keep their options empty.
Keep their enabled bit.
Cemu packages load as they do now.

## Tests to add before deletion

- Load a Lua package, run `nsmbu.Hooks.onLogicStep`, and read an option.
- A script error unloads that package only.
- `require` cannot escape the package directory.
- `io` and `os` are nil.
- Guest read and write honour the 1 MiB cap and big-endian layout.
- Two packages cannot replace one function.
- Two packages cannot replace one content path.
- A content-only Lua package changes a synthetic file open and does not create a Lua state.
- A Cemu package still enables without loading Lua.
- A v1 `native`, `guest`, or (after the alias window) `content` manifest fails install.
- A portable state records Lua ids and warns on a mismatch.
- The hook flag stays clear when no package hooks that function.

Host tests can use a fake guest memory buffer.
They do not need a game dump.
One scripted play run (see `docs/developer/automation.md`) should enable a tiny Lua status mod from a slot-1 state and check the log line.

## Phases

1. **API freeze.** Done.
   Accept this document.
   Stop new guest-mod features and new native ABI fields.
2. **Interpreter and `kind: lua`.** Done.
   Embedded LuaJIT.
   Logic-step, config, log, status, and guest memory.
   Keep content kinds working until phase 3.
3. **Content moves.** Done.
   Allow `content_dir` on Lua packages.
   Point the importer at `kind: lua`.
   Keep `kind: content` as a read-only alias for one release, then reject it.
4. **Hooks and `nsmbu.call`.** Proposal.
   Turn hook flags on in the default build.
   Delete the code-mod rebuild flow.
5. **Removal.** Proposal.
   Finish any leftover cleanup in the tables above.
   Bump the manager so v1 code packages fail closed.

Native SDK v1 and Guest SDK v2 code are already removed.
Phase 3 replaces content packages with Lua `content_dir` (alias residual this release).
Phase 4 replaces the guest v2 hook jobs in Lua.
Do not ship Lua hooks without a hook test.

## Risks

| Risk | Why it matters | Mitigation |
| --- | --- | --- |
| Lua on a hot function | A replacement of a per-particle or per-draw function can miss the frame time. | Document the cost. Offer `nsmbu.original`. Measure one hot hook before calling the API done. |
| Sandbox holes | Guest write is a gameplay tool and also a crash tool. | Bound sizes. No FFI. Fault one package. |
| Address drift | USA v1.3.0 addresses will not match another update. | Keep `game_id` and the RPX check. Generate `nsmbu.Fn` from the headers. |
| European builds | Hook addresses differ. | Keep the build map from SDK v2. Refuse an unmapped target. |
| Function pointers into mod code | Lua functions have no guest address. | State the limit. Do not revive the guest ELF heap for code. |
| Content and script restart split | A script that expects new files can run before the map updates. | One restart when `content_dir` is set. |
| Loss of native speed | Some mods want a tight inner loop or a third-party library. | Those mods wait, or they stay out of tree. This proposal does not keep `dlopen`. |
| Cemu and Lua in one zip | Players will try to combine a shader pack and a script. | Reject the package. Ask for two packages. |

## Open decisions

- Interpreter: **LuaJIT 2.1**.
  A git submodule at `vendor/luajit`, built static with FFI disabled.
- Whether `format_version` 2 is required, or `kind: lua` is enough on version 1.
- Whether content-only packages show as "Lua" or as "Files" in the Mods list while the manifest kind stays `lua`.
- Whether `nsmbu.Guest.alloc` is in the first hook release or a later one.
- Whether button injection exists at all.
- Named parameters on hooks, taken from header signatures.
  API v1 uses registers only.
  See [api.md](api.md).
- How long v1 manifests remain installable as errors versus a hard parser break.

## Prior art

These notes are design input only.
Do not copy their code without a license review.

| Project | What to copy as an idea |
| --- | --- |
| Mod SDK v2 (removed) and N64Recomp | Hook versus replace, one replacement, many hooks, port hooks stay outside, config schema, dependency order. Drop the MIPS/PPC ELF and the JIT. |
| Zelda64Recomp mod manager | Install, enable, and options in a menu. Already the model for [mod-manager.md](mod-manager.md). |
| SM64coopDX / other Lua game mods | A small `hook` table and per-mod script files. Their APIs are game-specific. |
| LuaJIT | A locked standard library for untrusted scripts (chosen; the host embeds LuaJIT 2.1). |

## Current code this plan talks about

| Topic | Location |
| --- | --- |
| Native ABI | Removed (`runtime/include/nsmbu_mod.h`) |
| Load, kinds, trust, frame tick | `runtime/src/mods/packages.cpp` |
| Lua embed and `kind: lua` (phase 2) | `runtime/src/mods/lua_runtime.cpp`, `lua_guest.cpp`, `lua_press.cpp` (LuaJIT from `vendor/luajit`, FFI off) |
| File replacement via Lua `content_dir` (phase 3) | `runtime/src/mods/content.cpp` |
| Cemu (keep) | `runtime/src/mods/cemu_pack.cpp` |
| Guest hooks | Removed (`runtime/src/mods/guest_mods.cpp`) |
| Code-mod rebuild | Removed (`runtime/src/mods/code_mods.cpp`) |
| Guest toolchain | Removed (`tools/guestmod/`) |
| Hook emission | Removed for guest mods (`--mod-hooks` in `tools/recomp/recomp.py`) |
| Mod identity in states | `runtime/src/save/portable_state.cpp` |

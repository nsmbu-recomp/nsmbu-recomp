# Lua API (version 1)

Status: **Phase 2 partial**.
Lifecycle fields on `nsmbu.Hooks` (`onLoaded`, `onLogicStep`, `onSceneChange`, `onConfigChanged`, and `onUnload`), package metadata, log and status, time, config, guest memory, high-level `nsmbu.Level` / `nsmbu.Player` helpers, level actor reads / spawns, and `nsmbu.Scene` flow probes and transitions are implemented with embedded LuaJIT 2.1.
Function hooks (`hook` / `replace` / `call` on `nsmbu.Hooks`), input pad reads, data files, guest heap alloc, and `nsmbu.Fn` stay proposal until later phases.
Package plan: [lua-mods.md](lua-mods.md).
Cemu packs do not use this API.

The runtime exposes one global, `nsmbu`.
It holds the lifecycle fields, the log and status helpers, and one field per module.

```lua
nsmbu.Guest.read_u32(0x105E2C80)
nsmbu.Level.set_time(999)
nsmbu.Player.get_player(0)
```

Mods are authored in TypeScript and compiled to Lua with TypeScriptToLua.
The definitions for the global live in [../../modding/sdk/nsmbu.d.ts](../../modding/sdk/nsmbu.d.ts).
`modding/tsconfig.json` sets the Lua 5.1 target with inlined helpers, so the compiled `main.lua` loads in the runtime LuaJIT without extra files.
See [../../modding/README.md](../../modding/README.md) for the build commands.
The snippets below show the Lua surface that the compiled scripts call.
A host function that can fail returns `nil` and a message; TypeScript models that pair with `LuaMultiReturn<[T | undefined, string | undefined]>`.
The runtime wiring that builds that table lives in [../../runtime/src/mods/sdk/nsmbu.cpp](../../runtime/src/mods/sdk/nsmbu.cpp) and mirrors the declaration module by module.

`require("nsmbu")` and `require("nsmbu.*")` still resolve to the same tables, but new mods use the global.
Names that start with `nsmbu` are reserved for the runtime.
Each package has its own Lua state, so one package cannot replace another package's global.

| Module | Role | Status |
| --- | --- | --- |
| `nsmbu` | Log, status, time, package | Live |
| `nsmbu.Config` | Package option reads | Live |
| `nsmbu.Guest` | Guest memory read/write (heap alloc later) | Live (R/W) |
| `nsmbu.Level` | Course timer, course coins, score, live actors, spawn | Live |
| `nsmbu.Player` | Player handles and iterator | Live |
| `nsmbu.Scene` | Boot flow phase probes and scene transitions | Live |
| `nsmbu.Fn` | Generated function addresses | Proposal (empty table) |
| `nsmbu.Hooks` | Lifecycle callbacks (live); hook, replace, original, call (proposal) | Partial |
| `nsmbu.Input` | Pad snapshot, button helpers | Proposal (`press` injection is live) |
| `nsmbu.Data` | Per-package data files | Proposal (empty table) |
| `nsmbu.Enemy` / `nsmbu.Boss` / `nsmbu.Blocks` / `nsmbu.Env` | Future game domains | Stub empty tables |

## Rules

- Call these functions on the game thread only.
- A host function returns `nil` and a string message on failure.
- It does not raise a Lua error for a bad address, a missing file, or a full heap.
- A raised error inside a callback unloads that package only.
- Integer registers and guest integers are in the range `0` .. `4294967295`, except the signed read helpers.
- Float registers are Lua numbers.
- Guest multi-byte values are big-endian.
- One guest read or write is at most 1 MiB.
- Strings passed to the host are copied before the call returns.
- Do not keep a guest pointer across frames unless you allocated it with `nsmbu.Guest.alloc` (proposal).

## Lifecycle

Implemented in phase 2.

Assign one function per event on `nsmbu.Hooks`.
Omit a field when the package does not need that event.

```lua
function nsmbu.Hooks.onLogicStep(step)
end

function nsmbu.Hooks.onSceneChange(factory)
end

function nsmbu.Hooks.onConfigChanged()
end

function nsmbu.Hooks.onLoaded()
end

function nsmbu.Hooks.onUnload()
end
```

| Field | Type | When it runs |
| --- | --- | --- |
| `onLogicStep` | `(step: number) -> ()` | Once per original logic step, after actor execution. Interpolated draws do not call it. `step` is the full logic-step counter. |
| `onSceneChange` | `(factory: number) -> ()` | After guest `Scene::setNextScene` returns. `factory` is the class creator id of the **requested** next scene, not a guarantee that scene is already current. Compare it to `nsmbu.Scene.Factory` names instead of hardcoding USA hex. Do not call `nsmbu.Scene` transition helpers from this callback; set flags and act on `onLogicStep` so a nested `setNextScene` cannot re-enter the handler mid-transition. |
| `onConfigChanged` | `() -> ()` | After the player edits an option for this package. |
| `onLoaded` | `() -> ()` | Once right after the entry script returns, before the first logic step. |
| `onUnload` | `() -> ()` | When the package is disabled or the profile changes. Process exit does not promise this call. |

Package order follows manifest dependencies.

## Package

Implemented in phase 2.

`nsmbu.Package` is read-only.

| Field | Type | Meaning |
| --- | --- | --- |
| `id` | string | Manifest id. |
| `version` | string | Manifest version. |
| `path` | string | Absolute directory of the installed package. |

Do not write these fields.
The next host call replaces the table if a script overwrites it.

## Log and status

Implemented in phase 2.

```lua
nsmbu.log("ready")
nsmbu.log_int("coins", 12)
nsmbu.log_hex("addr", 0x105E2C80)
nsmbu.log_float("dt", nsmbu.logic_dt())
nsmbu.status("Coins: 12")
```

| Function | Result |
| --- | --- |
| `log(message)` | Writes one line to the game log, tagged with the package id. Returns `true`. |
| `log_int(label, value)` | Same, with a decimal integer. |
| `log_hex(label, value)` | Same, with an 8-digit hex integer. |
| `log_float(label, value)` | Same, with a number. |
| `status(message)` | Sets the Mods tab status line for this package. The host keeps at most 1024 bytes. Returns `true`. |

`label` and `message` must be strings.
A bad type returns `nil` and a message.

## Time

Implemented in phase 2.

```lua
local step = nsmbu.logic_step()
local seconds = nsmbu.logic_dt()
```

| Function | Result |
| --- | --- |
| `logic_step()` | The same counter passed to logic-step callbacks. |
| `logic_dt()` | Length of the current logic step in seconds. |

## Config

Implemented in phase 2.

Values come from the Mods tab.
They match the manifest `options` entry.

```lua
local name = nsmbu.Config.string("label", "Coins")
local every = nsmbu.Config.number("poll_every", 30)
local on = nsmbu.Config.bool("enabled", true)
```

| Function | Result |
| --- | --- |
| `nsmbu.Config.string(id, fallback)` | The string option, or `fallback` when the id is missing or not a string. Enum and radio options return the selected choice, not the radio label. |
| `nsmbu.Config.number(id, fallback)` | The number, or `fallback` when the id is missing or not a number. |
| `nsmbu.Config.bool(id, fallback)` | The boolean, or `fallback` when the id is missing or not a boolean. |

`id` is the option id, not the display name.
`fallback` is required.
A wrong `fallback` type returns `nil` and a message.

Option edits are visible on the next `nsmbu.Config.*` call.
They also fire `config_changed` callbacks.
A package with `content_dir` still restarts before new files apply.
The script options themselves do not wait for that restart.

## Guest memory

Implemented in phase 2 (reads and writes).
Guest heap alloc is still proposal.

```lua
local value = nsmbu.Guest.read_u32(0x105E2C80)
nsmbu.Guest.write_u32(0x105E2C80, value + 1)

local bytes = nsmbu.Guest.read(0x105E2C80, 16)
nsmbu.Guest.write(0x105E2C80, bytes)
```

Reads and writes cover MEM1, MEM2, and the foreground bucket.
An address outside those ranges returns `nil` and `"address out of range"`.
A size above 1 MiB returns `nil` and `"size too large"`.

| Function | Result |
| --- | --- |
| `read_u8(addr)` | Integer `0` .. `255`. |
| `read_s8(addr)` | Integer `-128` .. `127`. |
| `read_u16(addr)` | Integer `0` .. `65535`. Big-endian. |
| `read_s16(addr)` | Integer `-32768` .. `32767`. Big-endian. |
| `read_u32(addr)` | Integer `0` .. `4294967295`. Big-endian. |
| `read_s32(addr)` | Signed 32-bit integer. Big-endian. |
| `read_f32(addr)` | Lua number. Big-endian IEEE-754 binary32. |
| `read_f64(addr)` | Lua number. Big-endian IEEE-754 binary64. |
| `write_u8(addr, value)` | `true` on success. The value is truncated to 8 bits. |
| `write_s8(addr, value)` | `true`. Truncated to 8 bits. |
| `write_u16` / `write_s16` | `true`. Truncated to 16 bits. Big-endian. |
| `write_u32` / `write_s32` | `true`. Truncated to 32 bits. Big-endian. |
| `write_f32` / `write_f64` | `true`. Big-endian. |
| `read(addr, size)` | A Lua string of `size` bytes. |
| `write(addr, data)` | `true`. `data` is a Lua string. |

Alignment is not required.
A partial object that crosses a valid region boundary fails the whole call.

### Guest heap

Proposal (not in phase 2).

Use this only when game code must hold a pointer to bytes you own.
Lua tables do not need it.

```lua
local block = nsmbu.Guest.alloc(256)
if block then
  nsmbu.Guest.write_u32(block, 1)
  nsmbu.Guest.free(block)
end
```

| Function | Result |
| --- | --- |
| `alloc(size)` | A 16-byte-aligned guest address, or `nil` and `"out of memory"`. |
| `free(addr)` | `true`. `free(0)` is `true`. A pointer this package did not allocate returns `nil` and a message. |

The default heap is 256 KiB.
`lua.heap_size` in the manifest raises it, up to 8 MiB.
`alloc(0)` returns `nil` and a message.
The heap is per package.
Save states store it with that package id when this function exists.

## Level and players

Implemented in phase 2.

These helpers wrap USA v1.3.0 guest layout for common course scripts.
Prefer them over raw `nsmbu.Guest` reads when they cover the job.
Addresses stay inside the host.

```lua
if nsmbu.Level.active() then
  nsmbu.Level.set_time(999)
  nsmbu.Level.set_coins(0)
  nsmbu.Level.set_score(0)
end

local p = nsmbu.Player.get_player(0)
if p and p:entered() then
  p:set_lives(99)
  p:set_mode(p:mode())
  local pos = p:pos()
  if pos and p:grounded() then
    p:set_pos(pos.x, pos.y, pos.z)
  end
end

for p in nsmbu.Player.players() do
  local lives = p:lives()
end
```

### `nsmbu.Level`

| Function | Result |
| --- | --- |
| `active()` | `true` when the course timer singleton is present. |
| `time()` | Remaining time in whole seconds, or `nil` and a message. |
| `set_time(seconds)` | Writes the timer. Values clamp to `0` .. `999`. |
| `add_time(seconds)` | Adds (or subtracts) seconds, then clamps. |
| `coins()` / `set_coins(n)` | Course-wide coin total from `FieldGame`. |
| `score()` / `set_score(n)` | Course score from `FieldGame`. |

### `nsmbu.Player`

The module is callable: `nsmbu.Player(index)` is the same as `nsmbu.Player.get(index)`.
`nsmbu.Player.players()` returns an iterator over entered players only.

`nsmbu.Player(index)` / `nsmbu.Player.get(index)` returns a player handle for `index` `0` .. `3`, or `nil` when `FieldGame` is missing or the index is bad.

| Method | Result |
| --- | --- |
| `entered()` | `true` when that slot has an entered player. |
| `lives()` / `set_lives(n)` | Field lives. Fails with `"not entered"` when the slot is empty. |
| `mode()` / `set_mode(n)` | Power-up mode `0` .. `8`. `mode` prefers the actor value when it is valid. `set_mode` writes field data and the actor when present. |
| `coins()` / `set_coins(n)` | Field coin count for that player. |
| `pos()` | Actor position as `{ x, y, z }`. Fails with `"no player actor"` when the actor is missing. |
| `set_pos(x, y, z)` | Writes actor position. You may also pass one table `{ x, y, z }`. |
| `speed()` | Actor speed as `{ x, y, z }`. Fails with `"no player actor"` when the actor is missing. |
| `set_speed(x, y, z)` | Writes actor speed. You may also pass one table `{ x, y, z }`. |
| `speed_y()` / `set_speed_y(v)` | Actor vertical speed only. Same actor requirement as `speed`. |
| `grounded()` | `true` when the actor foot collision reports ground. Fails with `"no player actor"` when the actor is missing. |
| `actor()` | Guest address of the player actor, or `nil` and a message. |

`p.index` is the slot number.
Do not keep a player handle across frames and treat it as a stable guest pointer.
Call `nsmbu.Player` / `nsmbu.Player.get` or `nsmbu.Player.players` again each logic step.

### Level actors and spawn

Live course actors, actor handles, and spawning.
These wrap `ActorMgr`, `ActorBase`, and `Profile` from the USA v1.3.0 layout.

```lua
for _, a in nsmbu.Level.actors() do
  local pid = a:profile_id()
  if pid == 467 then
    local pos = a:pos()
  end
end

nsmbu.Level.spawn(467, 100, 0, 0, { layer = 0, param0 = 0 })
```

| Function | Result |
| --- | --- |
| `actors()` | Array of actor handles for the live actor cache. The array holds every live actor, including menu actors outside `pos` use. |
| `count(profile_id?)` | Number of live actors, or of one profile when passed. |
| `spawn(profile_id, x, y, z, options?)` | Writes an `ActorCreateParam` and calls `ActorMgr::createLater`. Returns `true`. The actor appears on the next logic step, not in the same step. `options` may set `param0`, `param1` (both `0`), and `layer` (`0`) |

Spawns run on the game thread only.
A course must be running; otherwise the call fails.
An unknown or unregistered profile id fails with `"unknown profile"`.
Profile numbers come from the game's profile list:
coin `395`, goomba `467`, mushroom `591`, fire flower `592`, one-up `597`, star `853`.
`param0` / `param1` are the level designer nybbles ("spritedata").

Each handle is a table with `addr` (the guest address) and these methods:

| Method | Result |
| --- | --- |
| `address()` | Guest address of the `ActorBase`. |
| `id()` | `ActorUniqueID` value. |
| `profile_id()` | Profile id of the actor. |
| `is_actor()` | `true` when the runtime type derives from `Actor` (a course actor with position, speed, and scale). Menu and background actors report `false`. |
| `is_map_actor()` | `true` when the actor was spawned from level data. |
| `active()` | `true` when the create phase completed. |
| `layer()` / `param0()` / `param1()` / `player_no()` | Raw actor fields. |
| `pos()` / `set_pos(x, y, z)` | Position. You may pass one table `{ x, y, z }`. Requires `is_actor()`. |
| `speed()` / `set_speed(x, y, z)` | Speed. Same actor requirement as `pos`. |
| `scale()` / `set_scale(x, y, z)` | Scale. Same actor requirement as `pos`. |
| `delete()` | Sets the game's delete request flag. The game removes the actor on its next delete pass. Works on every actor handle. |

Do not keep an actor handle across logic steps.
The game reuses actor slots and memory, so a stale handle can point at a different actor.
Re-read `nsmbu.Level.actors()` each step, and call `profile_id()` before you edit or delete a handle.

## Scene flow

Implemented in phase 2 extended with the title-skip groundwork.

The boot flow runs the menus, the save select, and the world map as sead tasks.
`nsmbu.Scene` reads the current phase and requests transitions the same way the
game's own menus do.

| Field / function | Result |
| --- | --- |
| `Factory` | Named class creator ids (`Boot`, `Flow`, `FileSelect`, `SaveSelect`, `Title`, `Menu`, `Map`, `CourseTask`, `CourseSelect`) for comparing with `onSceneChange(factory)` and `phase()`. Values are remapped for the running build; do not hardcode USA v1.3.0 hex in mods. |
| `phase()` | The current boot-flow task class id, or `nil` and a message before the scene exists. Same remapped ids as `Factory`. |
| `title()` | `true` when the title layout phase is current. |
| `file_select()` | `true` when the small file-select task is the current scene. |
| `save_select()` | `true` when the current scene factory is `Factory.SaveSelect` (`ChallengeSelectScene`). |
| `course_task()` | `true` when the `CourseTask` scene is current. |
| `file_index()` | The save file index after the save select has applied a file. `nil` and a message before that. |
| `flow_phase()` | `true` when the application flow phase is current. |
| `map_phase()` | `true` when the world map gamescene phase is current. |
| `course()` | `true` when the game scene task (the animated map behind the menus, the world map, and course scenes) is alive. |
| `course_select()` | `true` when the world map select task is alive; the world map is up and interactive. |
| `open_title()` | Requests a scene change into the title task. The game continues its own flow. Returns `nil` and `"scene busy"` while a scene change is queued or the fader is busy. |
| `apply_save(file?)` | Writes `file` and applies that save the same way `open_first_save` does, including the GamePad / Pro flag. It does not request the world map. `file` is `0`, `1`, or `2`. Returns `nil` and `"scene busy"` or `"file not ready"` when it cannot run. |
| `open_first_save(file?)` | Writes `file` to `SaveData.header.last_file` on `SaveMgr` (`0x101E7DA4`), applies that save file with guest `0x029D7648`, then calls guest `0x029872BC`. `file` is `0`, `1`, or `2`. A missing argument uses `0`. A value outside that range returns `nil` and `"bad file"`. That function requests `CourseSelectTask` (`Factory.CourseSelect`) when the file has a record, and factory `0x0202B570` when the file is empty. After the call, `MainGame.mIsSinglePlayerDRC` (`0x101E7D7C` + `0x14`) is `1` for the GamePad and `0` for the Pro Controller, so the map reads the same device as the host. Returns `nil` and `"scene busy"` or `"file not ready"` when it cannot run. Returns true after the call. |
| `open_file_select()` | Selects save file 0 through `SubjectMgr::selectSaveFile` on an existing menu task, then enters menu state `0x1022C0E4` (`0x0297D268`). It does not create the menu factory and does not enter the challenge-select state (`0x0297D58C`). Returns `nil` and `"scene busy"`, `"menu missing"`, `"state not ready"`, or `"file not ready"` when it cannot run. Returns true when the file select runs. |
| `open_course_select()` | Requests the world map select task. Returns `nil` and `"scene busy"` while a scene change is queued or the fader is busy. |
| `confirm_file()` | Confirms the save select. The routine sets the file and story gates on the flow object and tells the app to confirm, the way the save select does after a file is chosen. Returns `nil` and `"scene busy"` while a scene change is queued or the fader is busy, and does not write the gates in that case. |
| `load_story(world_no, course_no)` | Sets the next story location on `CourseInfo` and requests the game scene task, the way the save select does. `world_no` is 0-indexed (world 1 = 0). `course_no` is 0–255 and is `pack_id - 1` (pack `1-23` is world 0, course 22). Returns `nil` and `"scene busy"` while a scene change is queued or the fader is busy. |
| `load_pack(name)` | `name` is `world-pack` with world 1–9 and pack 1–256 (for example `1-23`). It calls the same `setCourse_Story` path as `load_story`. Returns `nil` and `"bad pack"` on a bad name. Returns `nil` and `"scene busy"` while a scene change is queued or the fader is busy. |

The transitions call `Scene::setNextScene` and `Scene::setCourse_Story` inside the
port; mods never pass guest registers or addresses.
A request only moves the flow it lands on, so call transitions from the matching
phase or after the flow has reached the map.
Call the same transition again on a later logic step when it returns `"scene busy"`.
`Scene::setNextScene` accepts the request after the queued change clears and the fader is idle.

## Function addresses

Proposal (phase 4 with hooks).

```lua
local addr = nsmbu.Fn.dScnPly_Execute
```

`nsmbu.Fn` is a generated table.
Keys are public function names from the USA headers.
Values are integer addresses for the running build.
The European build map translates them at load.
A name that does not exist is `nil`.

Do not write `fn`.
The runtime does not accept a raw USA address on a European build unless that address is in the map.
Prefer `fn`.

There is no guest address for a Lua function.
The game cannot call a Lua function through a function pointer.

## Hooks

Proposal (phase 4).

```lua
local id = nsmbu.Hooks.hook(nsmbu.Fn.dScnPly_Execute, "entry", function(ctx)
  nsmbu.log_hex("r3", ctx.r3)
end)

nsmbu.Hooks.hook(nsmbu.Fn.dScnPly_Execute, "return", function(ctx)
  ctx.r3 = 0
end)

nsmbu.Hooks.unhook(id)
```

| Function | Result |
| --- | --- |
| `hook(target, when, callback)` | An integer id. `when` is `"entry"` or `"return"`. |
| `replace(target, callback)` | An integer id. |
| `unhook(id)` | `true`. Removes a hook or a replacement owned by this package. |

`target` is an address from `nsmbu.Fn` or a mapped integer.
`callback` is `function(ctx)`.
A second package that `replace`s the same function fails at enable time.
Any number of entry and return hooks is allowed.
Entry hooks run in package order, then registration order.
The replacement runs after the entry hooks, or the game body runs when there is no replacement.
Return hooks then run in reverse order.

The port hooks in `hooks*.txt` stay outside this sequence.
Frame interpolation decides whether the function runs at all.

### `ctx`

`ctx` is a table.
The runtime fills it before the callback and copies it back after the callback returns.

| Field | Meaning |
| --- | --- |
| `address` | Guest address of the hooked function. Read-only. The copy-back ignores writes. |
| `r3` .. `r10` | Integer argument registers. |
| `f1` .. `f8` | Float argument registers. |
| `skip` | Entry hooks only. Set `true` to skip the game body, the replacement, and later entry hooks. Return hooks still run. |

A return hook can change `r3`, `r4`, and `f1` to change the value the caller sees.
Other register writes from a return hook are copied back too.
The game ABI uses `r3` and `r4` for a two-register return, and `f1` for a float return.

An entry hook that only reads `ctx` must not set `skip`.

### `original`

```lua
nsmbu.Hooks.replace(nsmbu.Fn.cLib_addCalc2, function(ctx)
  nsmbu.Hooks.original()
  nsmbu.log_int("result", ctx.r3)
end)
```

| Function | Result |
| --- | --- |
| `original()` | Runs the game body once. Returns `true`. After it returns, `ctx` holds the registers the game body left. |

`original` is valid only inside a replacement callback.
A second call in the same invocation returns `nil` and `"original already called"`.
A replacement that never calls `original` skips the game body.
Entry and return hooks cannot call `original`.

## Calls

Proposal (phase 4).

```lua
local ok, err = nsmbu.Hooks.call(nsmbu.Fn.cLib_addCalc2, {
  r3 = object,
  f1 = 1.0,
})
```

| Function | Result |
| --- | --- |
| `call(target, args)` | `true`, or `nil` and a message. `args` is an optional table of `r3` .. `r10` and `f1` .. `f8`. Omitted registers are `0`. |

The return registers are not returned as a second table.
Read them with a return hook, or with `original` inside a replacement.
`call` is for a script that wants to invoke a game function from a logic-step callback or from a hook.

```lua
local result = nsmbu.Hooks.call_result(nsmbu.Fn.cLib_addCalc2, { r3 = object, f1 = 1.0 })
if result then
  nsmbu.log_int("r3", result.r3)
end
```

| Function | Result |
| --- | --- |
| `call_result(target, args)` | A table `{ r3, r4, f1 }`, or `nil` and a message. |

`call_result` is the form to use from a logic-step callback.
Both functions refuse a target that is not a translated game function.
Both refuse a call that is already on the stack for the same package beyond 8 nested `call` / `call_result` frames.

Do not use `call` on a function that runs thousands of times per frame.
Replace that function instead.

## Input

`press` is live. The pad snapshot and button read helpers stay proposal.

```lua
nsmbu.Input.press("A", 3)
nsmbu.Input.press({ "Down", "Down" }, 2)
```

| Function | Result |
| --- | --- |
| `press(buttons, steps)` | Injects those buttons into the pad for the next `steps` logic steps. `buttons` is one name or a mixed list of names (`"A"`, `"Down"`, `"Plus"`, ...). Returns `true`, or `nil` and a message for a bad type. Presses stack with host input; they end on their own. |


```lua
local pad = nsmbu.Input()
if bit.band(pad.buttons, nsmbu.Input.button.A) ~= 0 then
  nsmbu.log("A")
end
```

The `nsmbu.Input` module is callable.
`nsmbu.Input()` returns this table.
It does not change the pad.

| Field | Type | Meaning |
| --- | --- | --- |
| `buttons` | integer | Bit mask. The bits match `runtime/src/input/input.h`. |
| `lx`, `ly` | number | Left stick, about `-1` .. `1`. |
| `rx`, `ry` | number | Right stick. |
| `touch` | boolean | Touch is down. |
| `tx`, `ty` | number | Touch position. |

`nsmbu.Input.button` holds the mask constants.

| Name | Value |
| --- | --- |
| `A` | `0x8000` |
| `B` | `0x4000` |
| `X` | `0x2000` |
| `Y` | `0x1000` |
| `L` | `0x0020` |
| `R` | `0x0010` |
| `ZL` | `0x0080` |
| `ZR` | `0x0040` |
| `Plus` | `0x0008` |
| `Minus` | `0x0004` |
| `Home` | `0x0002` |
| `Up` | `0x0200` |
| `Down` | `0x0100` |
| `Left` | `0x0800` |
| `Right` | `0x0400` |
| `StickL` | `0x00040000` |
| `StickR` | `0x00020000` |

```lua
local down = nsmbu.Input.button.held("A")
```

| Function | Result |
| --- | --- |
| `nsmbu.Input.button.held(name)` | `true` when that button is down. `name` is a key of `nsmbu.Input.button` such as `"A"`. A bad name returns `nil` and a message. |
| `nsmbu.Input.button.pressed(name)` | `true` when the button is down now and was up on the previous `nsmbu.Input()` or `nsmbu.Input.button.pressed` sample in this package. |

This API does not inject buttons.
It does not rumble the pad.

## Data files

Proposal.

Files live in `mod-data/<package id>/` under the host config directory.
The name is one path segment.
`/`, `\`, `..`, and an empty name fail.
The host rejects symlinks and Windows device names.
One call moves at most 1 MiB.

```lua
nsmbu.Data.write("counter.txt", "3")
local text = nsmbu.Data.read("counter.txt")
nsmbu.Data.remove("counter.txt")
```

| Function | Result |
| --- | --- |
| `read(name)` | The file bytes as a string, or `nil` and a message when the file is missing. |
| `write(name, data)` | The number of bytes written. The write replaces the whole file. |
| `remove(name)` | `true`. A missing file is `true`. |

`data` is a Lua string and may contain zero bytes.

## Content files

Live (phase 3).

Scripts do not register file replacements.
Put files under `content_dir` in the manifest.
The map is fixed until the next process start.
A package with `content_dir` needs a restart; a script-only package does not.
See [lua-mods.md](lua-mods.md) and [content.md](content.md).

There is no `nsmbu.content` module in API v1.

## Standard library

The interpreter is LuaJIT 2.1, a git submodule at `vendor/luajit` built static with FFI disabled.
It follows Lua 5.1 semantics.
The standard library is base, `coroutine`, `string`, `table`, `math`, and `bit`.
There is no `utf8` library and no `bit32` library.
Do not use syntax or helpers from other Lua dialects: no type annotations, no `continue`, no `+=`, no generalized iteration (`for k, v in t` is invalid; use `ipairs` / `pairs`), and no `table.create`, `table.clone`, `string.split`, or `math.clamp`.

| Available | Absent |
| --- | --- |
| `string`, `table`, `math`, `coroutine`, `bit` | `utf8`, `bit32`, `io`, `os`, `debug`, `package.loadlib`, FFI |
| `require` of host `nsmbu` / `nsmbu.*` modules | `require` of an unknown `nsmbu.*` name |
| `require` of another `.lua` file in this package | `require` of a path with `..` or an absolute path |
| Host modules as described here | A native library, a PowerPC ELF, a Cemu `rules.txt` |

`coroutine` may yield only inside the callback that resumed it.
A yield across the host boundary returns `nil` and `"yield across host call"` from the host function that notices it.
Prefer no coroutines in hooks.

## Examples

A status line from an option and player coins (phase 2):

```lua
function nsmbu.Hooks.onLogicStep(step)
  if step % nsmbu.Config.number("every", 30) ~= 0 then
    return
  end
  local p = nsmbu.Player.get_player(0)
  if not p or not p:entered() then
    return
  end
  local coins, err = p:coins()
  if not coins then
    nsmbu.log(err)
    return
  end
  nsmbu.status(nsmbu.Config.string("label", "Coins") .. ": " .. coins)
end
```

Count calls to a play-scene function, then leave the game body unchanged (proposal until phase 4):

```lua
local calls = 0

nsmbu.Hooks.hook(nsmbu.Fn.dScnPly_Execute, "entry", function(ctx)
  calls = calls + 1
end)

nsmbu.Hooks.hook(nsmbu.Fn.dScnPly_Execute, "return", function(ctx)
  if calls % 60 == 0 then
    nsmbu.log_int("play scene calls", calls)
  end
end)
```

Save a counter beside the package (proposal until data files land):

```lua
function nsmbu.Hooks.onUnload()
  nsmbu.Data.write("calls.txt", tostring(calls))
end
```

## Not in version 1

- Drawing text or images (the HUD service from SDK v2).
- Button injection, rumble, or touch injection.
- Audio playback.
- Changing the content file map without a restart.
- A guest function pointer that enters Lua.
- Loading a `.so`, `.dll`, or `.dylib`.
- Shader or resolution overrides. Those stay in Cemu packages.
- Named C parameters generated from header signatures. Hooks use registers.
- Filled enemy, boss, blocks, and env helpers (modules exist as empty stubs).

## Errors

| Message | Cause |
| --- | --- |
| `address out of range` | Guest address is outside MEM1, MEM2, and the foreground bucket. |
| `size too large` | Read or write is above 1 MiB, or the heap request is above the package cap. |
| `out of memory` | The package guest heap is full. |
| `bad type` | A host function received the wrong Lua type. |
| `unknown option` | Not used. Config returns `fallback` instead. |
| `unknown function` | `nsmbu.Fn` name or `call` target is not a game function on this build. |
| `already replaced` | This function already has a replacement. Enable fails before the script runs when another package owns it. |
| `original already called` | `original` ran twice in one replacement. |
| `original outside replacement` | `original` ran from an entry hook, a return hook, or a logic-step callback. |
| `bad file name` | Data file name is not one flat segment. |
| `yield across host call` | A coroutine yielded through a host function. |

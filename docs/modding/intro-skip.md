# Native boot skip

This page covers the native boot skip.

Addresses are USA v1.3.0.

## Usage

The runtime has a native boot skip.
`nsmbu run W-C`, `nsmbu launch W-C`, `nsmbu open W-C`, `nsmbu record`, and `nsmbu replay`
pass `--open W-C`.
The native skip uses save file 1.
It presses A on the boot GamePad popup, forces the title if needed, applies the save,
and enters the pack.
The native skip writes `[open]` log lines.

## Flow

The native skip runs on each logic step:

1. Wait for the scene.
2. On the boot GamePad popup (`kCreatorFileSelect`), press A for three steps every 30 steps.
3. Press A up to four times.
4. If the popup remains, force `kCreatorTitle`.
5. Wait for `BootScene` (`kCreatorBoot`) to end.
6. Apply save file 1 with `apply_save_file`.
7. Call `scene_load_story_transition` for the requested pack.
8. Stop when `CourseTask` is current.

Do not call `open_file_select()` for this skip.
Forcing menu factory `0x0202B61C` early shows the time-attack trial layout.

A boot log with this skip shows:

1. `apply_save` after the boot logo
2. `load_pack` for the requested pack
3. `course_task()` or factory `0x0202B4C8`

## Why the menu path failed

`open_file_select()` used to request menu factory `0x0202B61C`.
That layout is the time-attack trial page when created early.
`0x0297D58C` enters the challenge-select state and opens `ChallengeSelectScene` (`0x024C11F4`).

## Boot tasks

| Factory | Header name | Runtime constant | Probe |
| --- | --- | --- | --- |
| `0x0202930C` | `BootScene` | `kCreatorBoot` | (boot) |
| `0x0297B9F8` | none | `kCreatorFlow` | `flow_phase()` |
| `0x0298AD3C` | none | `kCreatorFileSelect` | `file_select()` |
| `0x0202B51C` | `TitleScene` | `kCreatorTitle` | `title()` |
| `0x0202B6C4` | none | post-title select | — |
| `0x020292B8` | none | `kCreatorMap` | `map_phase()` |
| `0x0202B570` | none | empty-file wrapper (creates `MenuScene`) | — |
| `0x0202B61C` | `MenuScene` | `kCreatorMenu` | do not force |
| `0x024C11F4` | `ChallengeSelectScene` | `kCreatorSaveSelect` | `save_select()` |
| `0x0202B4C8` | `CourseTask` | `kCreatorCourseTask` | `course_task()` |
| `0x0202B86C` | `CourseSelectTask` | `kCreatorCourseSelect` | `course_select()` |

Observed boot order from `Scene::setNextScene` logs:

1. `BootScene` `0x0202930C`
2. Flow `0x0297B9F8`
3. File select `0x0298AD3C` (GamePad setup popup)
4. Title `0x0202B51C`
5. After `apply_save`: `CourseSelectTask` `0x0202B86C` (story path may skip straight to `CourseTask`)

## Recommendation

While `file_select()` is true, press A for three steps, then force the title if needed.
Apply save file 1 from the title or map phase after the boot logo.
Then call `scene_load_story_transition`.
Stop on `course_task()`.
Do not force `0x0202B61C`.

---

## GamePad popup

### What the popup is

The boot GamePad / controller setup screen is **not** `MenuScene`.
It is the unnamed file-select task at factory `0x0298AD3C` (`kCreatorFileSelect`).
`scene.file_select()` is true while that task is current.
It sits after the application flow task `0x0297B9F8` (`scene.flow_phase()`) and before `TitleScene`.

`MenuScene::enterFileState` (`0x0297D268`) is the later save-file menu.
Do not use `open_file_select()` or `confirm_file()` for this popup.

`MainGame.mIsSinglePlayerDRC` (`MainGame::sInstance` `0x101E7D7C` + `0x14`) is the GamePad vs Pro flag for single-player input after the map loads.
`apply_save` already writes that field from the host Pro / GamePad setting.
That write does not dismiss the boot popup.

### What the native skip does

The native skip presses A while `file_select()` is true.
It presses A for three steps every 30 logic steps, up to four pulses.
If the popup is still current, it forces the title.

Which button the layout actually consumes is still unknown.
**A** is the first guess; Plus or a layout-specific decide path may be required.
Log lines from real boots should confirm whether **A** alone clears the window or the title transition is doing the work.

Headless or `NSMBU_NO_HOST_INPUT` boots used to stall on `wait boot` at phase `0298AD3C` before this path existed.
Some interactive boots still clear the popup without the native skip in a few logic steps.
That behaviour is not reliable enough for automation on its own.

### UI path (partial RE)

Generated code around the file-select task uses:

| Address | Role |
| --- | --- |
| `0x024BC7D4` | `CommonWindowMgr::processPadTrig_OneButton` |
| `0x025F8788` | `PaCursor::triggerDecide` |
| flow object `+0x12C` | gate byte set to `1` on decide (seen at `0x0298A4C4`) |

The decide chain, decoded:

1. The task state (enter at `0x0298A300` / `0x0298A428`, execute at `0x0298A33C` / `0x0298A464`) resets the
   `CommonWindowMgr` trigger state each enter: trigger index `+0x22B0` = 0, flags `+0x22B4`, `+0x1AB8` = 1,
   pane id `+0x1AA0` = `0x29` (page 1) or `0x2A` (page 2), `+0x1147` = 0, `+0x1AC4` = 0, gate `+0x12C` = 0.
2. While the window is active (`+0x1ABA` != 0), `processPadTrig_OneButton` reads the trigger bitfield for the
   current trigger index (`cwm + 0x22B0` into the array at `cwm + 0x1ACC`, stride `0x194`).
   The bitfield is set by the layout animation, not by pad code directly:
   bit `0x2000` = decide, bit `0x4000` = a second trigger.
   `0x1146`/`0x1147`/`0x114A` are per-window consume flags; when set, the call returns 2 or 3 without a decide.
3. On decide: `PaCursor::triggerDecide` pulses the `PaCursor` singleton (`cwm + 0xEE8`, sets `+0x25F` = 1) and
   the task sets its gate `+0x12C` = `1`.
4. While the gate is set, the state keeps calling `triggerDecide` as long as `this->u32_0x130 & 0x2000` is clear.
5. A wait state (`0x0298A3F8` / `0x0298A404`) calls `SaveMgr::startWriteGame` (`0x029D8304`) and then stays
   current until `SaveMgr.mStatus` (`0x101E7DA4` instance `+0x14`) is no longer `cStatus_Writing`.
   Confirming the popup kicks a full `save://rp_savedata.dat` write before the flow moves on.

The native skip uses `lua_press::press` with the A button mask.
That press reaches the game through the padscore HLE sampling
(`runtime/src/mods/lua_press.cpp`), which drives the layout animation that sets the `0x2000` bit.
The writer of that bit inside the layout stack is not traced yet.

### Boot save-file load

The task state `0x0298A054` runs the boot save chain:

1. `SaveMgr::startReadPersonalSavedata` (`0x029D82D4` kicks state 5) loads `save_act://rp_personal_savedata.dat`
   into the `mpPersonalSavedata` buffer (`SaveMgr + 0x28`).
2. It waits while `SaveMgr.mStatus == cStatus_Reading`, with a duration config (x10) for the spinner, then
   closes the device and kicks the main load: `SaveMgr::startReadGame` (`0x029D8270` kicks state 1).

### Open work

1. Confirm from logs whether A or the title transition clears the popup.
2. If A does not advance the window, try other buttons or a layout-specific decide path.
3. Keep writing `mIsSinglePlayerDRC` through `apply_save_file` so the map matches host GamePad vs Pro.

Do not use `open_file_select()`, `0x0202B61C`, or `0x0297D58C` for this screen.

---

## Save and file loading (RE)

Decompiled sources live in `vendor/decomp/src/system/SaveMgr.cpp` (declarations in `vendor/decomp/include/system/SaveMgr.h`).
Addresses are USA v1.3.0.

### `SaveMgr` (singleton `0x101E7DA4`)

A delegate thread at `+0x10` runs a state machine; the kickers only run when `mStatus` (`+0x14`) is
`cStatus_Ready` (0), then set `mStatus` and signal the thread with the state id.
`mReadError` is `+0x18`, `mWriteError` is `+0x1C`.
`+0x20` holds a work parameter written by the kickers (the challenge replay slot for states 3/4).
`+0x24` is the `rp_savedata` buffer (`SaveData`, 0xB134), `+0x28` the `rp_personal_savedata` buffer (8 bytes).

| State | Kicker | Body | File | Size |
| --- | --- | --- | --- | --- |
| 1 | `0x029D8270` | `0x029D914C` load | `save://rp_savedata.dat` | 0xB134 |
| 2 | `0x029D8304` | `0x029D9560` save | `save://rp_savedata.dat` | 0xB134 |
| 3 | `0x029D82A0` | `0x029D97BC` load | `save://rp_challenge_replay%02d.dat` | ≤ 0xB134, needs ≥ 4 |
| 4 | `0x029D8E88` | `0x029D994C` save | `save://rp_challenge_replay%02d.dat` | `MainGame + 0x84` |
| 5 | `0x029D82D4` | `0x029D9AD8` load | `save_act://rp_personal_savedata.dat` | 8 |
| 6 | `0x029D8394` | `0x029D9CBC` save | `save_act://rp_personal_savedata.dat` | 8 |

`0x029D8304` also updates a byte at `savedata + 0xAEC0` through `0x024E87DC` (a play-log manager at
`0x101D2CCC` copies 0xA-byte entries into `savedata + 0xAEC1`) before kicking the write.

State 2 (`saveSavedata`) chains a challenge replay write when `SaveMgr._30 != -1`:
after the file write it calls `startWriteChallengeReplay(_30)` (`0x029D8E88`, state 4), which consumes the
pending request and resets `_30 = -1`.
That pending-request field is what `isSaving()` checks.

After a successful main load the state normalizes counters
(`0x029D8FDC` header, `0x029D9020` map block at `+0x1840`, `0x029D90C4` replay block at `+0x4E4C`,
`0x029D912C` save counter at `+0xAEC0`), always leaves `mStatus = cStatus_Ready`, and reports through
`mReadError`: `0` ok, `1` file device unavailable, `-1` file unavailable, `-2` file invalid.

### `SaveData` blocks

Every block ends with a CRC32 trailer (poly `0xEDB88320`):
`checkSavedataBlockCrc` (`0x029E2924`) hashes `block[0 .. size-4)` and compares with the trailer after a
runtime-selected transform (the table at `0x101929B8`, indexed by `0x105FAA78`; one choice is a byte swap).
`writeSavedataBlockCrc` (`0x029E2994`) recomputes and stores it.

| Offset | Size | Content |
| --- | --- | --- |
| `0x0000` | 0x10 | header: magic `'RPSD'`, version major/minor/patch at `+4/+5/+6`, `last_file` at `+7` (`rdash_state` at `+8`), CRC at `+0xC` |
| `0x0010` | 6 × 0x204 | per-file records (see below) |
| `0x0C28` | 6 × 0x204 | second record set; `applySaveFile` prefers it when its flag byte is set |
| `0x1840` | 0x360C | map / course progress |
| `0x4E4C` | 0xA8 | replay record: 20 u32 counters (≤ 999999) and 0x20 u8 counters (≤ 2) |
| `0x4EF4` | 0x5F38 | (large block) |
| `0xAE2C` | 0x94 | u32 at `+0` = total play seconds (≤ 999999999) |
| `0xAEC0` | 0x274 | save counter byte (≤ 0x3E) + play log entries (0xA bytes each) |

Load validation: size must be 0xB134, header CRC must pass before the record checks run, version must be
`1.0`, magic must be `'RPSD'`, and any block or record CRC failure makes the whole load invalid.
On any failure the state wipes the savedata to defaults through `SaveMgr::initSavedata` (`0x029D6E8C`)
and still reports the error, so a missing or corrupt file never leaves stale data in memory.

### Per-file record (0x204)

| Offset | Content |
| --- | --- |
| `+0x00` | exists flag (≤ 1) |
| `+0x01` | `FieldGameMode` (≤ 4); `applySaveFile` passes it to the map-entry setup |
| `+0x02` | coin count (≤ 100) → `FieldGameData + 0x134` |
| `+0x0C..0x0F` | per-player lives (≤ 99) → `FieldPlayerData.life_cnt` |
| `+0x14..0x17` | per-player mode bits `0x7F` → `player_mode`, bit `0x80` → entry `+0x1E` |
| `+0x18..0x1B` | per-player extra → entry `+0x3C` |
| `+0x4E` | byte → `FieldGameData + 0x154` |
| `+0x30` | u32 play seconds, capped at 999999999 |
| `+0x49..0x51` | player/Mii setup bytes (clamped) |

`sanitizeRecord_` (`0x029D727C`) clamps these fields; it returns early when the exists flag is 0.

### Apply save file (`0x029D7648`, `kApplySaveFile`)

`applySaveFile(SaveMgr*, u8 file)`:

1. Picks the record: the backup record at `0xC28 + file*0x204` when its flag byte is set, else the main
   record at `0x10 + file*0x204`.
2. Resets `CourseInfo` (`0x024D2904`).
3. Empty record → default `FieldGameData` into `FieldGame + 0x10`, map-entry setup with mode 1
   (`0x024C2230`), and resets `MainGame`'s run state (`0x029CF2D0`, also clears the shadowkun quest flag).
4. Existing record → sanitize, build the `FieldGameData` from the record, map-entry setup with the record's
   `FieldGameMode`, then the player tables: with the apply-gate byte clear (what `apply_save_file`
   does) a single table writes character ids `0..4` (Mario, Luigi, YellowToad, BlueToad, ...) into the five
   `0x48`-stride entries; with the gate set a second table writes default characters and colors `1..4`.
5. Copies the play seconds into `FieldGameData + 0x138` and pushes the player/Mii setup into `MainGame`
   (`0x029CF590`).

The apply-gate byte (`0x101E7D9C` instance `+0x11`) only selects between those two player tables.
The native callers pair `SaveMgr::setLastFile` (`0x029D714C`, writes `header.last_file`) with this apply,
then call `0x029D7CC8`, which writes the default game data back into the records (the fresh-file write-back
the native skip path skips).

### Save continue (`0x029872BC`, `kOpenFirstSave`)

Called with the src task; the SDK's `open_first_save` runs `apply_save` first, then this:

1. Reads `header.last_file` and that file's main record exists flag (`savedata + file*0x204 + 0x10`).
2. Record exists → `setNextScene(src, kCourseSelectTaskFactory)` and `mIsNewGame = 0`.
   Empty → `setNextScene(src, 0x0202B570)` (a wrapper task that creates the `MenuScene` save-slot task)
   and `mIsNewGame = 1`.
   This corrects the boot table row for `0x0202B570`: it is the MenuScene wrapper for an empty file, not
   "pre-course".
3. For each of the four players: an entered player with `cPlayerCharacter_Mario` gets
   `cPlayerMode_Normal`.
4. Writes `0x00020002` to `Scene + 0x6C`, clears `FieldGameData + 0x14C`, copies a byte from the play-log
   device at `*(0x101D4D70) + 0xA1772` into `MainGame.mIsSinglePlayerDRC`, and refreshes the story play
   text (`0x029D9E80`).

### What this explains in the intro skip

- `apply_save` "file not ready": `SaveMgr.mpSavedata` (`+0x24`) is null until the boot load state ran, and
  the apply needs `FieldGame::sInstance` too.
- The popup stays up through a full `rp_savedata` write; `Hooks.onSceneChange` sees the file-select factory
  the whole time, which is why the native skip pulses `A` on a step timer instead of once.
- `apply_save` does not load resources or scenes; entering a course still goes through
  `setCourse_Story` → `CourseTask` (see [course-open.md](../developer/nsmbu/course-open.md)).

---

## Course load (`load_pack`)

Goal: enter the stage that loads `game/content/Common/course_res_pack/1-23.szs` (pack name `1-23`) without stopping on the world map.

### Pack name mapping (proven)

`Scene::setCourse_Story` (`0x029941D0`) writes `CourseInfo::setNextWorldNo` (`+0x1C`) and `setNextCourseNo` (`+0x1D`), then requests `CourseTask` `0x0202B4C8`.

Course setup `f_024BD86C` calls `f_024D29A4`, which copies those next fields to `+0x10` and `+0x11` on `CourseInfo` (`sInstance` `0x101D18EC`).

Later in that function, `f_0269A2B4` sprintfs format `0x100E6040` (`"%d-%d"`) as `(byte+0x10)+1` and `(byte+0x11)+1`.
That string is the `%s` in `course_res_pack/%s.szs` (`f_029D3150`).
Pack `1-23` is `load_story(0, 22)` or `load_pack("1-23")`.
`course_no` is `pack_id - 1`, not a separate map slot index.

Guest `0x024D3AD4` is a save-nibble helper.
It is not the filename builder.
Older notes that pointed at ROM tables near `0x100C97xx` for the pack filename were wrong.

The Lua API exposes:

```lua
scene.load_story(world_no, course_no)
scene.load_pack("1-23")
```

Both call `setCourse_Story` when the fader is idle.
The second argument is the live flow task.
A null source crashes inside `Scene::setNextScene` (`ld32` at `0x029947D0`).
They return `"scene busy"` while a scene change is queued or the fader is busy.
The native skip requests the pack once, then waits for `course_task()`.

`load_pack` remains available to mods.
The native skip uses the same transition with the `--open` pack.

### Course enter banner digits

The loading card for `nsmbu open` is documented in [course-open.md](../developer/nsmbu/course-open.md).
The green world–course number on the pre-level banner is not MSBT text.
Course setup (`f_024BD86C`) copies `FieldGameData` and writes picture-pane glyph indices from bytes at the course-task entry `+0x10` (three slots; `0xFF` is blank).
Entering from the world map fills those bytes via the save/map path (`f_024BADF4`).
A direct `load_pack` / `load_story` skips that fill, so the glyph bytes stay `0xFF` and the number font shows garbage (often a comma).
When the copied `FieldGameData` already matches the current world/course, setup skips `f_024BAD4C` entirely (`f_024BADCC` hit).
The port does not hook that path.
A direct open can show a blank green number and the save's world name.

### Map return after directed open

The same skip means `FieldGameData::setCheckPoint` never runs before the course.
Quitting back to `CourseSelectTask` then has `mCheckPoint.index == -1` and `MainGame` map pose `+0x19C/+0x1A0/+0x1A4` cleared, so `CourseSelectPlayer` fails to spawn on the opened node.
The runtime seeds that checkpoint and pose from `CourseInfo` world/course when the checkpoint is still missing (course-out site `0x024B8324`, CourseSelectPlayer setup `0x0226FBC4` / `0x0226FD14`).
Node lookup uses `CourseSelectMgr` path entries by name (`W101` for pack `1-1`).
Path node `+0x50/+0x54` are not a reliable world/course pair (many nodes share `0,0`), so name match is required.
Live `CourseSelectPlayer+0x110` is a pointer after spawn.
Writing node floats there crashes in the map sound path (`ld` at `+0xC` off a null vtable).
Do not use the save `worldMapLocation` alone.
The map camera copies its focus node at `0x0227CCF4` and `0x0227E2A8` from the path search, which still returns the world start after a directed open.
Those sites replace that node with the opened course before the pose and position stores.
They stay out of the opening cinematic and out of loading a save from the file select.
Both of those set a course-exit type without going through course-out, and the map keeps its own camera until it gives you control.
See [course-open.md](../developer/nsmbu/course-open.md).

### Normal enter path

1. Intro skip → `apply_save` → optional `load_pack` → `CourseTask`.
2. Player picks a node on the map (vanilla path).
3. `setCourse_Story` → `CourseTask` `0x0202B4C8`.
4. Course setup calls `loadCourseResPack` with the resolved name (e.g. `1-23`).

Save-select story confirm (`MenuScene::confirmFile` with story gate) also ends on `CourseTask` `0x0202B4C8` after copying save into `CourseInfo`.
Challenge / time-attack uses other course indices and is the wrong path for story packs like `1-23`.

Do not call `ResMgr::loadCourseResPack` from a mod without the `setCourse_Story` → `CourseTask` path.
Do not force `ChallengeSelectScene` for story packs.

---

## Address quick reference

| Item | Address |
| --- | --- |
| `Scene::setNextScene` | `0x02994164` |
| `Scene::setCourse_Story` | `0x029941D0` |
| Save continue → map | `0x029872BC` |
| Apply save file | `0x029D7648` |
| `SaveMgr::setLastFile` | `0x029D714C` |
| Save data block CRC verify / store | `0x029E2924` / `0x029E2994` |
| `SaveMgr` load / save `rp_savedata.dat` | `0x029D914C` / `0x029D9560` |
| `SaveMgr` load / save `rp_personal_savedata.dat` | `0x029D9AD8` / `0x029D9CBC` |
| `SaveMgr` load / save challenge replay | `0x029D97BC` / `0x029D994C` |
| Boot file-select factory | `0x0298AD3C` |
| Flow factory | `0x0297B9F8` |
| `CourseSelectTask` factory | `0x0202B86C` |
| `CourseTask` factory | `0x0202B4C8` |
| `CourseInfo::sInstance` | `0x101D18EC` |
| `MainGame::sInstance` | `0x101E7D7C` |
| `mIsSinglePlayerDRC` | instance + `0x14` |
| CourseInfo copy for pack name | `0x024D29A4` |
| Pack name sprintf | `0x0269A2B4` |
| `ResMgr::loadCourseResPack` (guest) | `0x029D30EC` |

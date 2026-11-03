# Portable save states

Save states come in two kinds.
The two kinds share the five slots.

| | Portable (default) | Full (debugging) |
|---|---|---|
| File | `slotN.nsstate` | `slotN.bin` |
| Size | about 90 KB (refused above 128 KB) | about 270–300 MB |
| Holds | the live save image and the course or map place | all guest memory (game code, decompressed assets), threads, HLE state |
| Shareable | yes, attach it to bug reports | **never**: it contains game code and data |
| Loads into | any session of the same game, any build | only the same build, exactly where it was saved |
| Exact | no (see below) | yes |

Both files live in the states folder.
The Saves tab shows that path.
A release keeps the folder in `data/nsmbu`.
A build from source uses `nsmbu/states/` with `nsmbu launch` or `nsmbu run`.
macOS uses `~/Library/Application Support/nsmbu/states/`.
Other systems use the configuration folder.
`NSMBU_STATE_DIR` overrides the folder.

A slot load uses the file that the slot holds.
If the slot holds both kinds, the load uses the newer file.
Crash Recovery always writes a full state.

## Choosing the kind

- **Save** (the Saves tab, Shift+F1–F5, or the macOS Save States menu) writes a portable state.
- That save works in a course and on the Story Mode world map.
- The setting *Full save states (large, contain game data, don't share) – for debugging* switches Save to a full state.
- The choice stays in `states/full_save_states.cfg`.
- `NSMBU_FULL_SAVE_STATES=1` or `=0` decides for one start.
- `NSMBU_STATE_SAVE_AT`, `NSMBU_STATE_LOAD_AT`, `NSMBU_TEST_SAVE`, and `NSMBU_TEST_LOAD` keep full states.
- Those variables use a portable state only when `NSMBU_FULL_SAVE_STATES=0`.

A slot can hold both kinds.
A save of one kind does not delete the other kind.
The slot loads the newer file, and the Saves tab shows that file.
If the newer file is portable and an older `slotN.bin` remains, the Saves tab says so under the slot.
The log also notes that older file when you save.
Delete the large file in the states folder when you no longer need it.

## When a portable state can be saved

A save works while you stand in a course.
The course task is active, and the player actor is in memory.
The state keeps the course id and the player position.

A save also works on the Story Mode world map.
The map is open, and a course is not active.
The state keeps the map world and the path node.
The old message "no game in progress" does not apply on that map.

The title screen waits.
The message names the title screen.

File select waits.
The message names file select.

A scene change waits while the fade is busy.
The message says that a scene transition is in progress.
An open game menu also waits.

If none of these places is active, the message says that no game is in progress.
The save writes nothing in that case.
The same rules apply to the Save button and to Shift+F1–F5.
A full state and a Crash Recovery state can be made at any time.

## Bug reports

**Copy save for bug report** copies the path of the newest portable state.
It also copies the paths of `rp_personal_savedata.dat` and `rp_savedata.dat`.
The issue template asks for those files.

Copy a received state into the states folder as `slotN.nsstate`, then load slot N.
`NSMBU_PORTABLE_LOAD=<file>` loads that file when a course or the world map is ready.

## What a portable state holds

The file is UTF-8 text.
Each line has the form `key = value`.
The field list is in `runtime/src/save/portable_state.h`.

The header has these fields:

- `format` (1)
- `title_id` and `title_version` from `meta/meta.xml`
- `game_hash` (a hash of `red-pro2.rpx`, not the file contents)
- `runtime` (the version and the commit)
- `created`
- `file_slot` (save file 0–2)
- `player_name`
- `controller`, when the host input mode was recorded

The place has these fields:

- `place` is `course` or `map`
- `stage` is a label such as `1-2` or `W1 n1`
- `world` and `course`
- `player_pos` for a course (three coordinates)
- `section` and `node` for a map

`savedata` is the live save image.
That image is 0xB134 bytes from SaveMgr.
It holds the story save files.
The reader checks that byte 7 of the image is `file_slot`.
It also checks the `RPSD` magic, version `1.0`, and the same block CRC-32 trailers the game uses on `rp_savedata.dat`.

`checksum` is the CRC-32 of every line before it.

The writer and the reader accept only known fields.
A state from this game records a course or a map.
The reader refuses an unknown field.
A text value can have up to 128 characters.
The file can have up to 128 KB.
A file that breaks a rule is not written and is not read.
A state cannot carry a memory dump.
`runtime/test/portable_state_test.cpp` checks the format.

## Loading

The load runs on the game thread at a frame boundary.

A course load starts the story course change.
If the game is leaving a course, or the scene is busy, the load waits and tries again.
When the new player actor exists, the load writes the saved position.
The load does not write that position in the frame that leaves the old course.

A map load opens Course Select.
The load then writes the saved world, section, path node, and course tag.
The checkpoint index is that path node.
A course slot in that index moves the camera to the wrong node.

The load writes the save image back into the same SaveMgr buffer.
The file slot in the image must match the state.
A different slot does not receive this file's progress.

The title screen and file select wait.
The message names that screen.
The load enters a course from the map only when the fade is idle.

These parts do not come back:

- enemies, loose items, and other actors start as the course starts them
- a game menu or a scene change is not resumed
- in a course, the camera starts behind the player
- on the map, the camera uses the saved path node

Save-state notices show at the bottom of the picture for a few seconds.
On macOS they also show in the window title.

`controller = 1` means the GamePad.
`controller = 2` means the Pro Controller.
A load restores that host input mode.
A missing value or `0` leaves the current mode.
The reader refuses an unknown value.

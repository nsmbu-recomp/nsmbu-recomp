# Names for game functions

Every address in this repository is an address in USA v1.3.0 `red-pro2.rpx`.
An address is the canonical id of a function, a global, or an instruction site ([builds.md](builds.md)).
A name is easier to read and easier to talk about.
Add the name next to the address.
Do not replace the address with the name: the address stays the key in the hooks files and in the runtime.

## Where the names come from

| Tree | What it gives | Has addresses? |
| --- | --- | --- |
| `references/headers` | Class and function names for USA v1.3.0 ([nsmbu/headers](https://github.com/nsmbu/headers)) | Yes, an `Address:` value on each symbol |
| `references/decomp` | C++ source for part of the game ([REDPRO2](https://github.com/aboood40091/REDPRO2)) | No |
| `references/Cemu` | Emulator source for Cafe OS and GX2 | No |

Use `references/headers` first.
It is the only tree that ties a name to an address.
Use `references/decomp` for the body of a function, the member order, and the layout, when the header has only a declaration.
`references/decomp` uses the same class names as `references/headers`.
Use `references/Cemu` for HLE behaviour, not for names.

`vendor/luajit` is a submodule; run `git submodule update --init vendor/luajit` before a build.
These checkouts are gitignored.
They are not name sources.
Open one when the task is in that library.

| Tree | What it gives |
| --- | --- |
| `references/sead` | The sead engine and the related EAD libraries in NSMBU |
| `references/Eft` | NintendoWare Eft (particles) as in NSMBU |
| `references/G3d` | NintendoWare G3d (3D) as in NSMBU |
| `references/FFL` | The Wii U Face Library (Mii) as used by NSMBU |
| `references/Lyt` | Layout library |
| `references/Snd` | Sound library |
| `references/Sys` | System library |
| `references/Nintendo-File-Formats` | Nintendo file format notes |

The repository does not carry the headers.
`tools/recomp/names.py` extracts the addresses and names from a local `references/headers` checkout into `tools/recomp/headers_names.txt`, and that file is in git.
`recomp.py` and `stubgen.py` read the file, so the gen needs no checkout of the headers.
Credit the names to [nsmbu/headers](https://github.com/nsmbu/headers).
Refresh the file after a headers update with `python3 tools/recomp/names.py references/headers`.

## Read a name out of a header

A header gives the class in the file name or in the enclosing `class`, and the name from the declaration.
The address is in a comment next to the symbol.
The comment has three shapes.

| Shape | Where it appears |
| --- | --- |
| `Address: 0xADDR` on its own line, before the declaration | members and non-virtual methods |
| the symbol and `Address: 0xADDR` on one line | statics, and the singleton disposer block |
| `@par Address: 0xADDR` in a doxygen block | virtuals |

```cpp
// Address: 0x024BBF18
void incCoin(s32 num, s32 player_no);
```

So `0x024BBF18` is `CoinBattleScoreMgr::incCoin`.
The header also writes `Address: Deleted` for a symbol the linker removed.
That value is not an address.

The file gives the qualified name: `references/headers/scene/Scene.h` is the class `Scene`.
A nested `class` in the file adds to the scope: in `references/headers/scene/Scene.h`, the address `0x02994164` on the line before `bool setNextScene(...)` is `Scene::setNextScene`.

## The shapes we use

| Site | Shape | Example | Source of the shape |
| --- | --- | --- | --- |
| Generated C | `f_XXXXXXXX`, 8 uppercase hex digits | `f_02994164` | `tools/recomp/recomp.py` names every function by its canonical address |
| Generated C comments | the header name in a `/* ... */` comment | `/* Scene::setNextScene */` | `tools/recomp/names.py` reads `references/headers` and `recomp.py` writes the name above the definition, next to the prototype in `funcs.h`, and at every call site of a named function |
| `tools/recomp/hooks.txt` | `XXXXXXXX` for a function entry, `@XXXXXXXX` for an instruction site | `@022A79C4` | the recompiler reads this file |
| A hooked function | `hook_XXXXXXXX` in the runtime, `f_XXXXXXXX_orig` for the game code | `hook_02994164` | `f_` plus the same digits |
| Runtime code and data | `GC(0xXXXXXXXX)` for code, `GD(0xXXXXXXXX)` for data | `GC(0x02994164)` | [runtime/include/guest_addr.h](../runtime/include/guest_addr.h) |
| A runtime constant | a `kName` constant that holds the address | `kSetNextScene = GC(0x02994164)` | [runtime/src/mods/sdk/nsmbu.cpp](../runtime/src/mods/sdk/nsmbu.cpp) |
| Mod API (proposal) | `nsmbu.Fn.<name>`, generated from the headers | `nsmbu.Fn.dScnPly_Execute` | [modding/api.md](modding/api.md) |

`GC` and `GD` are the identity for the USA build.
They stay in the code so a later regional build can shift the address through that build's map.
Keep the canonical USA address inside `GC` and `GD`, and give the constant a name.

## Name an address

1. Take the digits of `f_XXXXXXXX`, `hook_XXXXXXXX`, or `XXXXXXXX`, and search the headers:

   ```sh
   grep -rn "0x02994164" references/headers
   ```

   Without a checkout, search the extracted file:

   ```sh
   grep "02994164" tools/recomp/headers_names.txt
   ```

2. Read the class and the symbol from the hit.
   `0x02994164` is `Scene::setNextScene`.
3. Write the name next to the address:
   - in a hooks file, after `#`:

     ```
     02994164  # Scene::setNextScene: log every scene transition with args
     ```

   - in the runtime, as the name of the `GC`/`GD` constant, or as a comment on the hook.
4. Search `references/decomp` for the same class when the header has a declaration only:

   ```sh
   grep -rn "Scene::setNextScene" references/decomp/src
   ```

5. When no tree has the symbol, keep the address and write what the site does.
   A guessed name is worse than an address.

## Name to address

`references/headers` also works in the other direction.
Search the name and read the `Address:` value:

```sh
grep -rn "setNextScene" references/headers
```

## Addresses used in this repository that have a header name

| Address | Name | Used as |
| --- | --- | --- |
| `0x02009954` | `ActorMgr::createLater` | `kActorMgrCreateLater`, mod API |
| `0x0202930C` | `sead::TTaskFactory<BootScene>` | `kCreatorBoot`, boot scene factory |
| `0x0202B51C` | `sead::TTaskFactory<TitleScene>` | `kCreatorTitle`, scene phase probe |
| `0x0202B61C` | `sead::TTaskFactory<MenuScene>` | `kCreatorMenu`, save-slot menu task |
| `0x0202B86C` | `sead::TTaskFactory<CourseSelectTask>` | `kCreatorCourseSelect`, scene phase probe |
| `0x024C11F4` | `sead::TTaskFactory<ChallengeSelectScene>` | `kCreatorSaveSelect`, time-attack scene |
| `0x0297AC48` | `BootScene::BootScene` (ctor) | boot task body |
| `0x0297C5D8` | `MenuScene::MenuScene` (ctor) | menu task body |
| `0x0297D268` | `MenuScene::enterFileState` | `kAppEnterFileState`, intro skip |
| `0x0297D58C` | `MenuScene::enterChallengeSelectState` | time-attack state enter (not intro skip) |
| `0x0297D5A8` | `MenuScene::openChallengeSelect` | state `0x1022C184` initialize; requests `ChallengeSelectScene` |
| `0x0297EC3C` | `MenuScene::confirmFile` | `kAppConfirmFile`, story vs challenge |
| `0x029872BC` | save continue into `CourseSelectTask` | `kOpenFirstSave`, intro skip |
| `0x029D7648` | apply a save file onto `MainGame` | `kApplySaveFile`, intro skip |
| `0x029D714C` | `SaveMgr` write `header.last_file` | pairs with the apply in native callers |
| `0x029D727C` | sanitize a save record (clamp fields) | called by the apply |
| `0x029D7200` | write default player character/color tables | called by the apply |
| `0x024C2230` | map-entry setup from `FieldGameData` | called by the apply |
| `0x024C203C` | `FieldGameData` write coin count (`+0x134`) | called by the apply |
| `0x024C1FC4` | apply record `+0x10..0x13` flags to player entry 0 | called by the apply |
| `0x029CF2D0` | reset `MainGame` run state (new game) | called by the apply |
| `0x029CF590` | copy record player/Mii setup into `MainGame` | called by the apply |
| `0x029D6E8C` | `SaveMgr` init savedata to defaults | load error epilogue |
| `0x029D914C` | `SaveMgr` load `save://rp_savedata.dat` | state 1 |
| `0x029D94C4` | `SaveMgr` add elapsed play time | after load |
| `0x029D9560` | `SaveMgr` save `save://rp_savedata.dat` | state 2 |
| `0x029D97BC` | `SaveMgr` load `save://rp_challenge_replay%02d.dat` | state 3 |
| `0x029D994C` | `SaveMgr` save `save://rp_challenge_replay%02d.dat` | state 4 |
| `0x029D9AD8` | `SaveMgr` load `save_act://rp_personal_savedata.dat` | state 5 |
| `0x029D9CBC` | `SaveMgr` save `save_act://rp_personal_savedata.dat` | state 6 |
| `0x029D9E30` | `SaveMgr` state dispatch | states 1..6 |
| `0x029E2924` | verify a `SaveData` block CRC trailer | all loads |
| `0x029E2994` | recompute and store a `SaveData` block CRC trailer | all saves |
| `0x029F19C4` | CRC32 over a block (poly `0xEDB88320`) | CRC helpers |
| `0x0298A300` | file-select task state enter, page 1 (pane `0x29`) | boot popup |
| `0x0298A33C` | file-select task state execute, page 1 | boot popup decide |
| `0x0298A3F8` | file-select task state: kick `SaveMgr` write | boot popup |
| `0x0298A404` | file-select task state: wait `SaveMgr` write done | boot popup |
| `0x0298A428` | file-select task state enter, page 2 (pane `0x2A`) | boot popup |
| `0x0298A464` | file-select task state execute, page 2 | boot popup decide |
| `0x0298A524` | task state: request the file-select task factory | flow side |
| `0x0297F3CC` | `ChallengeSelectScene::ChallengeSelectScene` (ctor) | time-attack scene body |
| `0x024CD904` | `SubjectMgr::clearSubject` | clears `mMode` |
| `0x024CE594` | `SubjectMgr::selectSaveFile` | sets `mSelectedFile` and `mMode` |
| `0x022A7760` | `DistantViewMgr::calcView_` | the site `@022A79C4` sits inside it |
| `0x024BBE40` | `CoinBattleScoreMgr::createInstance` | not used here; shape example |
| `0x02994164` | `Scene::setNextScene` | `hook_02994164`, `kSetNextScene`, mod API |
| `0x029941D0` | `Scene::setCourse_Story` | `kSetCourseStory`, mod API |
| `0x101C30C8` | `ActorMgr::sInstance` | `kActorMgrSingleton`, mod API |
| `0x101CC96C` | `CourseSelectTask::sInstance` | `kCourseSelectInstance`, mod API |
| `0x101D15F0` | `CourseTask::sInstance` | `kCourseTaskInstance`, mod API |
| `0x101D15F4` | `CourseTimer::sInstance` | `kCourseTimerSingleton`, mod API |
| `0x101D1604` | `FieldGame::sInstance` | `kFieldGameSingleton`, mod API |
| `0x101D1874` | `SubjectMgr::sInstance` | `kSubjectMgr`, mod API |
| `0x101E6994` | `PlayerMgr::sInstance` | `kPlayerMgrSingleton`, mod API |
| `0x101E7B28` | `Scene::sInstance` | `kSceneSingleton`, mod API |
| `0x101E7DA4` | `SaveMgr::sInstance` | `kSaveMgr`, intro skip |
| `0x101E9CC0` | `Actor::getRuntimeTypeInfoStatic()::typeInfo` | `kTypeInfoActor`, mod API |
| `0x101ED8DC` | `Profile::sProfileList` | `kProfileList`, mod API |

The headers do not name every address this repository uses.
`0x029872BC` has no header name.
The table records that address as the save continue handler.
The remaining hooks are `02ACD218`, `024D752C`, and `@022A79C4`.
The remaining runtime addresses are `kCreatorFlow`, `kCreatorFileSelect`, `kCreatorMap`, `kCreatorCourseTask`, `kFaderBusy`, the save state functions, and the interpolation probes.
Give the rest a name when a tree gains the symbol.

## Offsets

An offset constant in the runtime comes from a class layout, not from an address.
Take the member order from `references/headers` or `references/decomp` and add the base class size.
`CourseTimer` has a `SEAD_SINGLETON_DISPOSER` base of 0x10 bytes, then these members:

| Offset | Member |
| --- | --- |
| `0x10` | `mFreezeTime` |
| `0x14` | `mTime` |
| `0x18` | `mBeginTime` |
| `0x1C` | `mIsHurryUp` |
| `0x1D` | `mStopTimerInfo` |
| `0x1E` | `mBonusTime` |

So `kTimerTime = 0x14` is `CourseTimer::mTime`.
A `static_assert(sizeof(X) == 0xNN)` in the header confirms the layout.

`SubjectMgr` ends at `0x1E84`.
`kSubjectModeOff = 0x1E7F` is `SubjectMgr::mMode`.
`kSubjectFileOff = 0x1E7E` is `SubjectMgr::mSelectedFile`.
`kSubjectModeOff = 0x1E7F` is `SubjectMgr::mMode`.
`0x024CE594` stores the chosen save file in `mSelectedFile` when it sets `mMode` to active.

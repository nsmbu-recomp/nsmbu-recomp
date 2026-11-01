# Level runtime and course hot reload

[course-open.md](course-open.md) covers pack names and `CourseInfo`.
[course-files.md](course-files.md) covers the course data blocks.
This page covers the runtime side: how a course pack becomes a live level,
what stays cached, and how the debug hot reload rebuilds it from disk.

All addresses are USA v1.3.0 `red-pro2.rpx`.

## Task and scene flow

The normal story transition is:

1. `Scene::setCourse_Story` (`0x029941D0`) requests the `CourseTask` factory
   `0x0202B4C8`.
2. `Scene::setNextScene` (`0x02994164`) hands the factory to the task manager.
3. `CourseTask` is created and course setup `f_024BD86C` runs.
4. Course setup calls `ResMgr::loadCourseResPack` at `0x024BDF3C`.
5. Course setup copies the next world and course bytes into the now bytes
   through `f_024D29A4`.
6. `AreaTask` (`sInstance` `0x101D1598`) owns the active area and actors.
7. Leaving the course destroys the course task and its heap.

`CourseTask::sInstance` is `0x101D15F0`.
`CourseInfo::sInstance` is `0x101D18EC`.
`ResMgr::sInstance` is `0x101E7D94`.

The course task and area task classes are not matching decompilation yet.
Their headers and the generated C are enough to follow the load and teardown
boundaries.

## Pack load path

`ResMgr::loadCourseResPack` is `0x029D3150`.
Do not use `0x029D30EC` for this function: that address is
`CourseArchiveResHolder::CourseArchiveResHolder`.

The load path is:

1. Build `course_res_pack/<world>-<course>.szs`.
   `RDashMgr::isNSLU` can prepend `rdash://`.
2. `ResMgr::loadCourseResPackImpl_` (`0x029D2A20`) opens the file through the
   sead archive file device.
3. The runtime file HLE opens the host file.
   `runtime/src/hle/fs.cpp` does not cache file contents.
4. The sead SZS decompressor expands the file.
5. The outer file is a SARC. Its entries are the inner `<world>-<course>` SARC,
   actor archives, and tileset archives.
6. `loadCourseResPackImpl_` loads the inner entries into
   `ResMgr::mResHolderTreeMap` by name.
7. `ResMgr::mpCourseArchiveRes` points at the archive used for course files.
8. `new (heap) CourseArchiveResHolder(...)` clears `mpCourseArchiveRes` when
   the course heap is destroyed.

Course setup passes `heap = 0` to `loadCourseResPack`.
That is the current sead heap, normally the course heap.
The outer archive and the inner archive holders are therefore course-scoped.

## Example: `1-21.szs`

`game/content/Common/course_res_pack/1-21.szs` is Yaz0 compressed.
It expands to 30,859,264 bytes.
The outer SARC has 20 entries, including:

- the inner `1-21` SARC,
- actor archives such as `obj_gear`, `obj_dokan`, `kameck`, and `bunbun`,
- tileset archives such as `Pa0_jyotyu`, `Pa1_1-21_1`, and `Pa2_1-21_1`.

The inner `1-21` SARC holds the `course` folder with
`course1.bin` and the `course1_bgdatL*.bin` layer files.

## Cache and lifetime

| State | Owner | Lifetime | What it means for an edit |
| --- | --- | --- | --- |
| Host file contents | host file system | reopened on every guest open | a fresh open sees the new bytes |
| Content replacement map | `mods::content` | startup to exit | paths are fixed; mapped file bytes are not |
| `mpCourseArchiveRes` | `ResMgr` | cleared by `CourseArchiveResHolder` | points at the current course archive |
| `mResHolderTreeMap` | `ResMgr` | holder objects live on their heap | entries already present are skipped |
| `ResourceMgr` resources | current heap | usually the course heap | resources die with the course heap |
| `ChallengeResCacheMgr` | challenge tasks | cache heap | challenge mode can keep a course archive loaded |

The important case is a cached inner archive.
`loadCourseResPackImpl_` skips an entry when
`isArchiveResLoaded(entry.name)` is true.
A normal course exit destroys the course heap, which destroys the holders and
removes those entries.
If a holder survives on a persistent heap, an edited inner archive or actor
archive can stay stale.

`Profile::loadResource` also loads `actor/<name>.szs`.
An actor archive already loaded from the course pack wins because the key is
already in `mResHolderTreeMap`.
That is why actor edits inside the pack must refresh the course-scoped entry,
not just the `actor/` file.

## Hot reload

The debug hot reload watches the active story pack and rebuilds it with the
existing safe transition:

1. Read the active world and course from `CourseInfo`.
2. Build `course_res_pack/<world>-<course>.szs`.
3. Resolve the host path through the content replacement map.
4. Poll file size and modification time.
5. After the file is stable for two polls, read it, hash it, and validate the
   Yaz0 stream.
6. If the content hash changed, call the same story transition that
   `Scene.load_pack` uses, with the same world and course.
7. `Scene.load_pack` leaves the live course to the title screen and re-enters
   the course.

While that leave is pending, course setup `f_024BD86C` is skipped.
TitleScene bring-up then does not run setup against a torn-down `InputMgr`.
The course HUD (`f_0252F900`, `f_0252D9C0` on a base disc, `f_025CF020`) returns when `AreaTask` is null.

The reload does not patch live guest state.
The course restarts at its entrance.
The watcher runs on game logic steps, so it does not poll while the game is
paused.
A partial, truncated, or invalid SZS is rejected and the current course keeps
running.

The watcher is off by default.
Use one of these:

- `NSMBU_HOT_RELOAD_COURSES=1` for the session.
- The settings debug tab: **Course hot reload**.
- **Reload now** in the same tab for a one-off rebuild.

`NSMBU_HOT_RELOAD_POLL_MS` changes the poll interval.
The default is 250 ms; the accepted range is 50 to 5000 ms.

Logs use the `[course-reload]` prefix.
The debug tab shows the active pack, resolved file, and current status.

## What this does not cover

- Challenge mode, where `ChallengeResCacheMgr` can keep the archive on a cache
  heap.
- Resources on persistent heaps outside the course heap.
- Edits to files that were not opened again by the course transition.
- In-place patching of the running course.
  Actors, tiles, collision, and models are rebuilt only by the course transition.

## Verification

1. Start the game with `NSMBU_HOT_RELOAD_COURSES=1`.
2. Enter `1-21`.
3. Replace `game/content/Common/course_res_pack/1-21.szs` with a valid edited
   file.
4. Watch for this sequence in `nsmbu/logs/nsmbu.log`:

```text
[course-reload] watching .../1-21.szs
[course-reload] 1-21 changed, reloading
[course-reload] 1-21 reloaded
```

5. Confirm the game returns to the course and keeps running.
6. Truncate the file and confirm the log reports a rejected file and the
   course is not torn down.

## Decompilation gaps

The useful next targets are:

- `CourseTask` constructor, destructor, and `f_024BD86C` in full.
- `AreaTask` constructor, destructor, and area reset paths.
- Course heap ownership for the archive holders.
- The challenge cache path through `ChallengeResCacheMgr`.
- Whether any persistent heap keeps a `ResHolder` for an actor or tileset.

The runtime watcher can be tested without those answers.
A matching decompilation only becomes necessary if stale archive entries are
observed across a course transition.

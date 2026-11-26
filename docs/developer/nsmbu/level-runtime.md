# Level runtime

[course-open.md](course-open.md) covers pack names and `CourseInfo`.
[course-files.md](course-files.md) covers the course data blocks.
This page covers the runtime side: how a course pack becomes a live level,
and what stays cached.

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

`Scene.load_pack` leaves the live course and re-enters it.
While that leave is pending, course setup `f_024BD86C` is skipped.
The task manager still runs the next course-enter steps, `f_024BE4FC` and `f_024BFA50`.
Those are skipped too.
`f_024BE4FC` calls `InputMgr::setupCourseControllers_`.
`f_024BFA50` reads `InputMgr` at offset `0x574`.
TitleScene bring-up then does not run those steps against a torn-down `InputMgr`.
The course HUD (`f_0252F900`, `f_0252D9C0` on a base disc, `f_025CF020`) returns when `AreaTask` is null.

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

## Decompilation gaps

The useful next targets are:

- `CourseTask` constructor, destructor, and `f_024BD86C` in full.
- `AreaTask` constructor, destructor, and area reset paths.
- Course heap ownership for the archive holders.
- The challenge cache path through `ChallengeResCacheMgr`.
- Whether any persistent heap keeps a `ResHolder` for an actor or tileset.

A matching decompilation only becomes necessary if stale archive entries are
observed across a course transition.

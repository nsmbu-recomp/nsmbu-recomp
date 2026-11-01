# Opening a course

`nsmbu open 1-5` and `nsmbu open game/content/Common/course_res_pack/1-5.szs` enter the same course.
The file name and the course id are the pack name `1-5`.
A bare `1-5.szs` is read from `game/content/Common/course_res_pack/`.

The command passes the pack to the native boot skip.
The native skip applies save file 1, then runs the same story transition as
`Scene.load_pack`.
`load_pack("1-5")` is `load_story(0, 4)`.

## Pack name

The pack string is `world-course` with the numbers players see.
World is 1 through 9.
The course number in the name is 1 through 256.

`CourseInfo` stores both numbers minus one.

| Pack | World byte | Course byte | File |
| --- | --- | --- | --- |
| `1-1` | 0 | 0 | `course_res_pack/1-1.szs` |
| `1-5` | 0 | 4 | `course_res_pack/1-5.szs` |

World byte 0 is Acorn Plains, the first world.
Course byte 0 is level 1 of that world.
A loading card that still has those two bytes says level 1 of Acorn Plains.

The file name is `sprintf("%d-%d", world_byte + 1, course_byte + 1)`.
That string is the `%s` in `course_res_pack/%s.szs` (`f_029D3150`).
`course_no` is the pack course minus one.
It is not a separate map-node index.

## CourseInfo

`CourseInfo::sInstance` is `0x101D18EC`.
The object is 0x24 bytes.
`mCourseNow` starts at `+0x10`.
`mCourseNext` starts at `+0x1C`.

| Field | Offset | Role |
| --- | --- | --- |
| Now world | `+0x10` | World of the course that is already current |
| Now course | `+0x11` | Course of the course that is already current |
| Now file | `+0x12` | Save file index |
| Now goto | `+0x13` | Next entrance inside the course. `0xFF` means unset |
| Next world | `+0x1C` | World of the course being entered |
| Next course | `+0x1D` | Course of the course being entered |
| Next file | `+0x1E` | Save file to use for that entry |
| Next goto | `+0x1F` | Entrance for that entry. `0xFF` means unset |

The constructor writes 0 to the world and course bytes, both now and next.
A new file therefore starts as pack `1-1`.

`CourseInfo::setNextWorldNo` (`0x024D293C`) writes `+0x1C` only when `+0x1C` already equals `+0x10`.
`CourseInfo::setNextCourseNo` (`0x024D2954`) writes `+0x1D` only when `+0x1D` already equals `+0x11`.
If next and now already differ, the setter returns and the pack does not change.

`Scene.load_pack` copies now into next before `Scene::setCourse_Story` (`0x029941D0`).
The setters then accept the requested world and course.
`setCourse_Story` requests `CourseTask` `0x0202B4C8`.

Course setup `f_024BD86C` calls `f_024D29A4`.
That function copies `+0x1C` into `+0x10` and `+0x1D` into `+0x11`.
The pack file is chosen from the now bytes after that copy.

## Loading card

The card is drawn before that copy.
Until the copy, now is still the save's course.
On a new file that is world 0, course 0, so the card says level 1 of Acorn Plains even while next already holds `1-5`.

The banner reader uses next when next is a real course and differs from now.
After the copy, next and now match, so the same reader keeps the course you entered.

Two pieces of the card use that pair.

### Green digits

The green number is not MSBT text.
Course setup writes three picture-pane glyph bytes.
`0xFF` is a blank slot.
A digit from 1 to 5 maps to picture frame `digit - 1`.
Level 5 uses frame 4.
A digit above 5 has no frame in this map, so that glyph stays blank.
The bytes live on the course-task entry and on the copied `FieldGameData`.
Entering from the world map fills them on the save path (`f_024BADF4`).
A direct `load_pack` skips that fill.
The port does not refill those bytes.
A direct open can leave the green number blank.

### WorldCourse text

The words "Acorn Plains" and the level number in the sentence are an MSBT `WorldCourse` message.
The message tags read `MainGame` (`0x101E7D7C`) at `+0x19C` (world) and `+0x1A8` (course).
`f_0267CB3C` prints `course + 1`.
Course byte 0 prints 1.
World byte 0 selects Acorn Plains.
The port does not rewrite those tags on a direct open.
The card can still name the save's world.

## State a direct open needs

1. Next world and next course equal the pack (`0` and `4` for `1-5`), and they equal now at the moment `setNext*` runs so the write is accepted.
2. The loading card reads next while it still differs from now.
3. `MainGame+0x19C` and `MainGame+0x1A8` hold that same pair before the `WorldCourse` text expands.
4. The three glyph bytes hold the 1-based digits of `course + 1`.
5. After `f_024D29A4`, now matches next, and `course_res_pack/1-5.szs` is the file that loads.
6. On return to the world map, map checkpoint and pose must match that pack.
   Direct open skips the map enter path that calls `FieldGameData::setCheckPoint` (`0x024BADF4`).
   Without a resolvable pose, `CourseSelectPlayer` never appears on the node.
   The runtime seeds checkpoint and `MainGame+0x19C/+0x1A0/+0x1A4` from `CourseInfo` world/course when the checkpoint is still missing.
   Checkpoint `index` is the path node index (`W101` is 1).
   Course slot (`node+0x58`, 0 for `W101`) is a different field.
   Hooks run on course-out and CourseSelectPlayer setup.
   Resolution walks `CourseSelectMgr` paths for a node named like `W101` (pack `1-1`).
   Do not match on path node `+0x50/+0x54` alone; those fields are often `0,0` for many nodes (including the world start).
   Do not use the save file `worldMapLocation` alone; the template save is not the opened course.
   Live `CourseSelectPlayer+0x110` is an object pointer (used after `NMSndObject::calc`), not world position.
   Only write node floats into `+0x110/+0x114/+0x118` during createLater packing.
   After spawn write `+0x50/+0x54/+0x58` only.
   The map camera copies its focus node at `0x0227CCF4` and `0x0227E2A8`.
   Those sites replace the node with the opened course so the camera and title follow that node.
   Applying a save (`f_029D7648`) calls `f_029CF2D0`, which stores exit type `1`, so that value is also a fresh file load.
   The pose seed and the camera replace run only after course-out (`0x024B8324`).
   Opening the first save and the opening cinematic never take that path, so the map keeps the save's checkpoint and its own camera.
   If the replacement node's `+0x50` is negative, the site leaves the game's node in place.
   The runtime also writes the save slot `worldMapLocation` (world byte is 1-based) and checkpoint byte `+0x43` to that node index.

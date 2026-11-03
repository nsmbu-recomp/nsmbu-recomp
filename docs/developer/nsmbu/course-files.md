# Course data files

[course-open.md](course-open.md) covers the pack name and `CourseInfo`.
[level-runtime.md](level-runtime.md) covers the task graph, archive lifetimes,
and course hot reload.
This page covers the data the course itself is built from:
which structs hold it, where they come from, and how a modified
level file reaches the running game.

All addresses are USA v1.3.0 `red-pro2.rpx`.

## The two data sources

A course stage draws its data from two layers:

| Layer | What it holds | Where it comes from |
| --- | --- | --- |
| Resource pack | Textures, layout, audio, actor prefabs | `Common/course_res_pack/<pack>.szs` |
| Course data | Blocks: environment, entrances, map actors, areas, rails | see below |

The course data layer is a set of flat blocks.
The struct that points at them is `CourseDataFile`
(`vendor/decomp/include/map/CourseData.h`, 0x3B8 bytes).
It holds for each block a pointer, a byte size, and an entry count
(`mBlock`, `mBlockSize`, `mBlockEntryNum`, 15 entries, `cBlock_Num`).

`CourseData` is a 4-slot array of `CourseDataFile`
(0xEF0 bytes, 4 x 0x3B8) with the eraser/starter static pool at `+0xB0`.

## Normal flow: the pack

`Scene.load_pack` (`f_029D3150`) is given the pack name `world-course`
(for example `1-5`).
It opens `Common/course_res_pack/1-5.szs` through the runtime file device.
The runtime already replaces pack files in that directory
(`docs/modding/content.md`).
That is one path to load a modified level: replace the `.szs`.

## Custom course channel: save device `course/course%d.bin`

The game itself has a second channel that loads course data from the
save device, not from the pack:

1. The personal savedata root (`0x101E7D94`, fed from
   `save_act://rp_personal_savedata.dat`) keeps one record per
   world-course. The record name is the pack name `world-course`
   (built by the `%d-%d` site at `0x029CB3B0`, world and course + 1).
2. Inside that record sit four files `course/course1.bin` .. `course4.bin`.
   The format string is at `0x100E600C`; the index passed to the sprintf
   call at `0x02699B30` is slot index + 1.
3. `CourseData::instance()` (`0x0269A1B0`) lazily builds the singleton
   into `0x101EB048` / `0x101EB04C` (ctor `0x0269A144`, static pool
   pre-cleanup at `0x02699394`).
4. `CourseData::loadRecordCourseFiles` (`0x0269A244`) loops the four
   slots. Each slot calls the per-file loader (`0x02699AD4`):
   - format the name,
   - ask the personal savedata manager (`0x029D33EC`, vtbl-based,
     name + out pointer) for the file image,
   - the query returns a data pointer or null,
   - on success the loader stores the slot index into
     `CourseDataFile::mIndex` (first member, `+0`) and then parses the
     image.
5. The parser (`0x0269972C`) reads a table of pairs
   `{ content_offset, content_size }` (8 bytes per block) in front of
   the image and walks it:
   - entry size per block comes from a constant table in the
     `.data2` region at `0x100E5FCC`
     (`128, 24, 28, 8, 28, 20, 24, 24, 4, 28, 12, 24, 24, 12, 20`,
     matching `cBlock_Num` = 15; the first word `0x0269B36C` is a
     code address, so the table starts at `+4` and the actual C code
     indexes that size table with a compare ladder instead of a plain
     loop),
   - when the content byte size divides evenly by the entry size:
     `mBlock[i] = image + content_offset`,
     `mBlockSize[i] = content_size`,
     `mBlockEntryNum[i] = content_size / entry_size`,
     and the read parser skips ahead `content_size` bytes,
   - the non-divisible branch stores the byte size into the block
     pointer slot instead of a pointer (stub entry, as read),
   - slots that stayed empty get the out-of-image pointer (invalid
     marker) at the tail (`0x026998AC`).

Consumers read the slots through `CourseData::getFile(index)`
(`0x0269A2DC`; null-safe pure read that returns the slot whose
`cBlock_AreaData` block pointer at `+0x28` is set).
Two consumers are known today:

- `0x024BDEF0` — feeds map actor/area setup from the slots,
- `0x029CB390` — world-map boot does the same dance: build the
  `world-course` pack name, ask the personal savedata manager for the
  record, instance `CourseData`, load the four files, then register
  the map actors of every loaded slot against the map.

## What this means for a modified level file

Two working channels, in increasing invasiveness:

1. Pack replacement: ship a new `Common/course_res_pack/<pack>.szs`.
   The runtime file device already routes this. No save data involved.
2. The custom course record: put `course1.bin` under the
   `world-course` record the game asks for, and the game parses it into
   a `CourseDataFile` slot with the above parser. Blocks are plain
   memory images, no SARC wrapper, which makes this channel attractive
   for writing game-side edits (the style the coin-edit feature uses).

What is still open for 2:

- Who writes `courseN.bin` into the record (the save device write site,
  sibling of `0x029D33EC`).
- Which pack/flow opens a course using a loaded slot versus the pack.
- The exact producer of the pair table: likely one entry per
  `cBlock_*` (`cBlock_Num` = 15 matches), from the pack side.
- `registerBlocks` non-divisible stub branch reading: confirm against
  a real record when one is available.

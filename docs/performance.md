# Performance notes

Measurements and fixes from profiling the port on an Apple Silicon Mac, plus open leads.

## Native Windows checkpoint (2026-10-05)

Use `-DCMAKE_BUILD_TYPE=Release` with native LLVM/Ninja. The previous Windows
build cache selected Debug, leaving runtime/renderer code at `-O0`; Release now
uses `-O3 -DNDEBUG`. Exact guest FP settings remain unchanged.

Compared with the Windows port's performance notes, this renderer already has
asynchronous submission slots, mapped upload arenas,
grouped render passes, packed shader state, and descriptor reuse. Three missing
optimizations have been adapted:

- Windows guest deadline sleeps use SDL's high-resolution timer, including the
  sleep phase of precise vsync waits. Guest core release, freeze gates and
  completion callbacks retain their existing order.
- Index conversion retains thread-local scratch capacity. Immutable GPU uploads
  are copied before that scratch can be reused.
- Full texture content checks use BSD-licensed xxHash v0.8.3. Every mip and byte
  is still checked; sparse-check cadence and disk cache formats are unchanged.
  (Since 2026-10-06 both renderers decide when to check by page write tracking,
  see "Texture change detection" below; the sparse check is only the fallback.)

Swapchains now prefer mailbox when advertised, with FIFO fallback. The existing
override in **this** checkout is `NSMBU_VK_PRESENT_MODE` (the other checkout uses
`NSMBU_VULKAN_PRESENT_MODE`). To test presentation throttling separately:

```powershell
$env:NSMBU_VK_PRESENT_MODE = 'immediate'
./build/windows/nsmbu.exe
```

Immediate mode may tear. Remove the variable to restore mailbox/FIFO selection.
This setting retains guest GX2 pacing and does not guarantee 60 fps.

The Release timer probe (`build/windows/sleep_test.exe`) compares 80 five-ms
deadlines with `timeBeginPeriod(1)`, matching the game. One local run measured
standard-library mean/p95 lateness of 0.639/1.011 ms and SDL mean/p95 lateness of
0.249/0.532 ms. These are timer measurements, not gameplay FPS results. All five
CTest checks and the GPU renderer smoke test with synchronization validation
passed on the RX 9070 XT, with no reported validation errors.

Steady 60 fps at 3x resolution remains
unverified. Compare the same scene, camera and settings with warm caches;
disable validation and detailed profiling for FPS comparisons. Frame
interpolation increases displayed frames but still needs each rendered frame
to fit its budget.

## Windows draw batching default (2026-10-05)

Windows now enables the existing bounded 2,048-draw asynchronous batching by
default, with a maximum of two mid-frame submissions. Explicit values of
`NSMBU_VK_DRAW_BATCH` and `NSMBU_VK_DRAW_BATCH_CAP` retain their existing behavior.

## How to profile

A full course measurement uses `nsmbu replay --release 1-1`.

Read `docs/replay-1-1.md` for the 2026-10-17 Release profile.

Load slot 1 to reach 1-1.
Do this before you play through the title screen.
Slot 1 is `nsmbu/states/slot1.bin` when you use `nsmbu run` / `nsmbu launch`.
Otherwise it is under the platform config folder (`~/.config/nsmbu/states/` on Linux).
`NSMBU_STATE_LOAD_AT` is `TV frame:slot`.
Frame 700 is after the boot threads exist.
The log line `Loaded slot 1` means the load worked.

```sh
nsmbu build --release
nsmbu profile
```

`nsmbu profile` writes `.tmp/repro/nsmbu.log`.
Pass a slot (`nsmbu profile 1`), or `--exit-at`, `--load-at`, or `--log`.

Read `[prof]`, `[vulkan perf]`, and `[gx2] frame` in `.tmp/repro/nsmbu.log`.
A missing live thread logs `does not exist yet`.
Load at a later TV frame.
An exited thread in the save is ignored.

On macOS, sample the same command with `sample` (1 ms stacks for every thread).
The "Sort by top of stack" section shows where the CPU time goes.

Canonical addresses are USA v1.3.0 `red-pro2.rpx` (see [names.md](names.md)).

### Profilers

Game sampler (built in):

- Turn on with `NSMBU_GAME_SAMPLE=120`.
  `SIGUSR2` prints a report.
- You get a per-PC histogram and a per-thread table in the game log.
- The log is `nsmbu/logs/*.log` or `.tmp/repro/nsmbu.log`.

`perf` (external):

- Turn on with `nsmbu perf`.
- You get full stacks over every thread.
- Artifacts land in `.tmp/perf/`.

World-map profiles use the debug boot in [automation.md](automation.md).
`nsmbu scripted` changes present mode and resolution, so it is the wrong boot for a settings-matched map profile.

`nsmbu perf` runs the fixed 1-1 scene under `perf record`.
It writes `.tmp/perf/nsmbu.data`, `game.log`, `report.txt`, and `report_flat.txt`.
Attach to a live session with `nsmbu perf --attach PID --seconds 30`.
It asks for sudo and sets `perf_event_paranoid` to 1 when the kernel refuses sampling (per boot).

### Read the artifacts

`[gx2] frame N, X swaps/s`, `[gx2] slow swap`, and `[prof]` per-phase ms lines are the first check.
Steady 60 fps holds ~59.9 swaps/s.

A `[game sample]` report counts `pc - binary_base` of the built `nsmbu` binary (usually `build/debug/nsmbu`), CPU-time sampled.
The thread table at the bottom gives the busy share per game thread (`GX2 render` about 60-70%, `game` about 20% is normal).

`.tmp/perf/nsmbu.data` resolves against the binary by build ID.
Keep that binary unchanged until the data is read.

Name in-binary sample rows with:

```sh
python3 tools/game_sample_report.py LOG
```

That names the enclosing symbol.
`f_XXXXXXXX` rows print the header name from `tools/recomp/headers_names.txt`.

Rows with huge offsets are mapped libraries, and the log alone cannot name them.
Resolve by re-running with `/proc/<pid>/maps` captured, or use perf, which symbolicates everything itself.
Sample offsets between runs share low-12 bits with the same library, so clusters are comparable.
Bases are not.

`grep "XXXXXXXX" tools/recomp/headers_names.txt` first.
Full rules are in [names.md](names.md).
Reads of guest code live in `build/gen/code_*.c` (`f_XXXXXXXX`).

### Known hotspot map

Linux, Intel Arc + ANV, from 2026-10-13 profiles.

- libc `memcpy` / `memmove` / `memset` loops (~40%) are upload and decode copies.
  The `memcpy@plt` row is the funnel for the same calls.
- `gfxvk::decode_level_fast`, `XXH3`, and `LatteAddrLib::ComputeSurfaceAddr…` are the CPU texture decode, hash, and upload path.
- The vDSO cluster is busy clock reads in deadline waits (`[gx2] slow swap`, acquire wait).
- libc `ioctl` is GPU submissions (i915).
- `futex` / `pthread_cond` are job-queue wakeups.
- `f_02A15500` and `f_02A157FC` are an unnamed guest byte-at-a-time stream loop on the game thread (~5%).
  Name it via [names.md](names.md) before optimizing.

### Fixed-scene benchmark

Use `nsmbu run 1-1` and `NSMBU_STATE_LOAD_AT=700:1` (slot 1) in How to profile for the current NSMBU repro.

`nsmbu bench` opens one game and records a render trace for a named scenario (`nsmbu bench --list`).
For headless save-state A/B comparisons with interleaved variants, the statistics and orchestration
helpers live in `tools/bench/bench_lib.py` (unit tests only; not a supported CLI entry point).

## Where the time goes

Busy (non-waiting) samples in one 20 s in-game run with 60 fps interpolation on, after all the fixes
below (Apple M4):

| | Metal | Vulkan |
|---|---|---|
| Recompiled game code (`f_XXXXXXXX`) | ~3,600 | ~3,400 |
| Renderer (`gfx::`/`gfxvk::`, `gx2::`, driver) | ~2,300 | ~3,300 |
| Other (memcpy, clocks, malloc, objc, …) | ~2,900 | ~3,100 |

The recompiled code is about 17% of one core, and the GX2 render thread is about 30% busy, so the
CPU translation isn't the bottleneck on this machine; the renderer work matters more on slower CPUs
(upstream issue #7).

## Fixes

### Both-renderer build crashed at the first shader with Homebrew boost installed

With Metal and Vulkan in one app, `CMakeLists.txt` added Vulkan's and glslang's include directory
(`/opt/homebrew/include`) to `nsmbu` as a normal `-I` path, ahead of the vendored Cemu headers. With
Homebrew's `boost` installed, `metal_draw.mm` compiled against Homebrew's `static_vector` while
`cemu_latte` used the vendored one, so `LatteDecompilerShader` was 968 bytes on one side and 920 on
the other, and the first translated shader read a garbage `strBuf_shaderSource` (both renderers
crashed at boot). Those directories are now added as `SYSTEM`, which is searched after the project's
own paths. A Metal-only build never had the problem. With the fix, the 2,241 shaders of a test run
are byte-identical to the Metal-only build's.

### Pipeline queue scanned every frame (`build_pending_pipelines`)

At startup the shader cache and the head start queue ~250–280k pipeline recipes. A recipe is built
in the background once both of its shaders have compiled. `cache_warm_step` runs once per frame
inside `gfx::swap()` with a budget of 16 builds, but the budget only limited builds, not checks. So
every frame walked the whole queue, doing three hash lookups per recipe against a table of ~260k
shaders.

Each per-frame call now checks at most 2,048 recipes and resumes from a cursor on the next frame.
`--warm-shaders` passes an unlimited budget and still scans the whole queue.

| same 20 s run | before | after |
|---|---|---|
| `build_pending_pipelines` self time (render thread) | 6,086 samples (~40%) | 29 |
| whole-process CPU | 94% | 62–67% |
| pipelines built by the end of the run | 20,241 | 28,909 |
| draws skipped while a pipeline compiles | 373 | 247 |

### Pipeline recipes appended to the cache again

Pipelines built from the queue aren't recorded (they're already in the file). But when the game
needed a pipeline before its queued recipe was built, the normal path recorded it again. About 20% of
the pipeline records in a long-used `shaders.bin` were exact duplicates (33k of 170k), and the file
grew every session.

`g_known_pipelines` holds a hash of every recipe loaded from the cache file and the head start.
Recipes already in it aren't queued twice or written again. In the test run the cache grew 0 bytes
(it grew ~157 KB per run before), and startup queues 136k pipelines instead of 165k.

### Texture-coordinate snapping rewrote each shader 32 times (`snap_texcoords`)

Before compiling, every shader source is rewritten to snap 2D texture coordinates to the 1/256 texel
grid. The old code searched the whole source once per texture slot (32 slots) and inserted text in
place. Background compiles from `cache_warm_step` ran this on the render thread. It now makes one
pass over the declarations and one over the sample calls, then builds the output once.

The output must not change: the system Metal cache is keyed by the source, so any byte difference
would recompile every shader. Old and new produce identical output for 4,292 shaders dumped from a
play session (`NSMBU_DUMP_SHADERS=1`), plus hand-written edge cases (nested calls, two-digit slots,
non-2D textures, unbalanced parentheses). The new version is 25× faster (301 ms → 12 ms for that
set). In the 20 s run, memchr/memcmp under `compile_msl` dropped from ~670 samples to ~160.

### Render thread polled for compiles (`wait_compiled`)

When a draw needs a shader or pipeline that's still compiling, the render thread waits up to 25 ms
per frame (`NSMBU_COMPILE_WAIT_MS`) before skipping the draw. It used to poll: `usleep(100)` plus a
clock read in a loop. Metal's completion handlers now publish the result through `compile_done`,
which signals a condition variable that `wait_compiled` sleeps on until the result arrives or the
time runs out.

| same 20 s run | before | after |
|---|---|---|
| clock reads (`mach_continuous_time`) | 423 samples | 24 |
| draws skipped while a pipeline compiles | 247 | 0 |

The render thread now wakes as soon as a compile finishes instead of up to 0.1 ms (plus timer
slack) later, so fewer waits run out of time. Busy samples for the whole process went from ~10,400
to ~9,900 with both of these changes.

### Vulkan: fixed 2 ms spin before every vsync (`park_sleep_until`)

With the Vulkan renderer on macOS, the game thread's vsync wait slept until 2 ms before the vsync and
busy-waited the rest, because sleep timers wake late. Measured in game, they wake 0.25–1 ms late (rarely
1.75 ms), so most of the 2 ms was spent spinning: ~1,700 samples, 15% of Vulkan's busy CPU. The window
now follows the measured lateness: the largest of the last 120 wakes plus 250 µs, kept within
0.5–2 ms; a wake past the vsync puts it straight back at 2 ms. `NSMBU_VSYNC_SPIN_US=n` fixes the window.

| Vulkan, same 20 s run | fixed 2 ms | adaptive |
|---|---|---|
| clock reads in the vsync wait | 1,844 samples | 768 |
| busy samples, whole process | 11,393 | 9,806 |

Over a 60 s run, 20 of ~3,200 waits woke after the vsync (none with the fixed window), all by under a
millisecond of a 33 ms frame. Swaps slower than 36 ms were the same in both: 2.

### Metal: shader key rehashed on every state change (`stage_state_hash`)

A draw re-derives its shaders whenever a shader-relevant register changed (`g_shader_state_gen`):
~2,950 lookups per frame with 60 fps interpolation, ~60% of which gathered exactly the same ~400 words
as the previous lookup for that stage (the generation also moves for registers that aren't in the key,
and vertex and pixel shaders share it). Each lookup hashed all of them in a serial multiply chain. The
last gathered words and result are now kept per stage (two buffers, swapped on a change); an identical
gather returns the previous result after a `memcmp`. Same input, same key, by construction.

### Metal: index conversion (`build_indices`)

Indices were converted with a switch on the index format for every index and `insert` calls for every
triangle, into a vector allocated per draw. The format is now resolved once per draw (a template over
the reader), output is written into a presized buffer, and the draw reuses one vector (render thread
only). Old and new produce identical output for 12,675 cases (every primitive type, index format,
with and without an index buffer, counts 0–64); the new one is ~5× faster on large lists.

| Metal, same 20 s run | before | after |
|---|---|---|
| `get_shader_uncached` self time | 626 samples | 336 |
| `build_indices` self time | 291 | 57 |
| renderer samples | 2,701 | 2,262 |
| busy samples, whole process | 9,242 | 8,812 |

## Open leads

Ranked by expected payoff. Sample counts are from the 20 s run above.

1. **Boot time: shader cache replay (~6.2 s + 1.6 s head start).** `cache_load` decompiles all
   ~260k cached shaders on the render thread before the first draw. They're only 2,344 distinct
   programs: the key (`stage_state_hash`) includes render-target and texture state, so each program
   averages ~111 translations, and `shaders.bin` reaches ~290 MB. Options:
   - spread the decompiles across worker threads (the decompiler would need to be checked for
     shared state first);
   - store the translated MSL, so replay skips the decompile;
   - narrow the key. That is a correctness risk: every register in it changes the generated code
     somewhere.
2. **`OSSendMessage` (~300 samples, measured, not worth changing).** One job queue carries ~26,000
   messages a second (sender `0276035C`, one worker receiving). The `notify_all` almost always wakes
   a worker that is really waiting, which is necessary; macOS already skips the kernel call when no
   thread waits, so waking only on waiters would save next to nothing.
3. **Recompiled code (small).** `PPC_ENTER` checks the trace flag and the preemption flag on every
   function call; compiling the trace check out of release builds saves a little. Keep
   `-ffp-contract=off` and the exact rounding: they are what makes the results correct.

## CPU texture decode fast path and parallel save states (2026-10-12)

`perf` (Linux, after `sysctl kernel.perf_event_paranoid=1`) on the slot 3 stage with all-draw phase
sampling (`render_prof.cpp` now samples every draw op; regs keep the 1-in-64 mask) showed the GX2
render thread burning ~45% of its samples in
`LatteAddrLib::ComputeSurfaceAddrFromCoordMacroTiledCached` with ~13% in `gfxvk::upload_surface`
plus xxHash and page faults. `NSMBU_UPLOAD_PROFILE=1` traced it to two CPU-written surfaces that the
game rewrites every frame (2048x512 RGBA8, 4.0 MiB, and a 2048x512, 1.33 MiB mip chain,
`TM_2D_TILED_THIN1`), so `decode_level` re-swizzled ~5.4 MiB per frame through the per-texel address
math and the game paced at 46-52 logic steps/s.

The Vulkan decode now has two fast paths in `decode_level_fast` (surfaces.cpp), ported from Cemu's
`AddrLibFastDecode.h` pattern. `TM_2D_TILED_THIN1` maps each 8x8 micro tile with one cached
`ComputeSurfaceAddrFromCoordMacroTiledCached` call for the tile base and the precomputed
`microTilePixelIndexTable` for the in-tile permutation (with the >> 3 in-tile scramble for bpp > 32);
`TM_LINEAR_ALIGNED` walks rows directly. mip levels whose xxHash matches the stored per-level hash are
not re-uploaded, so a mip chain that only changes its top level skips the other eleven levels.
`NSMBU_DECODE_CHECK=1` runs both decoders and aborts on any difference; it stayed silent over a full
21 s in-stage run, and `--renderer-smoke` plus the 49 ctest suites pass.

Slot 3 in stage (Linux, 16 cores, RelWithDebInfo): lock 60.0 logic steps/s (was 46-52), frame
16.68 ms (was 20.4-21.7), render-thread CPU 6.6 ms/frame (was 9.9-10.0), draw op 6.0 ms/frame
(was 9.4), texture phase 5.2 ms/frame (was 8.6), game-thread `DrawDone` wait 5.7 ms/frame
(was 12.9). Save states now win from the parallel capture/restore/memory walk and Linux `mincore`
page tracking in `state_memory.h`/`platform/host.h`: restore 262 to 70-85 ms, capture inside the
440-790 ms range with the game frozen. `NSMBU_STATE_RESIDENT_OFF=1` keeps the old per-call
`memory_touched` behavior for comparison.

## Per-mip xxHash reuse on full checks (2026-10-13)

After the register-path work, `nsmbu perf` on 1-1 still spent most GX2 render draw time in the
**textures** phase (`XXH3_hashLong_64b_default`, `decode_level_fast`, memcpy, upload).
Decode already skipped mips whose per-level hash matched; a dirty full check still **re-hashed every
level** before that decision.

Vulkan `upload_surface` (`surfaces.cpp`) now reuses stored `levelHash[level]` when write-watch is
active, the surface is not `dirty`, the level already has a valid hash, and no guest page in that
level was written since the prior `watchStamp` (captured before `arm()`). The combined `contentHash`
is still the same FNV mix over per-level hashes; only xxHash work is skipped on stable mips.
Single-level surfaces that rewrite every frame still hash in full. `ctest` `write_watch` passes.

## Register-state memo and write-watch clock (2026-10-13)

Linux, Intel Arc B580, i5-12600KF (Alder Lake hybrid).
`nsmbu perf` on the fixed 1-1 scene (`nsmbu/states/slot1.bin`, TV frame 700, exit 1100, 25 s
`perf record` at 997 Hz, `-e cycles:u`, DWARF stacks).
Artifacts: `.tmp/perf/before/` (pre-change) and `.tmp/perf/after/` (with the fixes below);
`build/nsmbu` was unchanged between each record and `perf report`.

`perf` on hybrid Intel CPUs records both `cpu_atom/cycles/u` and `cpu_core/cycles/u`.
The cycle table uses the **P-core** column (`cpu_core`) from `perf report --group` with
`--no-children`, summing every row for that symbol (all threads).

Two changes in the per-frame register stream:

- **`gx2::shader_irrelevant`** (`gx2_core.cpp`) answers from a 64 KiB bool table built once on
  first use from the unchanged predicate (`raw_shader_irrelevant`), instead of three range walks
  and a modulo per register word. Indices past `kNumRegs` still return false.
- **Write-watch sequence** (`write_watch.cpp`): `g_seq` always advanced in lockstep with `g_clock`
  (one bump per write fault, host write, or hint), so `write_seq()` now returns `g_clock - 1` and
  each hint, fault, and host write does one atomic RMW instead of two. Page stamping is unchanged.

`tools/run/perf.py` passes `--call-graph none` for the flat report (perf 7.0 removed
`--no-callchains`).

| Symbol (P-core sample periods, ~6.9 s / ~5.8 s windows) | before | after |
| --- | ---: | ---: |
| `gx2::shader_irrelevant` | 86.2 M | (below report threshold) |
| `gx2::apply_small_regs` | 230.5 M | (below report threshold) |
| `wwatch::hint` | 23.7 M | 18.2 M |
| `gx2::execute` | 98.5 M | (below report threshold) |

After the memo, `shader_irrelevant` and `apply_small_regs` no longer accumulate enough samples to
name in the flat report; cost moves out of those symbols rather than disappearing from the frame.

Matched `[prof]` window at TV frame 1080 (120 frames, 59.9 swaps/s, 13,004 register writes/frame,
195 draws/frame):

| | before | after |
| --- | ---: | ---: |
| `regs` op ms/frame | 0.72 | 0.37 |
| regs ms per 1000 writes | 0.055 | 0.028 |
| render-thread CPU ms/frame | 9.82 | 5.20 |
| render-thread in ops ms/frame | 9.97 | 5.32 |
| game-thread `DrawDone` wait ms/frame | 8.71 | 4.29 |

At similar register volumes earlier in the run (~8.3–12.5k writes/frame), regs ms per 1000 writes
fell about 45–50% (for example 0.42 ms at 8,348 writes to 0.21 ms at 8,348).
Frame time stayed at the vsync ceiling; the gain is lower render-thread op time and more
`DrawDone` headroom, not higher swaps/s on this scene.

## Partial upload of the 1-1 CPU texture (2026-10-17)

`docs/replay-1-1.md` measured a 4.00 MiB texture upload on every logic frame of 1-1.

That was most of the render-thread CPU inside the vsync limit: textures 1.98 ms/frame, render-thread CPU 3.53 ms/frame.

The surface is `TM_2D_TILED_THIN1`.

Write tracking already saw only a few host pages change, and the uploader still hashed, untiled, and copied the whole level.

Vulkan `upload_surface` now keeps a linear copy and a page-to-tile map for a large single-slice thin1 texture (`surfaces.cpp`).

A later frame rehashes only the written pages.

When fewer than 96 pages differ, it retiles those 8x8 blocks and copies those rectangles.

A full rewrite, a scaled image, or any other tile mode still takes the whole-level path.

That path decodes straight into the staging buffer when the fast untile applies, so the temporary image is not cleared and then copied.

`NSMBU_DECODE_CHECK=1` still compares a partial untile with the full decoder.

The renderer smoke test covers a one-page thin1 change, a wide change, and the page update after that wide change.

Release `replays/1-1.replay` on the same machine as the note (i5-12600KF, Arc B580, Mesa 26.2.4), 2x, FXAA, anisotropy, GamePad off, FIFO:

| | before | after (TV frame 2760) |
| --- | ---: | ---: |
| swaps/s | 59.9 | 59.9 |
| textures ms/frame | 1.98 | 0.33 |
| texture upload MiB/frame | 4.00 | 0.00 |
| render-thread CPU ms/frame | 3.53 | 1.68 |
| game-thread DrawDone wait ms/frame | 2.38 | 0.72 |

Frames 1800–2760 stay on the vsync limit.

The replay still reports 7 position mismatches, first at step 1036.

## Uncapped 1-1 replay: texture re-hash and small copies (2026-10-20)

`perf` needs `perf_event_paranoid` at 1 or lower, which this machine does not allow.
The numbers below come from a `SIGPROF` leaf sampler on the thread CPU clock (1 ms) attached with `LD_PRELOAD`, plus the `[prof]` windows of `nsmbu replay --release --uncapped`.

A replay with its position columns stops at the first divergence, so the uncapped runs use a copy of `replays/1-1.replay` without those columns; it plays all 2420 steps.
The machine and build match the note above, with a warm shader cache.

The render thread was the largest CPU user (about 55% of samples).
Two costs came out of it:

- The 4 MiB CPU texture was hashed on every frame.
  Surface `41410800` is 2048x512 `TM_2D_TILED_THIN1`, one level of 4.00 MiB.
  The game calls `GX2Invalidate` on it every frame, which set `dirty` and made `upload_surface` hash the whole level with XXH3.
  Write tracking showed that none of the written pages had changed, so the hash only confirmed that the image was unchanged.
  `dirty` now tells apart changes that write tracking cannot see (`dirtyOob`: `GX2CopySurface` into the surface, a save-state load) from an invalidation that only announces CPU writes.
  In-band changes therefore use the per-page verdict and the 4 MiB hash is skipped; out-of-band changes still take the whole-level path.
- Small copies went through libc.
  `apply_small_regs` compared and copied every register set with `memcmp` and `memcpy`, twice when a context shadow was bound, and the command queue copied every payload with `vector::insert`.
  Sets of 16 words or fewer now compare and copy word by word, the shadow is written only when a word differs, and small payloads are appended without `insert`.

Frames 1200-3300 of the uncapped 1-1 run, four runs each, medians of the 120-frame `[prof]` windows:

| 2x internal resolution | before | after |
| --- | ---: | ---: |
| render-thread CPU ms/frame | 1.35 | 1.14 |
| textures ms/frame | 0.27 | 0.08 |
| regs ms/frame | 0.42 | 0.36 |
| draw op ms/frame | 0.87 | 0.70 |
| frame time ms | 1.75 | 1.75 |
| swaps/s | 571 | 572 |

At 2x the GPU takes the CPU time that was freed, so frame time does not move.
The same replay at 1x internal resolution is CPU-bound, and there the faster render thread raises throughput:

| 1x internal resolution | before | after |
| --- | ---: | ---: |
| render-thread CPU ms/frame | 1.36 | 1.10 |
| frame time ms | 1.51 | 1.22 |
| swaps/s | 663 | 818 |
| whole run wall time | 6.15 s | 5.81 s |

The capped replay behaves as before: both builds stop at the same divergence, step 17, recorded 600.000 and now 600.100.
`ctest` and `--renderer-smoke` pass.

## Texture change detection (2026-10-06)

The renderers used to re-hash a CPU texture in full only when it was new, invalidated by an exact
GX2Invalidate range, or every 64 frames (phased by frame and address); in between they compared 256
sampled words (Metal: of the base level only). The game changes textures in place without announcing
it, so a change between the samples showed up to 63 frames late, and two runs reaching the same scene
at different frame counts could render it differently.

Now both renderers use page write tracking (`runtime/src/core/write_watch.cpp`): each full check
write-protects the host pages of all the texture's levels and records a stamp; the first CPU write to
such a page faults once, the handler stamps the page and makes it writable. A lookup re-hashes (every
byte, every level, xxHash) only when a page carries a newer stamp, or when the game signalled a change
(GX2Invalidate on the range, GX2CopySurface into it, a loaded save state). Unchanged textures cost a
stamp comparison per page and no hashing. Kernel writes into guest memory (FSReadFile's `fread`)
are bracketed with `wwatch::HostWrite`, since a protected page would make the read fail with EFAULT
instead of faulting. The sampled check remains only for hosts where page protection is unavailable.
The 300-frame `[gfx]` report (Metal) and the `[vulkan textures]` line (`NSMBU_VK_CPU_ONLY_STATS=1`) show
full checks, hashed bytes (Metal), uploads, page write faults and pages protected;
`NSMBU_LOG_TEXCHECK=1` (Metal) logs which textures were re-checked because of a write.


## Guest register reloads after calls (2026-10-21)

Functions with calls now keep the guest registers in C locals between calls and reload only the ones a later read needs.
A backward liveness pass over each function's statement graph (`tools/recomp/leaflocal.py`, `NSMBU_RECOMP_NONLEAF_LIVE`, on by default) drops the reloads of registers and float halves that are overwritten before they are read again, and keeps the ones a return, a tail call or a loop-head sync still needs.

The recompiler emits one reload statement per register per call, and the game has 173,463 calls in functions with calls.
`report.txt` therefore counts 2,918,797 reload statements with the pass off and 621,916 with it on: 79% of them are gone.

`NSMBU_RECOMP_NONLEAF_ABI`, an earlier version that also left r0, r11 and r12 in their locals at a call, was removed because both halves break the game.
A generated callee loads `c->r[0]`, `c->r[11]` and `c->r[12]` in its prologue, and 6 and 36 guest functions read r0 and r11 before writing them, so skipping the write-back loses their input (the boot hangs before `[gx2] initialized`).
A callee with calls of its own writes those registers back at its own calls, so skipping the reload leaves the caller a stale copy (SIGSEGV at guest address `00000019`, `lr=02000278`).
In a plain (struct form) recomp, 0 of 84,509 functions read r12 before writing it and 40 read r0 or r11, but a sound skip needs the callee's live-in set at each call site for about 15% of what the live pass already saves.

Release `replays/1-1.replay`, uncapped copy without the position columns, frames 1200-3240, median of the 18 120-frame `[prof]` windows, three runs each:

| | 2x off | 2x on | 1x off | 1x on |
| --- | ---: | ---: | ---: | ---: |
| frame time ms | 2.070 | 2.085 | 1.325 | 1.280 |
| swaps/s | 482 | 480 | 753 | 782 |
| render-thread CPU ms/frame | 1.165 | 1.195 | 1.115 | 1.070 |
| guest-thread `f_*` CPU samples | - | - | 911 | 836 |

Frame time and swaps/s stay inside the run-to-run spread (the medians above are within 4%): the guest thread has slack at 24% of the process CPU and the render thread or the GPU sets the pace (56% busy at 2x, 84% at 1x).
The direct effect is on the guest code.
A 1 ms `SIGPROF` leaf sampler on the thread CPU clock, attached with `LD_PRELOAD`, counts the guest thread's samples in `f_*` code between 881 and 928 with the pass off and between 830 and 884 with it on, about 8% fewer.
That is CPU headroom, not frames, on this machine.

`nsmbu recomp` writes the reload count to `report.txt`; recompile the same tree with `NSMBU_RECOMP_NONLEAF_LIVE=0` for the baseline.
A build with the pass on must still print `62 position checks` and `replay finished at step 2420, player state matched` for `nsmbu replay --release 1-1`.
`python3 -m unittest tools/recomp/test_leaflocal.py` covers the transform itself.

Three things mislead while measuring this.
The `[prof]` windows for one setting swing up to 20% between runs here, so compare three runs per side and use the medians; a single pair can show a gain that is not there.
`perf` is unavailable (`perf_event_paranoid=4`), which is why the guest CPU number comes from a `SIGPROF` sampler on the thread CPU clock.
`NSMBU_RECOMP_LEAF_POISON=1` fills the locals that a reload would have overwritten with garbage, and it also writes garbage into the `Cpu` struct at every return, so it is good for a short boot and not for a full replay (SIGSEGV at guest address `00000034` with r5-r12 all `DEADBEEF`).

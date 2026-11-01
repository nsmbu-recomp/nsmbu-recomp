# Vulkan renderer

The Vulkan renderer (by OpenAI Codex, originally the `vulcan` branch) translates GX2 to Vulkan; on a
Mac it runs on MoltenVK. The shader translator and texture addressing reuse the vendored Cemu
components under MPL-2.0; Cemu's emulator runtime is not needed.

## One app, two renderers (macOS)

The default macOS build (`NSMBU_RENDERER=BOTH`) contains the Metal renderer and the Vulkan renderer.
`runtime/src/gfx/renderer.h` is the interface: each renderer fills a table of entry points
(`gfx/metal/metal_backend.mm` for Metal, namespace `gfx`; `gfx/vulkan/backend_table.cpp` for Vulkan,
namespace `gfxvk`), and the GX2 layer, save states, menus and input call `render::*`, which
forward to the renderer chosen once at start-up:

1. `--renderer=metal|vulkan` (or `--renderer metal|vulkan`) on the command line,
2. `NSMBU_RENDERER_RUNTIME=metal|vulkan` (tests),
3. the saved choice from **Graphics > Renderer** (`renderer` in
   `~/Library/Application Support/nsmbu/display.plist`), which takes effect at the next start; the
   menu offers "Restart Now" (the game relaunches itself with the same arguments),
4. Metal.

If Vulkan cannot start, the game falls back to Metal and shows why (log, a sheet on the TV window,
the window title says "Metal (Vulkan unavailable)"): no Vulkan loader or glslang installed (both are
weak imports, so the app still starts without them), no driver (MoltenVK), no device with dynamic
rendering. `NSMBU_VK_FORCE_INIT_FAIL=1` forces this path for testing.

Both renderers use the same AppKit host (`gfx/metal/display.mm`, `menu.mm`, `input.mm`,
`controls_ui.mm`, `mods/mouse.mm`): TV and GamePad windows, full screen, GamePad screen modes
(separate window, picture-in-picture, automatic), scaling filters, aspect ratio, the Controls window,
save states. Vulkan presents into
the windows' views through a `CAMetalLayer` and `VK_EXT_metal_surface`; `display_plan()` gives it the
same layout as the Metal composition (picture rectangle, GamePad overlay with frame and opacity,
filter), and `gfx/vulkan/present.cpp` draws it. The TV window title starts with the renderer in use.

| Feature (macOS app) | Metal | Vulkan |
| --- | --- | --- |
| TV / GamePad windows, full screen, display modes, PiP, automatic PiP | yes | yes |
| Scaling smooth / sharp / integer, FXAA | yes | yes |
| Internal resolution, 16x AF | yes | yes |
| Aspect ratio (16:10, 21:9, 32:9, match window) | yes | yes |
| 60 fps interpolation | yes | yes |
| Save states (menu, Shift+F1..F5 / F1..F5) | yes | yes (a state loads with either renderer) |
| Controls window, keyboard, game controllers, mods | yes | yes |
| Frame dumps `NSMBU_DUMP_FRAMES`, `NSMBU_DUMP_PRESENT` | yes | yes |
| Capture frame (P) | pictures + draw log | pictures only (no draw log) |
| Screenshot key (F10, `runtime/src/gfx/screenshot.h`) | yes | yes (read back at the submission's fence, no wait) |
| Shader head start (`--warm-shaders`) | yes | no (Vulkan keeps its own SPIR-V / pipeline caches) |

Other builds: `-DNSMBU_RENDERER=METAL` (Metal only, no Vulkan dependencies) and
`-DNSMBU_RENDERER=VULKAN` (Vulkan only with the portable SDL3 host: SDL windows, input and audio;
the default on Windows/Linux). Test runs: `NSMBU_HIDDEN_WINDOWS=1` keeps the windows off screen
(frame and present dumps still work), `NSMBU_EXIT_AT_FRAME=n` quits in order at frame n,
`NSMBU_TEST_RENDERER_SWITCH=frame:metal|vulkan` does what "Restart Now" does, `NSMBU_LOG_TITLE=1`
logs the window title.

For macOS Vulkan save-state comparisons, use `NSMBU_DUMP_LOAD_FRAMES=n,...` to dump TV,
GamePad and optional presented images at renderer-frame offsets from the completed load
(`load_frame_<n>.png`, `_drc.png`, `_present.png`). A requested load frame can complete a frame
later in another run; fixed-frame inputs and captures can therefore compare different game states.
Renderer offsets alone do not align interpolation phase at 60 fps. For a phase-aligned capture,
`NSMBU_TEST_CAPTURE_LOAD_COUNTER=n` selects a full-pass swap at the restored game counter plus n,
on the guest thread before the render queue. It uses the ordinary `nsmbu/logs/<time>/` TV, GamePad
and presented capture files. Keep the selected counter and scenario origin identical between
barrier modes and retain the load/capture logs. Paused menus freeze the game counter; use
`NSMBU_TEST_CAPTURE_LOAD_STEP=n` instead to select a full-pass swap n logic steps after load.
Align both runs on the same restored clock with `NSMBU_TEST_CAPTURE_LOAD_STEP=n` and identical scenario origins.
An absolute startup mode switch can change random-seed evolution when a load completes one step later.

## Build

Use Clang/clang++ with CMake, Vulkan 1.3 headers and loader, glslang and zlib (and SDL3 and, on
Windows/Linux, LZ4 for the SDL host). A GPU/driver with dynamic rendering is required: Vulkan 1.3,
or Vulkan 1.1 / 1.2 with `VK_KHR_dynamic_rendering`. Dependencies must match the target architecture.

The executable imports no Vulkan functions (`VK_NO_PROTOTYPES`): `gfx/vulkan/loader.h` loads them
at run time through the loader's `vkGetInstanceProcAddr` / `vkGetDeviceProcAddr` (SDL host:
`SDL_Vulkan_LoadLibrary`; macOS app: the weakly linked Homebrew loader). On Windows and Linux the
Vulkan loader is not linked at all, so a missing loader or an older one (Vulkan 1.2 drivers on
Windows lack `vkCmdBeginRendering`) no longer stops the program before it starts. Without a usable
GPU, the SDL host shows a message box naming the GPU, its Vulkan and driver versions and what is
missing, and exits.

On macOS:

```sh
brew install cmake vulkan-headers vulkan-loader molten-vk glslang   # sdl3 too for NSMBU_RENDERER=VULKAN
cmake -S . -B build/cmake -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++   # BOTH
cmake --build build/cmake --parallel
# Vulkan only, SDL3 host:
cmake -S . -B build/vulkan -DNSMBU_RENDERER=VULKAN -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
```

Generate `build/gen` from your own game as described in the main README first.
Set `GEN_DIR` to use another generated-code directory. For Windows, use a Clang
CMake toolchain with the required dependencies; generated guest code requires
Clang's `musttail` support. Cross-compiling runtime files alone does not establish
that the executable links or runs on Windows/Linux.

## Check the renderer

```sh
./build/vulkan/nsmbu --renderer-smoke
```

This test does not read game assets. It checks GPU texture uploads, mip/layer
readback, clears, blits, depth/stencil, a generated triangle and presentation.
It also queues ten asynchronous submissions, wraps the four-slot ring, and checks
immutable upload payloads and deferred-buffer retirement by GPU readback, and the
scaled depth/stencil copies below (drawn, texel by texel, and through a real aspect-ratio and
resolution change of a depth target).

Scaled depth copies: a resolution or aspect-ratio change rescales every render target, and a
scaled GX2CopySurface scales its source. Vulkan makes blits of depth/stencil formats optional,
and some drivers (Adreno: `D16_UNORM` and `D32_SFLOAT`, issue #72) have none, so for such a format
the copy is drawn instead (depth through `gl_FragDepth`, stencil one bit per pass, no
`VK_EXT_shader_stencil_export` needed), with the texel a nearest-filter blit would pick. Devices
that can blit keep the blit. The first time a format takes another path, one log line names it.
If a device can neither blit nor draw a format, a whole destination is cleared (depth 1, stencil
and colour 0) and a partial one keeps its contents, instead of aborting the game.
`NSMBU_VK_DEPTH_COPY=draw` forces the drawn copy on any device (test aid, e.g. on a desktop GPU);
`NSMBU_VK_DEPTH_COPY=none` takes neither the blit nor the draw for depth formats, which shows the
last resort.
To enable Khronos validation, install the Vulkan validation layers and set
`NSMBU_VK_VALIDATION=1`. On Homebrew, set `VK_LAYER_PATH` to
`/opt/homebrew/opt/vulkan-validationlayers/share/vulkan/explicit_layer.d`.
Test aids for older drivers: `NSMBU_VK_FORCE_API=1.2` (or `1.1`) makes the renderer treat every GPU as
if it reported that Vulkan version, so a Vulkan 1.3 GPU takes the `VK_KHR_dynamic_rendering` path;
`NSMBU_VK_HIDE_EXTENSIONS=VK_KHR_dynamic_rendering` (comma-separated names) hides device extensions,
which together with `NSMBU_VK_FORCE_API=1.2` shows the "missing features" error.
If the loader does not discover MoltenVK, set `VK_DRIVER_FILES` to its installed
`MoltenVK_icd.json` (Homebrew places it below `$(brew --prefix molten-vk)/etc/vulkan/icd.d`).

## Current scope

This backend is under development. It records real Vulkan draw calls and translates
Latte shaders to SPIR-V. Guest GX2Flush, GX2DrawDone and presentation queue work using four fenced
submission slots (see "CPU/GPU overlap" below); readbacks, captures and save states wait for
completion. Each
slot retains its command/descriptor pools and upload arena until its fence completes,
so memory use is higher than the original single-slot implementation.
Geometry shaders, transform feedback, multisampled guest surfaces and some format
conversions are rejected explicitly. Vulkan now provides live Graphics controls, including a
macOS menu; other Metal-specific menus remain unported. SDL mouse capture and camera
input are supported. MoltenVK devices without sampler LOD bias use zero bias,
matching the existing Metal backend; native Vulkan devices retain the bias. Shader warmup/cache behavior differs from Metal.

Vulkan keeps a persistent device pipeline cache under the host configuration
directory's `shadercache` folder. `NSMBU_VK_PIPELINE_CACHE=/path/cache.bin` selects
an isolated cache file; `NSMBU_VK_PIPELINE_CACHE=0` disables persistence. Setting
`NSMBU_SHADER_CACHE=0` also disables persistence unless an explicit Vulkan cache
path is supplied. Caches are matched to the device and pipeline-cache UUID;
incompatible, truncated, or checksum-invalid files fall back to an empty cache. New data is saved
atomically after 120 frames without new pipelines. First-time shader translation
and pipeline creation can still cause stalls.

Vulkan also persists compiled SPIR-V in the host configuration directory's
`shadercache/vulkan-shaders/spirv.bin`. `NSMBU_VK_SHADER_CACHE=/directory` selects
an isolated directory; `NSMBU_VK_SHADER_CACHE=0` disables disk storage. An explicit
directory overrides `NSMBU_SHADER_CACHE=0`. Canonical GLSL and stage deduplication
remains active in memory when disk storage is disabled. Fresh decompiler metadata
is kept for every guest variant; identical final shader programs share compilation.

Shader keys have two levels, like Cemu's base and auxiliary shader hashes but exact
about what the GLSL translation reads (`gfx/vulkan/shaders.cpp`, `gather_linkage` and
`variant_hash`). The linkage key is the program, the fetch shader, the pixel-shader input
table (input count, position import, parameter generation, and per input its semantic,
flat and noperspective bits), the vertex input semantics, and for vertex shaders the
viewport-transform enable, half Z, points and streamout enable; for pixel shaders the
output mask (`CB_SHADER_MASK`), the alpha test and the front-face import. The variant key
adds, for the texture units the program samples, their dimension and integer format, the
semantic ids of the parameters a vertex shader exports, and streamout strides when it writes
streamout; the units and exports are recorded from the program's first translation.
A pixel shader is translated for the draw's vertex shader: an input that none of the vertex
shader's exports feeds is declared as a constant with the GPU's default value for it
(`SPI_PS_INPUT_CNTL` DEFAULT_VAL) instead of an input, so the pixel shader's variant key also has
the mask of those inputs and their default values (`ps_link`). A declared input that no output
writes made the Adreno driver refuse the pipeline (PR #30, the boat ride after the sword and shield).
Render-target formats, samplers, buffer addresses and units the program does not sample
are not in the key. Keys that still translate to an identical shader (GLSL, resource
mapping, uniform offsets, descriptor ranks) share one shader and its pipelines.
`NSMBU_VK_SHADER_KEY_VERIFY=1` also computes the previous, wider key on every lookup and,
once per distinct wider key, translates again under the current registers and compares
the result with the shader the narrow key returned; any difference is logged as
`[vulkan shader key] VIOLATION` and counted in the stats (`=N` checks one wider key in N).
The current NSMBU repro is `nsmbu run 1-1` with `NSMBU_STATE_LOAD_AT=700:1` (slot 1); see
[performance.md](performance.md).

Sharing shaders whose translation is identical is the idea of PR #46 by rhemfur.

Pipelines are keyed by the two shader identities, the fetch layout, topology, attachment
formats, vertex strides and the fixed-function state the pipeline bakes in, normalized so
that ignored state (blend words of attachments that do not blend, stencil words with the
stencil test off, depth bias words with the bias off) does not create pipelines. The key is
a fixed-size struct hashed and compared as bytes. Descriptor sets are cached per
submission by a 64-bit hash of layout and descriptors, confirmed by comparing them.

The versioned cache checks the compiler recipe, exact shader source, checksum,
record bounds and SPIR-V structure. Incompatible or corrupt files become misses.
After 120 quiet frames, the render thread copies a bounded immutable snapshot
and schedules one background save. The worker serializes, checksums, and atomically
replaces the file; it does not access Vulkan objects or the live shader cache.
Completion is polled on the render thread. Shaders discovered during a save remain
dirty for a later snapshot, and failed writes remain eligible for retry. Orderly
window close joins any worker and saves the latest records before exiting. Forced
termination can leave the most recent additions unsaved. Files and live canonical
entries are bounded to 128 MiB. New programs compile once when
encountered; this does not precompute every unvisited scene or warm all device
pipelines before gameplay.

Set `NSMBU_VK_STATS=1` to report frame rate, frame-interval percentiles, upload
traffic, shader compilation, and GPU/presentation waits every 120 frames.
Use the performance overlay for live presented FPS; the window title shows the release and game versions.
Stats also report descriptor allocation/reuse, SPIR-V compilation and disk hits,
decompiler time, and Vulkan pipeline-creation time. Cache snapshot-copy time is
reported separately from background serialization and disk-write time.
On POSIX hosts, stats also report render-thread CPU milliseconds per frame using
`CLOCK_THREAD_CPUTIME_ID`, sampled once per reporting window. This excludes blocked
waits and OS descheduling, and excludes CPU work on other threads. Discard startup
reports when comparing warmed scenes. Uniform preparation, reuse hits/bytes,
descriptor bind opportunities, actual bind calls, and skipped sets are reported
alongside upload traffic; snapshot counts describe requests, including reuse hits.
Benchmark with validation disabled; the validation layer adds substantial CPU
overhead when gameplay submits thousands of draws each frame.

On macOS, direct Metal resource bindings are the default MoltenVK path. Set
`MVK_CONFIG_USE_METAL_ARGUMENT_BUFFERS=1` to restore argument buffers if needed
for larger resource limits or another device. A same-scene M3 Max comparison
measured about 30 FPS with argument buffers and 30–40 FPS with direct bindings;
these are historical measurements from before the later CPU fixes. The setting is documented in
[MoltenVK configuration parameters](https://github.com/KhronosGroup/MoltenVK/blob/main/Docs/MoltenVK_Configuration_Parameters.md).

## Graphics card

When more than one Vulkan device can draw the TV and GamePad windows, the game uses a dedicated GPU.
Debug in the settings overlay (` or F12) offers Prefer dedicated, Prefer integrated, or one listed card.
That choice is saved and applies the next time the game starts.
`NSMBU_VK_GPU` overrides the saved choice for one start.
Its value is `dedicated`, `integrated`, a card id such as `10de:2184`, or part of the card name.

On Windows, Intel's Vulkan driver (for example HD Graphics 520 with driver 101.2115) can crash inside
`igvk64.dll` when a depth attachment view does not state its usage and the title screen draws shadow
maps. The renderer enables `VkImageViewUsageCreateInfo` on attachment layer views for Intel GPUs
automatically. `NSMBU_VK_ATTACHMENT_VIEW_USAGE=1` forces it on any vendor; `=0` turns it off.

## CPU paths (on by default since 2026-10-07; formerly "opt-in CPU experiments")

The following paths are on by default on every platform (`runtime/src/main.cpp`,
`default_vulkan_cpu_paths`; before 2026-10-07 only in the Android port). Each is active only when its
environment value is exactly `1`; set it to `0` to turn that path off. Together they cut the
render-thread CPU time by 8-14% on an M3 Max and by about a quarter on a Galaxy S25 Ultra; see
`performance.md` ("Desktop CPU/GPU overlap and CPU path defaults") for the per-path
measurements. Timing comparisons should use the same binary, saved scene, graphics settings, warmed
caches and a quiet host, with validation disabled.

For timing with detailed per-draw instrumentation disabled, unset
`NSMBU_VK_STATS` and set `NSMBU_VK_CPU_ONLY_STATS=1`. This reports render-thread
CPU time, FPS, frame pacing, and cumulative shader/pipeline cache counters every
120 frames. It leaves comparison clocks, detailed preparation counters, and
wait timers disabled. CPU time excludes sleeping and GPU waits.

| Environment option | Scope and correctness constraints |
| --- | --- |
| `NSMBU_VK_REUSE_UNIFORM_SNAPSHOTS` | Reuses immutable uniform slices after full fresh byte comparison within the same device/submission generation. Support uniforms are packed before lookup; allocation alignment and zero padding remain unchanged. |
| `NSMBU_VK_REUSE_FEEDBACK_IMAGES` | Retains compatible feedback-copy images in separate shader-stage/texture-unit slots, bounded to 128 MiB of retained image allocations. Every attachment alias still receives a fresh copy, with barriers preserving prior reads before overwrite. Replaced resources remain fence-retired. |
| `NSMBU_VK_SKIP_REDUNDANT_BINDS` | Omits only exact consecutive descriptor bindings, including layout, sets, dynamic offsets, command buffer, and submission generation. It can bind just the changed stage; resource preparation still runs. Pass changes reset the tracked state. |
| `NSMBU_VK_DESCRIPTOR_RANKS` | Precomputes descriptor binding ranks and compacts active writes/offsets in binding order. Invalid or duplicate metadata falls back to the original sorting path. |
| `NSMBU_VK_PIPELINE_LOOKASIDE` | Adds eight exact pipeline-key entries ahead of the existing map lookup. Full key comparison, device checks, and reset handling remain in place. |
| `NSMBU_VK_SHADER_ADDRESS_MEMO` | Texture address words 2/3 no longer invalidate the last Vulkan shader lookup. Other register classification is unchanged; textures and attachment aliases are resolved afresh, and context/save-state loads invalidate shader memoization. |
| `NSMBU_VK_FETCH_MEMO` | Reuses the last fetch-program lookup within one frame after fresh header/range validation, preserving the existing once-per-frame program-byte hashing contract and save-state reset. |
| `NSMBU_VK_SPECIALIZE_INDICES` | Selects a typed endian/restart reader once per draw and directly fills expanded primitive indices. Native index eligibility and the final immutable index extent scan remain unchanged; wrapped big-endian guest ranges use the original reader. |
| `NSMBU_VK_SHADER_STATE_MEMO` | Retains four exact gathered linkage-key entries per shader stage. Every lookup freshly gathers all linkage words and compares every active byte, count, and program hash seed. Misses use the identical hash mixer; save-state and sentinel calls clear the memo. |
| `NSMBU_VK_SKIP_VERTEX_BINDS` | Omits only identical host vertex buffer/offset bindings after fresh snapshot preparation. Sixteen binding slots are guarded by command buffer, submission generation, and pass resets. Index bindings remain fresh. Combine with exact vertex snapshot reuse to make unchanged slice identities available. |
| `NSMBU_VK_SAMPLER_MEMO` | On unless set to `0`. Reuses the last eight immutable sampler handles after exact device, fresh sampler-word, compare/integer, and effective anisotropy matching. Texture preparation still runs before lookup. |
| `NSMBU_VK_SPARSE_HASH_MEMO` | Only used where page write tracking is unavailable (texture changes are detected by `runtime/src/core/write_watch.h`; the sparse check is its fallback). Compares all freshly read, ordered sparse texture samples before reusing their hash. Full texture checks, invalidation, and uploads remain unchanged. Retains 64 entries by default; `NSMBU_VK_SPARSE_HASH_ENTRIES=256` selects a bounded 16 MiB sample store. Oversized sample sets stream through the original mixer. |
| `NSMBU_VK_SHADER_KEY_DIRTY` | Avoids shader-generation bumps only for explicitly classified register bits absent from the current Vulkan shader key. Program headers, fetch strides, unknown registers, and context/save-state invalidation remain conservative. |
| `NSMBU_VK_VERTEX_HISTORY_REUSE` | Adds a second distinct vertex snapshot key per binding, requiring `NSMBU_VK_REUSE_VERTEX_SNAPSHOTS=1`. Every candidate receives a fresh full payload comparison within the same device/submission generation; changed bytes allocate a fresh slice. |

The four additional paths together with the previous ten and vertex snapshot
reuse passed a 2,400-frame Khronos game run with a guarded wide-view save-state
restore, resolution transitions, FXAA, and anisotropy.
The run exited normally without validation errors, and its actual presentation
capture was inspected. In this scene the 256-entry sparse memo matched all
roughly 170 checks per frame after warming; secondary vertex reuse avoided about
2.6 MiB of additional copies per frame, and the register classifier avoided about
1,460 generation bumps per frame. These are work counts; performance and default
selection still require controlled comparisons with validation disabled.

The first six paths together passed a 2,400-frame full-game Khronos validation
run, including a guarded save-state load and resolution/FXAA/anisotropy transitions,
with normal exit and no validation errors. The boot-time unused-fragment-output
warning also occurs with these paths disabled. All seven paths also passed the
native build, four CTest checks, actual-device Khronos smoke checks, and a separate
1,800-frame game run with a guarded save-state restore and inspected capture.
The measured scene shows about 4,244 fetch-memo hits and 150 pipeline-lookaside hits
per frame; roughly 4,935 descriptor sets and 0.50 MiB of uniform uploads are avoided
per frame. These are work counts, not timing claims. Preliminary CPU comparisons
were taken under substantial external CPU load and do not establish a final speedup;
all seven paths remain opt-in pending controlled measurements.

The index specialization passed 3,594,240 UBSan differential vector/extent checks,
265 additional edge checks, an independent source review, and the native build.
Private conversion microbenchmarks support the reduced decoding/expansion cost;
they do not establish game FPS gains. Both newer paths together with the previous
seven and exact vertex snapshot reuse passed a 1,800-frame Khronos game run with
a guarded save-state restore, normal exit, no validation errors, and an inspected
actual swapchain capture. The state memo avoids hashing on roughly 70% of its
2,150 lookups per frame in that scene. Controlled timing remains required before
default selection.

All ten paths plus exact vertex snapshot reuse also passed a 2,400-frame Khronos
run with guarded save-state restore, 1→1.5→2→3→1 resolution transitions, FXAA,
anisotropy, and inspected actual swapchain capture. Vertex bind elision skips
roughly 4,124 calls per frame in the saved scene while retaining fresh byte checks.
State memo differential QA passed 100,000 cases with UBSan. ASan could not run:
a process sample showed a runtime initializer deadlock before `main`.

## Guest buffer cache

The guest buffer cache replaces the per-draw copies of guest vertex arrays, index arrays and uniform
blocks into the upload arena with persistent GPU copies keyed by guest address
(`runtime/src/gfx/vulkan/buffer_cache_core.h`, glue in `buffer_cache.cpp`). It is **on by default everywhere**: macOS, desktop Linux (Steam Deck included), Android (issue #56:
0 verify mismatches, ~13 more presented fps in heavy views on an Adreno 830) and Windows (issue #91:
13-19% less render-thread time on an RX 6700 XT, no geometry problems); `NSMBU_VK_BUFFER_CACHE=1` turns it on and
`NSMBU_VK_BUFFER_CACHE=0` off on any platform.
Verify mode (`NSMBU_VK_BUFFER_CACHE_VERIFY=1`) keeps a CPU copy of every upload and compares guest
memory against it, so it never reads the mapped GPU memory (uncached on discrete GPUs: it ran at ~1 fps
on an RX 6700 XT before, issue #91).

**Verify mode:** on drivers whose upload memory is not host-cached (e.g. AMD on Windows) the verify
mode reads it back and becomes unusably slow; it is a diagnostic, not needed for normal play. Earlier
note: it stayed opt-in until it had been checked on
those hosts, where the page-fault handling it relies on costs more and Linux limits the number of
protected regions. Please run a normal play session, or the benchmark scene, once with
`NSMBU_VK_BUFFER_CACHE_VERIFY=1 NSMBU_VK_CPU_ONLY_STATS=1` and report:
- the `[vulkan buffer cache] verify:` lines (the mismatch count must stay 0; any `VERIFY MISMATCH` line
  is a bug, please include it),
- the `page write faults` count of the `[vulkan textures]` lines and the `protect failures` count of
  the `[vulkan buffer cache]` lines,
- and, if you can, render-thread CPU and FPS with `NSMBU_VK_BUFFER_CACHE=1` against `=0` on the same scene (two launches with those env vars).

- **Validity without hashing.** An entry is current while none of its pages has a newer stamp in the
  page write tracker (`runtime/src/core/write_watch.h`, shared with the texture checks): the upload arms
  (write-protects) the range before reading it, and the first CPU write to such a page faults once and
  stamps it. Kernel writes are bracketed by `HostWrite` (FSReadFile). Explicit guest signals stamp a
  separate hint array that only the buffer cache reads: `DCFlushRange`, `DCFlushRangeNoSync`,
  `DCStoreRange`, `DCStoreRangeNoSync` and `GX2Invalidate` of attribute or uniform buffers (in command
  order). A save-state load drops every entry. Nothing writes guest memory from the GPU (no stream-out;
  render targets stay GPU images). Checking an entry costs one atomic load when nothing was stamped
  anywhere since its last check, otherwise one stamp comparison per page.
- **What is cached.** Vertex-array prefixes, guest uniform blocks (FULL_CBANK shaders), native index
  data (with a CPU shadow and a memoized index extent, so draws never rescan it) and converted index
  data (big-endian, fans, quads, loops, other restart markers: a hit skips the conversion). A request
  with the same start and at most the cached size hits; a longer one uploads the longer range; ranges
  with other starts are separate entries. Packed uniform variables stay in the arena.
- **Dynamic ranges.** A range re-uploaded because of writes three times, each within four frames of the
  previous upload, is no longer armed and takes the arena path; it is tried again after 64 frames,
  doubling up to 2048.
- **Memory.** 32 MiB host-visible blocks, device-local when the device offers it (unified memory,
  resizable BAR or the 256 MiB BAR window, of which at most half is used). `NSMBU_VK_BUFFER_CACHE_MB`
  sets the budget (default 256). Replaced regions are retired with the recording submission and freed
  after its fence. Entries unused for 1800 frames are evicted, the least recently used ones when over
  budget.
- **Verify mode.** `NSMBU_VK_BUFFER_CACHE_VERIFY=1` (implies the cache) compares every hit with freshly
  read guest bytes (converted indices: a fresh conversion) and logs `VERIFY MISMATCH` with address and
  size; a difference caused by a write racing the check (newer stamp) is counted as "raced" instead.
- `NSMBU_VK_BUFFER_CACHE_HINTS=0` ignores the DCFlush/GX2Invalidate hints (write faults only).

With `NSMBU_VK_CPU_ONLY_STATS=1` the 120-frame report adds a `[vulkan buffer cache]` line: lookups,
hit rate, uploads, stale entries, dynamic bypasses, resident MiB, hints and protect failures; the
`[vulkan textures]` line shows the page write faults (textures and buffers together).

## Live graphics controls

In the macOS app the Graphics and Display menus and their keys work as with Metal. The SDL game window
also accepts the following plain-key shortcuts; repeated keydown events are ignored.
The shortcuts are scoped to the game window, respect host-input disabling, and do
not toggle settings while entering text. Menu checkmarks reflect pending settings;
resolution changes latch at the next render-frame boundary. Unsupported effect
controls remain disabled until the renderer reports support.

| Control | Behavior |
| --- | --- |
| `R` | Cycle internal resolution through 360p, 540p, 720p (the Wii U picture), 1080p, 1440p, and 2160p/4K, with lower choices scaled up to the window and each preset also available in the menu. |
| `N` | Toggle anisotropic filtering up to 16×, bounded by device support, for eligible mipmapped asset textures. |
| `8` | Toggle FXAA in final screen presentation. This is post-process antialiasing; it does not enable multisampled guest surfaces. |
| `6` | Toggle 60 FPS interpolation mode. Selecting it does not guarantee its target frame rate. |
| `T` | Toggle between 60 fps (interpolation off) and 120 fps frame interpolation. |
| Graphics menu: Scaling | Select smooth, sharp, or integer scaling for presentation. |

Startup overrides match Metal: `NSMBU_RES_SCALE` selects the initial internal scale
(0.5–4). `NSMBU_ANISO` defaults to off and a
nonzero value enables it. `NSMBU_FXAA` defaults to off and its presence enables it,
including a value of `0`, matching Metal's existing behavior.
`NSMBU_SCALE_FILTER=smooth|sharp|integer` selects the initial presentation filter;
smooth is the default. Live settings currently apply to this process.

A full-game Khronos validation run passed internal-scale transitions
1× → 1.5× → 2× → 3× → 1× with FXAA and anisotropy
enabled. Four CTest host regressions and the GPU smoke test passed. The native
macOS Graphics menu was verified through its accessibility item list. Captures of
actual swap output with FXAA on and off differed at 31,446 pixels, verifying that
the toggle reaches presentation. These checks establish the tested controls and
rendering paths, not higher-resolution frame-rate acceptance. Higher-resolution
performance remains unverified; contemporaneous 1× and 2× runs both measured
roughly 30 FPS while other CPU workloads were active.

## Save-state shortcuts

In the macOS app, the Save States menu and keys work as with Metal. In the SDL game window, `F1` through `F5` load slots 1 through 5;
`Shift+F1` through `Shift+F5` save those slots. Repeated keydown events are ignored,
and these keys do not reach the game's button mapping. The Screenshot binding (F10 by default,
Controls) works in every game window the same way; `NSMBU_TEST_SCREENSHOT=<frames>` scripts it. Host-input-disabled scripted
runs do not accept the shortcuts. State files remain in the configured state directory.

A load must pass the existing allocation, thread, and guest-stack guards. A state
that loads in the same process may fail during a new process's startup; matching
printed LR/SP values do not establish that deeper guest stack frames match. Loading
later can succeed if the worker threads reach compatible waits. Failed loads do not
bypass these checks or establish a valid benchmark starting point.

## The CPU never reads mapped upload memory, unless it is host-cached

The upload arena (`allocate_upload`) and the buffer cache's blocks are host-visible memory the CPU
writes and the GPU reads. On discrete GPUs that memory is uncached or write-combined (plain
host-visible system memory, or device-local BAR / resizable-BAR memory), and CPU reads from it are
about 100 times slower than cached reads. On Apple silicon (MoltenVK) all memory is cached, so such
reads do not show up there. Issue #44: v0.2.4 turned the vertex snapshot reuse paths on everywhere,
and their comparisons against `slice.mapped` cost an RX 6700 XT 57 ms per frame.

Rule: mapped upload memory is only written. Every reuse check or scan uses a CPU copy in ordinary heap
memory, kept next to the slice and filled before the slice is written from it (so both hold the same
bytes even when the game writes the range meanwhile):

- vertex snapshots and vertex windows (`NSMBU_VK_REUSE_VERTEX_SNAPSHOTS`, `NSMBU_VK_VERTEX_HISTORY_REUSE`):
  `vertex_snapshot_history.h`;
- uniform snapshots (`NSMBU_VK_REUSE_UNIFORM_SNAPSHOTS`): `uniform_snapshot.h`;
- native index extents: the buffer cache entry's shadow, or a CPU copy the arena slice is written from;
- converted indices are built in a CPU vector and scanned there.

`runtime/test/snapshot_cache_test.cpp` (CTest `snapshot_caches`) poisons every mapped byte right after
it is written and checks that the caches still find exactly the expected reuse hits. The only read of
mapped GPU-input memory left is the buffer cache's opt-in diagnostic `NSMBU_VK_BUFFER_CACHE_VERIFY=1`.
Buffers the CPU is meant to read (captures, the GamePad overlay signatures) come from
`create_readback_buffer`, which prefers host-cached memory.

**Unless it is host-cached.** Where the arena's memory type is both `HOST_CACHED` and `HOST_COHERENT`,
CPU reads cost what heap reads cost and the copies are pure overhead (on an M3 Max about 0.2 ms of
render-thread time per frame). That is every memory type on Apple silicon (MoltenVK),
and the usual case on UMA drivers (many Android GPUs, integrated GPUs that expose cached host memory).
`allocate_upload` records the type of each arena block it creates (`R.uploadCached`, true only while
every block is CACHED and COHERENT) and sets `R.uploadReadsDirect`; then:

- the reuse caches keep no copy: an entry's slice is written from the fresh guest bytes and later
  compared against `slice.mapped`, which holds exactly those bytes (slices are immutable until their
  submission retires). The mode is stored per entry, so a comparison always matches how its entry was
  made;
- the native index scan of the uncached index path scans the arena slice.

CACHED without COHERENT never counts: the arena requires COHERENT and does no flush or invalidate.
Uncached memory (discrete GPUs, Windows/Linux AMD and NVIDIA) keeps the copy path unchanged. The reuse
decisions and hit counts are identical in both modes; `snapshot_caches` runs every sequence in both
(poisoned arena for the copy mode, a readable one for direct reads) and checks equal hits, and that
direct reads against a poisoned arena miss.

`NSMBU_VK_UPLOAD_READS` overrides the choice for A/B runs: `auto` (default), `shadow` (always keep CPU
copies, e.g. to measure the discrete-GPU path on a Mac) or `direct` (always read the slices; slow on
uncached memory, a diagnostic only). The log line `[vulkan] upload arena memory: ...` reports the
memory and the mode.

## CPU/GPU overlap: lazy DrawDone and asynchronous presentation (all platforms)

Since 2026-10-07 every platform uses the two paths that were Android defaults before:

- `NSMBU_VK_LAZY_DRAW_DONE` (default on, `0` restores the wait): GX2DrawDone queues the work instead
  of waiting for an idle device. The renderer copies all guest data (vertices, indices, uniform
  blocks, textures) into fenced upload slices when it records the work and never writes GPU results
  back to guest memory, so the game only needs its commands executed, which `render_sync` already
  waits for (Cemu's GX2DrawDone waits for its GPU thread in the same way). Save states still wait
  for the idle GPU.
- `NSMBU_VK_ASYNC_PRESENT` (default on, `0` restores the drain): the presentation submission goes into
  the submission ring and `swap()` does not wait for the GPU, so the render thread records frame N+1
  while the GPU draws frame N. Both window hosts (SDL and the macOS AppKit host). Captures, frame and
  present dumps and the automatic GamePad overlay's signatures read back through a waiting flush.

The Metal renderer keeps its GX2DrawDone wait: it binds large vertex buffers straight from guest
memory. See `performance.md` ("Desktop CPU/GPU overlap defaults") for the measurements.

## Draw batching (all platforms)

Every platform defaults to submitting after 2,048 guest draws, with at most three
mid-frame submissions (measured on macOS and Windows; Linux uses the same defaults). Each submission retains its upload slices, descriptor
pool and deferred resources until its fence completes. The next draw reopens
attachments with `LOAD`, preserving draw order and contents. This uses the
existing asynchronous submission path.

Set `NSMBU_VK_DRAW_BATCH=0` to disable mid-frame batching, or set an explicit
positive draw count to tune it. Empty or invalid values disable batching.
`NSMBU_VK_DRAW_BATCH_CAP=1|2|3` sets the maximum mid-frame submissions (default three).
See `performance.md` for the local Windows comparison.

## macOS defaults and tuning

Mac/MoltenVK gameplay and correctness tests are the current priority. The macOS
Vulkan defaults now select command-buffer prefill mode 3, asynchronous batches of
2,048 draws with a cap of three extra submissions per frame, and precise vsync
sleeping. Explicit environment overrides take precedence. Windows/Linux defaults
remain unchanged and further device testing there is deferred. Vertex snapshot
reuse is on by default on every platform since 2026-10-07 (see "CPU paths").

| Option | Behavior and default |
| --- | --- |
| `NSMBU_VSYNC_PRECISE` | macOS defaults to enabled: sleep to near the vsync deadline, then spin for the final portion (up to 2 ms). Set `0` to disable. Other platforms keep the previous off default. Precise sleeping increases CPU use. |
| `MVK_CONFIG_PREFILL_METAL_COMMAND_BUFFERS` | macOS defaults to `3`; an explicit MoltenVK override, including `0`, is respected. Normal macOS render batches use an autorelease pool. Other platforms receive no application override. |
| `NSMBU_VK_DRAW_BATCH` | Defaults to `2048` when unset, on every platform. Set `0` to disable. Positive decimal values up to 1,048,576 select the batch size; malformed or out-of-range values disable batching. |
| `NSMBU_VK_DRAW_BATCH_CAP` | Accepts `1`, `2`, or `3`. When unset, defaults to `3`. An explicitly invalid value falls back to `2`. Has no effect with batching disabled. |
| `NSMBU_VK_REUSE_VERTEX_SNAPSHOTS=1` | Reuse a bounded vertex snapshot only after exact guest-byte comparison within the same fenced submission generation. On by default since 2026-10-07 (`0` turns it off). |

Draw batching keeps the four-slot fence retirement contract. More submissions can
reduce the final GPU tail, but add attachment LOAD/STORE boundaries and descriptor
cache resets, and can wait when the ring wraps. Capture/readback and save states still drain work. Use matching camera views and warm caches for
comparisons, and keep validation runs separate from timing runs.

On Apple M3 Max, warm static-view, camera-sweep, and character movement/swimming
benchmarks achieved 99.78%, 100%, and 99.45% coverage of nominal 59.94 Hz display
ticks. These percentages measure flip intervals divided by elapsed vsync ticks,
not the percentage of frames meeting the ready-time deadline. The static run
covered 1,799 flip intervals over 1,803 ticks; a capture inside its measurement
window can contribute to a long interval. The character run covered 1,799 intervals
over 1,809 ticks, with eight late-ready frames among 1,800 samples.

The camera sweep covered all 1,799 ticks with no late-ready frames among 1,800
samples over 30 seconds. Its 99th-percentile ready time was 13.558 ms and maximum
16.622 ms against the 16.683 ms deadline. It reached far views of roughly 6,500
draws per frame; this is not an exact reproduction of the earlier user view of
roughly 6,600 draws. Character checks are complete.

These results apply to the benchmarked scenes and configuration, not every area
or device. First-use shader/pipeline warmup can still hitch. Selected 60 FPS mode
and the live FPS title alone do not establish performance acceptance.

Windows/Linux save states use a separate LZ4 format version; they do not interchange
with Apple's existing compressed save states. Windows uses conservative safe
parking at host wait points; arbitrary guest-entry save parking is not enabled.

## Validation checkpoints

The selected macOS defaults passed a fresh-process Khronos gameplay validation run:
a guarded slot-5 state restore succeeded, the game reached the frame-1,800 checkpoint
and exited normally, and the log contained zero VUID reports, warnings, or errors.
A preceding cold-cache run reached frame 2,280 before its bounded duration timeout;
it was a partial run rather than the completed validation checkpoint. That cold
run compiled 518 canonical programs in 766 ms. Two background cache saves consumed
38.3 ms on the worker, with 2.2 ms of render-thread snapshot copying; the fresh
warm-cache process compiled zero programs. These measurements separate first-use
warmup cost from steady gameplay.

Earlier macOS/MoltenVK checkpoints passed four CTest host regressions and the GPU smoke
test with Khronos validation enabled, including upload-arena fence reuse and
interior mip invalidation, asynchronous slot wrap, and dynamic-UBO binding order
and reuse after pool reset. Earlier warm gameplay builds
ran approximately 29.4–30.0 FPS, with median frame intervals near 33.4 ms and
95th percentiles around 36–37 ms; occasional longer frames remain. A bounded
20-frame trace measured a median 17.53 ms from vsync wake to the next swap,
missing the 16.68 ms deadline with one vsync wait per frame in that earlier build.
At an earlier optimization checkpoint, descriptor/state deduplication, asynchronous
flush slots, word-based shader and texture hashing, and direct Mac bindings produced
a 60-second dock run that
measured 39.13 FPS across warmed frames 720–1920 (31.13–55.77 FPS in
120-frame intervals). The comparable argument-buffer run was about 30 FPS.
The subsequent historical checkpoint with dynamic uniforms and the consecutive
binding fast path measured 47.27 FPS (34.75–59.44 FPS per 120-frame interval), with zero
SPIR-V compilation after cache reload. Across the longer warm run it averaged
46.69 FPS. These scene-specific results do not establish smooth or sustained
60 FPS. Vertex position
invariance prevents multipass depth artifacts on the player. All 195 captured vertex
shader variants passed SPIR-V compilation with invariant position outputs.
Earlier validation runs reported unused fragment outputs during depth-only passes;
the final gameplay validation run above reported no warnings.
Broader gameplay and devices remain to be validated. On Linux (Ubuntu 24.04, Clang 18) the
executable builds without warnings and links against placeholder guest code
(`recomp/stubgen.py`), the unit tests pass and `--renderer-smoke` passes on Mesa lavapipe with
the Khronos validation layer reporting nothing (`.github/workflows/linux.yml`); gameplay on Linux
needs your own recompiled game and remains to be tested. On Windows (x86_64, Clang/MinGW: llvm-mingw
or MSYS2 CLANG64) the executable builds and links, the unit
tests pass, and `--renderer-smoke` passes under Wine with lavapipe (`.github/workflows/windows.yml`
builds and runs the tests natively); a run on Windows hardware with a GPU and gameplay remain to be
tested.

For an isolated capture, set `NSMBU_CAPTURE_PATH` to the PNG output path and
`NSMBU_CAPTURE` to the renderer frame number (default120). Keep these artifacts
private; they contain game imagery.

## Render-thread profiler

`runtime/src/gfx/render_prof.h` (both renderers) is on by default and cheap (`NSMBU_PROFILE=0` turns it
off). Every 120 frames it builds a report that `NSMBU_PROFILE=1`, `NSMBU_VK_STATS` or
`NSMBU_VK_CPU_ONLY_STATS=1` log as `[prof]` lines, and that the settings overlay's
**Copy performance report** button (Graphics tab) puts on the clipboard. Its two header lines
(`runtime/src/crash/report_header.h`) say where the report comes from: the version and commit (release
builds: the tag; others `git describe`, e.g. `v0.2.5+3`), the OS and version, the GPU with its driver
version (decoded as vulkaninfo does) and Vulkan version, or the Metal device; then the renderer, host,
frame mode, internal scale, buffer cache on/off, the gyro source when not off, and as `overrides:` the
CPU paths not set to `1` and lazy DrawDone / async present when turned off. The report itself:

- frame time, swaps/s, logic steps/s, render-thread CPU, time in GX2 ops and idle;
- ms per frame per op and, sampled on one draw in 64, per draw phase (shader lookup, index
  conversion, targets, pipeline, uniforms, textures, descriptors, pass, recording, vertex
  snapshots, draw/submit);
- render-thread waits for the GPU and the swapchain; game-thread waits for the render thread
  (DrawDone, CopySurface, flips);
- uploads per frame by kind (vertex, index, uniform blocks, packed uniforms, texture staging; logic
  and interpolation hold frames apart) and, on one frame in 16, the unique guest bytes those
  copies read;
- draw classes: draws with no register change since the previous draw, draws where only ALU
  constants, uniform-block or vertex-buffer words changed (continued-draw candidates), and the most
  written other registers;
- Vulkan shader translations: new programs vs. new variants, and which state words differ from the
  nearest existing variant of the same program (texture/sampler words of units the shader samples
  vs. units it does not).

`nsmbu bench` runs checked-in performance scenarios and records render traces (see `performance.md`, "How to profile").
Low-level save-state A/B orchestration remains in `tools/bench/bench_lib.py` for unit tests only.

## Resource dependencies

Precise barriers are **on by default on Android** and **opt-in on desktop**
(`NSMBU_VK_NARROW_BARRIERS=1`) until they have been checked on native Windows/Linux drivers; on MoltenVK
they measured neutral (7 interleaved pairs). The precise path records each image's last write scope
and accumulated reader stages, including attachments, feedback copies, mip chains, composition
and readbacks. Texture bindings name the shader stage that actually reads them. Barrier elision
preserves the conservative dynamic-rendering pass boundaries. Source access masks contain writes only; a write waits for all pending readers.
Same-layout reads skip dependencies once the producer is visible to that stage/access; new readers
and layout transitions still get dependencies. Layout transitions remain ordered as image writes.
Image ranges remain whole-resource. Buffer compute/readback dependencies use the same derivation;
coherent upload/cache slices are immutable until their submission fence retires, so submission
makes host writes visible without an extra GPU barrier. All resources use one queue family.

`NSMBU_VK_NARROW_BARRIERS=0` selects conservative image scopes and `=1` the precise path, on every
Vulkan platform.
Scripted synchronization and frame-equivalence checks cover the default path. Stable conservative scenes
require bit-identical precise frames. For a scene with conservative repeat variation, compare five
captures per path: candidate differences must stay within the conservative pixels, value ranges and
maximum difference counts. New regions or larger differences fail. `NSMBU_VK_VALIDATION=1`
now explicitly enables Khronos synchronization validation as well as the usual validation checks.
Fixed per-mip transfer barriers and fence/host lifetime rules remain in place. CI calibrates the
layer with `NSMBU_VK_SYNC_NEGATIVE_CONTROL=1` and `--renderer-smoke`: two intentional unordered writes
must report a synchronization hazard. Regular renderer smokes still require zero validation errors.
CI also enables the layer's shader-access heuristic and attachment LoadOp-after-StoreOp checks.

Known baseline issue: conservative MoltenVK repeats can vary at isolated foliage-edge pixels,
including depth values. MoltenVK/Metal alpha-edge behavior is suspected; the root cause remains
unconfirmed. Some broad 60 fps replay differences were traced to enabling interpolation at an
absolute boot step while state loads completed a step apart. Use `--origin-load-steps` in the
benchmark tool to align interpolation and input to the restored clock, and select full-pass
captures by post-load counter (or logic step for paused menus). Track remaining baseline variation
separately from barrier acceptance, without weakening
synchronization validation or the bounds above.

The write-only source and previous/next-use approach was inspired by GreenNaugahyde's Android
renderer (MPL-2.0); this implementation uses upstream resource tracking and copies no fork code.
See [Android device checks](android/device-checklist.md) for the pending Mali/Adreno capture.

## Optional performance diagnostics

These switches require the exact value `1` and remain disabled by default.

- `NSMBU_VK_GPU_TIMESTAMPS`: measures submission timestamp intervals using two queries per fenced submission slot. Results are collected only after the existing completion fence, without query waits. The interval sum is not GPU busy time or the frame critical path; unsupported or zero-only timestamp results cannot establish GPU cost.
- `NSMBU_VK_UNCAPPED`: bypasses guest wall-clock flip eligibility for throughput diagnostics, while retaining FIFO GPU completion ordering. Each completed swap advances guest time by one vsync and reports that gap from GX2 swap status. The audio clock stays on wall time. This is not normal gameplay FPS. The same switch is Graphics → Fast forward (not saved) and `NSMBU_UNCAPPED=1` sets it for either renderer; while it is on, the swapchains present with immediate (or mailbox) instead of the chosen mode, and Metal turns the layers' displaySyncEnabled off.
- `NSMBU_VK_READY_FLIP_WAIT`: experimental waiting for an already-eligible pending flip to complete instead of sleeping another full guest tick. It preserves the existing minimum swap interval and GPU completion guards. Literal wait-for-vblank behavior changes for an eligible pending flip; keep opt-in until matched gameplay benchmarks and correctness checks establish suitability.
- `NSMBU_VK_READY_FLIP_PARK`: experimental eligible-flip completion wait after the save-state freeze gate and before guest-core reacquisition. The optional host callback runs while the thread is marked running and its guest core remains released; it keeps the existing flip eligibility and FIFO GPU guards. This avoids a second core release/acquire roundtrip. Default sleep behavior and Metal callers remain unchanged.
- `NSMBU_VK_FEEDBACK_STATS`: counts full feedback copies and identical source versions within one draw across both shader stages. It reports physical texels rather than estimated bandwidth and does not skip copies.

The saved uphill 3x scene passed 2400 frames with both experimental switches, guarded checkpoint restore and Vulkan synchronization validation on Apple M3 Max. This establishes a tested path, not a universal synchronization proof or FPS improvement. Duplicate feedback counters were zero for that scene, so duplicate-copy elimination was rejected there. Comparisons must use the same executable, accepted checkpoint, resolution and graphics settings; a rejected state load invalidates the scene benchmark.

The PARK experiment separately passed the native build, four CTest regressions, extracted-function UBSan ordering/guard tests, and 2400-frame synchronization validation with an accepted uphill checkpoint. Its gameplay performance still requires matched non-validation comparisons.

`NSMBU_VK_GPU_PASS_TIMESTAMPS=1` adds bounded render-pass and feedback-copy scopes: at most 256 pairs per submission, recorded outside dynamic rendering and collected after the existing fence. Top-eight format/extent buckets report stage intervals, draw counts, unavailable/zero results and capacity drops. This instrumentation affects GPU scheduling: a matched uphill 3x calibration measured about 45 FPS with scopes versus 55 FPS without them. Use it to investigate work, and disable it for performance acceptance; interval sums are not exclusive GPU busy time.

`NSMBU_VK_VERTEX_WINDOW_STATS=1` measures a conservative unused-prefix opportunity without changing uploads. Its separate index scan adds CPU overhead, so its FPS is not a gameplay benchmark. The accepted uphill checkpoint showed approximately 27.4 MiB of copied-prefix requests with 8.0 MiB never referenced by the draw.

The strict opt-in `NSMBU_VK_VERTEX_COPY_WINDOW=1` instead fuses minimum and maximum index reduction, retains the original physical allocation and draw addressing, and copies/compares only the proven fetch window. Unsupported bounds/rates/layouts keep the original full-prefix path. A separate cache includes reservation, window offset/length, device and submission generation; fresh guest data is always compared. Device smoke tests passed full/window pixel equivalence with poisoned leading bytes, signed base vertex, restart, vertex-index checks, mutations and submission-ring retirement. Actual saved-scene validation and matched FPS acceptance are still required before adoption.

Small register updates (at most 16 words) use the fused Vulkan path by default. Set `NSMBU_VK_FUSE_SMALL_REGS=0` to disable it. Matched scene tests suggest roughly 2% less renderer CPU time; FPS benefit remains modest and subject to run drift.

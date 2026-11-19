# Runtime command-line flags

`runtime/src/core/env.h` is the list of game settings.
Each setting is one `ENV` line in that header.
The C++ name is the flag name without the `NSMBU_` prefix: `NSMBU_NO_AUDIO` is `env::NO_AUDIO` and the flag `--no-audio`.

The game does not read project-owned names from the process environment.
It reads command-line flags.
Host names stay on `getenv`: `HOME`, `APPDATA`, `USER`, `USERNAME`, `USERPROFILE`, `XDG_CONFIG_HOME`, `XDG_DATA_HOME`, `DISPLAY`, `WAYLAND_DISPLAY`, `PATH`, `TMPDIR`, `TEMP`, `APPIMAGE`, `APPDIR`, `CI`, `GITHUB_ACTIONS`, `ANDROID_HOME`, `ANDROID_SDK`, `ANDROID_SDK_ROOT`, `JAVA_HOME`, `ADB`, `MVK_CONFIG_*`, and `LIBDECOR_PLUGIN_DIR`.
The MoltenVK settings `MVK_CONFIG_USE_METAL_ARGUMENT_BUFFERS` and `MVK_CONFIG_PREFILL_METAL_COMMAND_BUFFERS` stay on `setenv` in the Vulkan backend.

## Precedence

Rank 2: command-line flags (`argv`), including `NAME=value` and boolean `--flag` forms.
Rank 1: `env.txt` on Android (one `--flag` or `--flag=value` per line, not `NAME=value`).
Rank 0: in-process defaults and `settings.ini` where a setting is saved.

A flag on `argv` wins over `env.txt` and over defaults.

## Naming rule

Every game setting flag is derived from its `ENV` name in `env.h`.
Drop the `NSMBU_` prefix, lowercase the rest, and join words with hyphens.
Boolean settings use a `--name` switch without a value.
Values use `--name=value` or a separate argument when documented in `--help`.
`./build/debug/nsmbu --help` prints the same setting names.
The list below is the catalog.

To add a setting, add one `ENV` line in `runtime/src/core/env.h`.
The flag name follows the rule above.
Add one line for that flag in the catalog below.

## Writes

An in-process write uses `env::set`, `env::set_new`, or `env::set_named`.
`env::set` overwrites a value already applied from a flag.
`env::set_new` fills a default only when nothing else set that name.
`env::unset` clears the in-process value.
None of those calls write the process environment.

Longer checks stay in the page that owns the behaviour ([automation.md](automation.md), [vulkan.md](vulkan.md), [framerate.md](framerate.md), and the player pages under [user/](../user/)).

The host test `env` in `tests/host/env_test.cpp` covers `.name()`, a value after `env::set`, and `env::unset`.

## Program options

These options are parsed in `runtime/src/main.cpp`.
They are not `ENV` settings.

- `--game DIR` sets the game dump directory.
- `--save DIR` sets the save data directory.
- `--install DIR` reads `DIR/install.json` and sets the game and save directories from that record.
- `--trace` logs HLE calls.
- `--renderer API` selects `metal` or `vulkan` for this start.
- `--open W-C` boots into that course pack and skips the native intro.
  The pack looks like `1-1`.
- `--warm-shaders` fills the Metal shader cache and exits.
  A Metal build accepts this option.
- `--renderer-smoke` runs the Vulkan smoke test and exits.
  A Vulkan build accepts this option.
- `-h` and `--help` print usage and every setting flag.

## Session

- `--cli` skips the crash dialog in a Debug build.
  A Release build ignores it.
- `--user-dir PATH` is the folder for settings, logs, and user files.
- `--log-file PATH` writes the log to that path.
  `--log-file=0` skips the log file.
- `--settings PATH` is the `settings.ini` path.
- `--display-settings PATH` is the `display.plist` path.
- `--mod-manager-dir PATH` is the userdata tree that holds `mods/` and `mods.json`.
  With `--no-host-input`, the mod manager stays off unless this path is set.
- `--quit-prompt=0` skips the quit prompt.
- `--language N` sets the console language to a code from 0 through 11.
  Any other value uses English.
- `--swkbd-text TEXT` is the text the software keyboard returns.
- `--renderer-runtime=metal` or `--renderer-runtime=vulkan` selects the renderer for this start.
- `--run-in-background` keeps the game running when the window is not focused.
- `--perf-hint` is on unless the value is `0`.
  It applies the host performance hint.
- `--fast-cores` is on unless the value is `0`.
  It pins work to the fast cores.
- `--heat-guard=normal` is the default.
  `relaxed` or `1` raises the pause threshold.
  `off` or `2` disables the guard.
- `--no-qos` skips the host QoS setup.
- `--no-sched` skips the guest scheduler.
- `--profile=0` turns the render profiler off.
  `--profile=1` logs `[prof]` lines.

## Display

- `--fullscreen` starts the TV window in full screen.
  `--fullscreen=0` starts it windowed.
- `--hidden-windows` keeps the windows off screen.
  Frame dumps still run.
- `--display-hz N` overrides the detected refresh rate.
- `--sim-screen WxH` sets a simulated screen size, for example `1920x1080`.
- `--view-button` shows the view button.
  On Android it is on unless the value is `0`.
  On other hosts it is on only when set.
- `--touch-controls=0` hides the on-screen touch pad.
- `--no-gamepad` does not open the GamePad window.
- `--drc-auto=THRESH:HOLD` sets the auto GamePad view thresholds.
- `--drc-auto-log` logs auto GamePad view switches.
- `--drc-mode=NAME` is `window`, `pip`, `auto`, `off`, or `gamepad`.
- `--drc-pause` shows the GamePad view while the game is paused.
  On Android it is on unless the value is `0`.
- `--drc-pip=CORNER:SIZE:OPACITY` sets the picture-in-picture box.
  The corner is `tl`, `tr`, `bl`, or `br`.
- `--aspect=MODE` is `16:9`, `window`, `16:10`, `21:9`, `32:9`, or a custom ratio.
- `--aspect-stretch` stretches the picture to the window.
- `--aspect-at=FRAME:RATIO` changes the aspect at that TV frame.
  Separate extra pairs with commas.
- `--aspect-log` logs aspect changes.
- `--aspect-trace-frame=N` traces aspect layout on that TV frame.
- `--res-scale=N` sets the internal resolution scale.
- `--res-scale-at=FRAME:SCALE` changes that scale at a renderer frame.
  Separate extra pairs with commas.
- `--shadow-scale=N` sets the shadow-map scale.
  Unset leaves the scale at 0.
- `--scale-filter=NAME` is `smooth`, `sharp`, or `integer`.
- `--aniso` enables anisotropic filtering when the value is not `0`.
- `--fxaa` enables FXAA when the value is not `0`.
- `--uncapped` starts in fast forward.
  The game draws one step per picture and does not limit the frame rate.
- `--collision-viewer` draws the collision wireframe.
- `--hot-reload-courses` reloads course files while the game runs.
- `--hot-reload-poll-ms=N` is the reload poll interval in milliseconds.
  The default is 250.
  The runtime clamps the value from 50 to 5000.

## Input

- `--no-host-input` ignores the host keyboard, mouse, and controllers.
- `--no-controllers` does not open gamepads.
- `--android-keyboard` maps the Android keyboard as a pad.
- `--pro-controller` uses the Pro Controller layout.
- `--controls-hover=VALUE` drives the controls UI hover script.
- `--controls-selftest` runs the controls UI self-test.
- `--controls-snapshot=PATH` writes a controls UI image to that path.
- `--controls-snapshot-at=SECONDS` waits that long before the snapshot.
  The default is 3.
- `--show-controls` shows the controls UI at start.
- `--press=RANGES` holds VPAD bits for inclusive TV-frame ranges.
  Each item is `from-to:bits`.
- `--keys=RANGES` holds host keys for inclusive TV-frame ranges.
  Each item is `from-to:key`.
- `--stick=RANGES` sets the left stick for inclusive TV-frame ranges.
  Each item is `from-to:x:y`.
- `--rstick=RANGES` sets the right stick in the same form as `--stick`.
- `--hold-run` is accepted and the runtime does not read it.
- `--log-buttons` logs button changes.
- `--record-input=PATH` writes a pad log keyed by guest logic step.
- `--record-course=NAME` puts that course name in the pad log header.
- `--replay-input=PATH` plays a pad log and ignores the live pad.
- `--replay-exit=none` keeps live input when that log ends.
  Any other value quits when the log ends.
- `--replay-trace` logs replay clock arming and early step positions.
- `--agent-sock=PATH` opens the agent control socket.
  A Release build ignores it.
- `--gyro=SOURCE` is `off`, `controller`, `cemuhook`, or `mouse`.
- `--gyro-log` logs gyro samples.
- `--rumble` is on unless the value is `0`.
- `--rumble-log` logs rumble patterns.
- `--log-rumble` logs rumble patterns.
- `--phone-rumble=MODE` is `off`, `touch` or `1`, or `fallback` or `2`.
- `--pad-trace=PATH` writes a pad trace file.

## Audio

- `--no-audio` mutes this start and does not open an output device.
- `--audio-dump=PATH` writes the mixed output as a WAV file.
- `--audio-output=SOURCE` is `auto`, `tv`, or `gamepad`.
- `--audio-volume=N` sets the output volume for this start.
- `--ax-stats` logs AX voice statistics.
- `--sound-trace=PATH` writes a sound trace for 60 seconds.
- `--voice-rate` logs the voice sample rate.
- `--se-stats` is accepted and the runtime does not read it.
- `--se-trace` is accepted and the runtime does not read it.

## Interpolation

Course picture checks are in [Frame interpolation](#frame-interpolation) below.

- `--interp=0` forces interpolation off for this start.
  `--interp=1` forces it on.
  When the flag is omitted, in-between frames above 60 fps are on.
- `--interp-at-step=N` logs interpolation at that logic step.
- `--interp-cam-trace=N` logs that many camera pose lines.
- `--interp-grass-trace=N` logs that many grass sample lines.
- `--interp-cut=N` is the camera cut distance.
  The default is 800.
- `--interp-fps=N` is the present cap.
  Listed rates are 30, 60, 120, 144, 165, and 240.
  Any other value from 60 to 1000 is a custom cap.
  A value below 60 other than 30 becomes 60.
- `--interp-models` is accepted and the runtime does not read it.
- `--interp-model-cut` is accepted and the runtime does not read it.
- `--interp-model-trace=N` logs that many moving course pictures.
- `--interp-model-trace-joints` is accepted and the runtime does not read it.
- `--interp-no-repeat` does not repeat a held pose on an in-between frame.
- `--interp-paced` paces in-between frames to the display.
- `--interp-pass-stats` logs interpolation pass statistics.
- `--interp-run` is accepted and the runtime does not read it.
- `--trace-input-phase=N` logs that many input-phase lines.

## Saves

- `--state-debug` logs save-state thread debug.
- `--state-dir=PATH` is the save-state folder.
- `--state-dump=N` writes a save-state debug dump when the value is not `0`.
- `--state-load-at=FRAME:SLOT` loads that slot at a TV frame.
  Separate extra pairs with commas.
- `--state-resident-off` keeps save-state memory off the resident set.
- `--state-save-at=FRAME:SLOT` saves that slot at a TV frame.
  Separate extra pairs with commas.
- `--state-stage-addr=ADDR` sets the stage address used by save states.
- `--full-save-states=1` uses full save states for this start.
  `--full-save-states=0` uses portable states.
- `--portable-load=PATH` loads that portable state file.
- `--portable-load-at=FRAME:SLOT` loads a portable state at a TV frame.
- `--portable-save-at=FRAME:SLOT` saves a portable state at a TV frame.
- `--crash-recovery` turns crash recovery on when the value is not `0`.
- `--crash-recovery-interval=SECONDS` sets how often crash recovery saves.
  The default is 120.
  A value below 10 becomes 10.
- `--replay=N` selects the crash-recovery replay mode.
- `--replay-at=N` sets the frame for that replay.

## Capture

- `--screenshot-dir=PATH` is the screenshot folder.
- `--screenshot-gamepad` includes the GamePad picture when the value is not `0`.
- `--capture=FRAME` writes a renderer frame capture.
- `--capture-path=PATH` is the PNG path for that capture.
- `--present-capture-frame=FRAME` captures the presented image at that frame.
- `--present-capture-path=PATH` is the PNG path for the present capture.
- `--dump-frames=LIST` writes `frame_<n>.png` for those TV frames.
  The list is comma-separated, for example `800,1200`.
- `--dump-title=N` writes that many title pictures.
- `--dump-load-frames=LIST` dumps TV frames after a save-state load.
- `--dump-present` also writes the presented image next to a frame dump.
- `--exit-at-frame=N` quits in order at that TV frame.

## Shaders and GX2

- `--shader-cache=PATH` selects the shader cache file.
  `--shader-cache=0` disables that cache.
- `--strict-mul` keeps strict shader multiplies unless the value is `0`.
- `--sync-gpu` waits for the GPU before the next guest step.
- `--sync-shaders` compiles shaders on the render thread.
- `--sync-stats` logs GPU sync statistics.
- `--copydbg` logs surface copies.
- `--no-rt-mips` skips render-target mipmaps.
- `--headstart=SPEC` selects the Metal shader head-start set.
- `--shader-warm=STOPFILE` fills the shader cache until that file appears, then exits.
  `--shader-warm=0` does not warm.
- `--msl-nonce=TEXT` adds that nonce to Metal shader keys.
- `--bg-compiles=N` is the maximum number of background compiles in flight.
  The default is 8.
- `--compile-wait-ms=N` is the per-frame compile wait in milliseconds.
  The default is 25.
- `--no-chunk` disables chunked texture uploads.
- `--no-uv-snap` disables UV snapping.
- `--snapshot` keeps Metal uniform snapshots.
- `--vb-snapshot=N` is the vertex-buffer snapshot byte limit.
  The default is 262144.
- `--dump-shaders` writes generated shaders.
- `--dump-draws=SPEC` dumps draws that match that spec.
- `--dump-ps=ID` dumps that pixel shader.
- `--log-frame=N` logs that frame.
- `--log-invalidate` logs texture invalidation.
- `--log-rescale` logs surface rescales.
- `--log-texcheck` logs textures rechecked after a write.
- `--log-title` logs TV title changes.
- `--log-vsync` logs a vsync line every 60 calls.
- `--log-slow-swap=MS` logs swaps slower than that many milliseconds.
- `--log-longwait` logs long thread waits.
- `--gx2-delay-copy=N` delays GX2 copies by that count.
- `--no-render-thread` runs draws on the guest thread.
- `--vsync-precise` spins for the last part of the vsync wait.
  Desktop builds do this unless the value is `0`.
- `--vsync-spin-us=N` sets that spin window in microseconds.
- `--decode-check=1` runs both texture decoders and stops on a difference.
- `--upload-profile` logs texture upload time.

## Vulkan

These flags apply when the Vulkan renderer is active.
The fifteen CPU paths that default on are set in `default_vulkan_cpu_paths`.
`--name=0` turns that path off.

- `--vk-attachment-view-usage` sets attachment view usage.
  `1` forces it on and `0` forces it off.
- `--vk-async-present` queues presentation without a drain.
  It is on unless the value is `0`.
- `--vk-async-shaders` compiles Vulkan shaders off the render thread.
- `--vk-bc-timing` logs BC decode time.
- `--vk-bc-verify` checks BC decode output.
- `--vk-buffer-cache` turns the guest buffer cache on with `1` and off with `0`.
- `--vk-buffer-cache-hints=0` ignores DCFlush and GX2Invalidate hints.
- `--vk-buffer-cache-log=1` logs buffer-cache events.
- `--vk-buffer-cache-mb=N` sets the buffer-cache size in megabytes.
- `--vk-buffer-cache-verify=1` compares every cache hit with a fresh CPU copy.
- `--vk-cpu-only-stats=1` logs render-thread CPU time without GPU timestamps.
- `--vk-depth-copy=draw` forces a drawn depth copy.
  `--vk-depth-copy=none` skips the depth copy.
- `--vk-descriptor-ranks` precomputes descriptor binding ranks.
  It is on unless the value is `0`.
- `--vk-draw-batch=N` sets the mid-frame draw batch size.
  `0` disables batching.
- `--vk-draw-batch-cap=N` sets the maximum mid-frame submissions.
  The accepted values are `1`, `2`, and `3`.
- `--vk-feedback-alloc-log` logs feedback-image allocation.
- `--vk-feedback-stats` counts feedback copies.
- `--vk-fetch-memo` reuses the last fetch-program lookup in a frame.
  It is on unless the value is `0`.
- `--vk-force-api=1.2` or `--vk-force-api=1.1` treats every GPU as that API version.
- `--vk-force-bc-decode` decodes BC textures on the CPU.
- `--vk-force-init-fail` forces Vulkan init to fail when the value is not `0`.
- `--vk-force-renderpass=1` forces classic render passes.
  `--vk-force-renderpass=0` requires dynamic rendering.
- `--vk-fuse-small-regs` fuses small register updates.
- `--vk-gpu=NAME` selects that GPU for this start.
- `--vk-gpu-pass-timestamps` records render-pass and feedback-copy GPU intervals.
- `--vk-gpu-timestamps` records GPU submission intervals.
- `--vk-hide-extensions=LIST` hides those comma-separated device extensions.
- `--vk-lazy-draw-done` queues GX2DrawDone instead of waiting.
  It is on unless the value is `0`.
- `--vk-narrow-barriers=1` uses precise image barrier scopes.
  `--vk-narrow-barriers=0` uses conservative scopes.
- `--vk-pipeline-cache=PATH` selects that pipeline cache file.
  `--vk-pipeline-cache=0` disables persistence.
- `--vk-pipeline-lookaside` keeps a short exact pipeline-key cache.
  It is on unless the value is `0`.
- `--vk-present-mode=NAME` is `fifo`, `mailbox`, or `immediate`.
- `--vk-ready-flip-park` waits for an eligible flip before the guest core is taken again.
- `--vk-ready-flip-wait` waits for an eligible pending flip instead of a full guest tick.
- `--vk-reuse-feedback-images` keeps compatible feedback-copy images.
  It is on unless the value is `0`.
- `--vk-reuse-uniform-snapshots` reuses uniform slices after a full byte compare.
  It is on unless the value is `0`.
- `--vk-reuse-vertex-snapshots` reuses vertex slices after a full byte compare.
  It is on unless the value is `0`.
- `--vk-sampler-memo` reuses recent sampler handles.
  It is on unless the value is `0`.
- `--vk-shader-address-memo` ignores texture address words that do not change the shader key.
  It is on unless the value is `0`.
- `--vk-shader-cache=PATH` selects an isolated shader directory.
  `--vk-shader-cache=0` disables disk storage.
- `--vk-shader-key-dirty` skips shader rebuilds for register bits that are not in the key.
  It is on unless the value is `0`.
- `--vk-shader-key-verify=1` also computes the previous wider shader key on each lookup.
- `--vk-shader-state-memo` keeps a few exact shader linkage keys.
  It is on unless the value is `0`.
- `--vk-skip-redundant-binds` skips an identical descriptor bind.
  It is on unless the value is `0`.
- `--vk-skip-vertex-binds` skips an identical vertex-buffer bind.
  It is on unless the value is `0`.
- `--vk-sparse-hash-entries=N` sets the sparse-hash sample store size.
- `--vk-sparse-hash-memo` reuses a sparse texture hash after a fresh sample compare.
  It is on unless the value is `0`.
- `--vk-specialize-indices` picks one index reader per draw.
  It is on unless the value is `0`.
- `--vk-stats` logs Vulkan frame and upload statistics.
- `--vk-sync-negative-control` adds the intentional unordered writes in `--renderer-smoke`.
- `--vk-uncapped` skips the guest flip wait for a throughput check.
  `--uncapped` sets the same fast-forward mode for either renderer.
- `--vk-upload-reads=MODE` is `auto`, `shadow`, or `direct`.
- `--vk-validation` enables Vulkan validation layers.
- `--vk-vertex-copy-window` copies vertex data through a bounded window.
- `--vk-vertex-history-probe` logs vertex-history reuse candidates.
- `--vk-vertex-history-reuse` keeps a second vertex snapshot key.
  It requires `--vk-reuse-vertex-snapshots`.
- `--vk-vertex-window-stats` logs vertex-window statistics.

## Traces

- `--trace-funcs` logs traced PPC function calls.
- `--trace-msg` logs scheduler messages.
- `--trace-ps=SPEC` traces that pixel shader.
- `--trace-swap=N` logs that many swap calls.
- `--trace-voice=N` logs that many AX voice events.
- `--trace-vpad=N` logs that many VPAD calls.
- `--watch=SPEC` arms a PPC watch.
- `--watch-dump=SPEC` dumps memory when a watch hits.
- `--watch-expr=EXPR` is the watch expression.
- `--watch-if=COND` logs a watch only when that condition is true.
- `--watch-limit=N` stops after that many watch hits.
- `--watch-mem=SPEC` starts a memory watch thread.
- `--watch-r3=HEX` traces when `r3` equals that hex value.
- `--nan-probe` traces when a guest value is NaN.
- `--thread-dump=SPEC` dumps guest threads.
- `--tick-stats` logs scheduler tick statistics.
- `--sched-stats` logs a scheduler report.
  `--sched-stats=2` also logs timed statistics.

## In-game Settings

- `--ingame-settings` shows Settings in the course pause menu and on the world-map start menu.
  The button stays off when this flag is unset.

## Tests

`nsmbu scenario` and `nsmbu compare-runs` use these flags.
See [Scripted compare runs](#scripted-compare-runs).

- `--test-align-scene=NAME` names the scene whose first logic step is the align point.
- `--test-capture-load-counter=N` captures a full-pass swap at the restored game counter plus N.
- `--test-capture-load-step=N` captures a full-pass swap N logic steps after a load.
- `--test-cemu-trace` logs Cemu content pack reads.
- `--test-close-tv-at=FRAME` closes the TV window at that frame.
- `--test-content-trace=1` logs content-mod file reads.
- `--test-crash-at=N` crashes at that count for crash-recovery checks.
- `--test-debug` logs the test clock and TV frame.
- `--test-drc-key=FRAMES` presses the GamePad view key on those frames.
- `--test-drc-mode=FRAME:NAME` switches the GamePad view at that frame.
  Separate extra pairs with commas.
- `--test-end=SECONDS` ends a timed input script at that time.
- `--test-fail-compiles=N` fails that many shader compiles.
- `--test-fixed-seed` counts `--press`, `--keys`, `--stick`, `--rstick`, and `--dump-frames` from the align TV frame.
  That frame counts as frame 1800.
  It does not write guest memory.
- `--test-fullscreen-key=FRAMES` toggles full screen on those frames.
- `--test-gyro=RANGES` turns the virtual GamePad by yaw and pitch.
  Each item is `from-to:yaw:pitch`.
- `--test-host-crash` crashes on purpose after boot.
  A Debug build exits non-zero and prints the crash log path.
- `--test-load=FRAME:SLOT` loads a full save state at that TV frame.
- `--test-load-story=PATH` loads that story save for an overlay check.
- `--test-mode=MODE@SECONDS` switches input mode at that time.
- `--test-modes=LIST` is a sequence of those mode switches.
- `--test-mod-enable=NAME` enables that mod when host input is off.
- `--test-origin=STEP` starts a timed input script at that logic step.
- `--test-overlay=NAME` opens that overlay page for a test.
- `--test-pad=SPEC` scripts overlay pad input.
- `--test-post-keys=LIST` posts host keys while `--no-host-input` is set.
- `--test-present-mode=NAME` switches the present mode during an overlay test.
- `--test-press=RANGES` holds buttons after `--test-origin`.
- `--test-quit-answer=ANSWER` is `quit`, `cancel`, or `save`.
- `--test-renderer-switch=FRAME:API` restarts into `metal` or `vulkan` at that frame.
- `--test-rstick=RANGES` sets the right stick after `--test-origin`.
- `--test-save=FRAME:SLOT` saves a full state at that TV frame.
- `--test-screenshot=FRAMES` takes a screenshot on those frames.
- `--test-status-trace=PATH` appends one line per logic step.
  The line has the step, scene, map, course, pack, and player x, y, and z.
- `--test-stick=RANGES` sets the left stick after `--test-origin`.
- `--test-touch=SPEC` scripts a touch on the GamePad view.

## Frame interpolation

Course models blend between the last two logic poses when in-between frames are on.
A picture's pose is the uniform block copied when its display list runs.
Snapshot that block at submit, as `vendor/ZeldaWWHDRecomp/runtime/src/gx2/submitted_uniforms.h` does.
`ModelG3d::calc` and `calcGPU` in `vendor/decomp/include/graphics/ModelG3d.h` are the order of the skeleton uniform block and the render-thread copy.
`interp_picture_test` checks that a later write to the live block leaves the copied pose unchanged.
A pose or camera change is done when that test passes and two consecutive TV frames are dumped with `--dump-frames`.
`--interp-model-trace` is how many moving course pictures to log.
Each `[interp] pose` line has the hold flag, the blend weight, and the previous, current, and drawn bone translations.
`nsmbu test pose-direction-120` walks in `1-1` at 120 fps and checks that drawn point stays on the segment between the two logic poses.
Each player's head is a one-bone model with no skeleton uniform, and that test also checks the uploaded shape matrix.
The model view-projection is rebuilt from that same halfway camera.
Course tiles read the live ortho instead, so that split and the tile-only sample are in [framerate.md](framerate.md).
A hold does not run `calcGPU`, so the uniform would otherwise keep the full camera step.
The dump steps are in `docs/developer/automation.md`.
`--interp-fps` is a present cap.
30, 60, 120, 144, 165, and 240 are the listed rates.
Any other value from 60 to 1000 is a custom cap.
A value below 60 other than the 30 preset is 60.
Guest logic stays at 60 steps/s.
The 30 fps preset runs two logic steps per picture.
`--uncapped` starts in Fast forward: one game step per picture and no frame limit.
`--interp=0` forces interpolation off for that start.
`--interp=1` forces it on.
When `--interp` is omitted, in-between frames above 60 fps are on.
Off keeps one picture per game step and paces those pictures at 60 fps.
The 30 fps preset still runs two logic steps per picture.
The other interpolation flags are in the catalog above.

## Scripted compare runs

These flags are used by `nsmbu scenario` and `nsmbu compare-runs` (see `docs/developer/automation.md`).

- `--test-status-trace=path` appends one line per logic step: step, scene, map, course, pack, and player x, y, z.
- `--test-align-scene=name` names the scene (`Title`, `Map`, `CourseTask`, ...) whose first logic step is the align point.
  The runtime logs `[test] align at logic step N scene=NAME` there and records the TV frame.
- `--test-fixed-seed=1` counts `--press`, `--keys`, `--stick`, `--rstick`, and `--dump-frames` from that TV frame, which counts as frame 1800.
  It writes no guest memory.
  `--exit-at-frame` stays on raw TV frames.
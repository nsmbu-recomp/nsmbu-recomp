# Android device checks (rhemfur)

Each check below is pending until a device result is recorded.
Record the device, Android version, GPU, driver and APK version with each result.

## On-device setup

- Install the release APK on an arm64 phone with no earlier NSMBU install.
  The app icon opens setup and **Play** stays disabled.
- After a game is ready, tick **Skip the installer for future runs**, leave the app, and open it from the icon.
  The game starts.
  **Settings > Android** is the last tab and turns that off, so the icon opens the installer again.
- Set up from an extracted game folder, a Cemu `.wua` archive, and a `.wud`/`.wux` image with a Cemu `keys.txt`.
  Each setup reaches **Play**, and the game boots to the title screen.
- Set up a `.wud`/`.wux` image with separate disc and common key files, and once with a wrong key.
  The wrong key is named before extraction starts.
- Add the 1.3.0 update folder to an image job.
  Setup completes, and the game boots with the update applied.
- Pause setup during compile, leave the app, and resume.
  Completed objects are reused, and the progress text says so.
- Reboot the device during compile.
  Setup offers to continue, and the URI grant still opens the chosen input.
- Heat the device or run it below the low-battery limit during setup.
  Setup pauses by itself and continues when the device is ready.
- Choose a different game after a successful setup.
  The previous build stays playable until the new one is activated.
- Install the next release over the current one.
  Saves and settings stay, and setup rebuilds only when its fingerprint changed.
- Record the setup time, peak free-space use and peak memory for a full dump.

## Phone runtime

- Back opens and closes Settings, and a controller's Home button opens them too.
- Full screen covers the cutout area in both landscape directions.
- **Share log** in setup shares `nsmbu.log` and `nsmbu-previous.log` after a run.
  Before the game has run, the same button shares the setup status and the latest extract, translation, compile, and link logs.
- Kill the game from the system (low memory or `adb shell am kill`).
  The next start offers a report with the Android exit reason.
- Export a save from the launcher shortcut while the game runs.
  The stop does not make a crash report.
- The system Files app shows the app's root, refuses writes in `game`, and hides key files.
- On a fan-cooled handheld, the log says `heat guard: handheld (fan)`.
  On a phone, it says `heat guard: phone`.
- At 120 fps, heat the device until the log says `60 fps paused: thermal headroom`, then let it cool until it resumes.

## Touch controls

- Play World 1-1 with touch controls only, including jumps, runs and the D-pad.
- Switch the face-button layout in Settings and check that the drawn A still jumps.
- Connect a Bluetooth controller.
  The touch buttons and the view button hide, and a corner touch reveals them for a few seconds.
- Hold the show/hide button, move and resize a control, rotate the device, and restart the app.
  The layout stays.
- With **Phone vibration** on **Touch controls**, take damage while playing by touch.
  The phone vibrates, and controller input stops it.

## Release APK

- `apksigner verify --print-certs` reports one signer with the release certificate.
- The APK has no `lib/*/libmain.so`, and `tools/release/guard.py` passes on it.
- The `android-x86_64` APK installs and completes setup on an x86_64 emulator or Chromebook.

## Vulkan barrier capture

Android performance gain is **pending device results**. Emulator display checks cannot establish
Mali/Adreno GPU cost. Use the same native library, game, copied save/checkpoint, scripted input,
resolution, frame mode and driver for both variants. Record device/OS/GPU/driver and temperatures.

The API 36.1 SwiftShader emulator passes swapchain presentation and PNG readback in both modes.
Its full renderer smoke fails `volume render wrote outside its slice` in both modes; keep this
baseline limitation separate and do not report the full emulator smoke as passing.

1. In a private test APK, use SDL's manifest environment metadata under `<application>`:
   `<meta-data android:name="SDL_ENV.NSMBU_VK_NARROW_BARRIERS" android:value="0" />` for the old
   path, `1` for precise barriers (also the default). Reinstall/restart between variants; keep the native library identical.
   Also set `SDL_ENV.NSMBU_PROFILE=1`, `SDL_ENV.NSMBU_NO_AUDIO=1` and the existing `NSMBU_TEST_*`
   scripted input/state-load switches. Use private shader caches, copied saves and a verified state
   load. Do not distribute an APK containing game code.
2. First enable Khronos synchronization validation (`NSMBU_VK_VALIDATION=1`, with the Android
   validation layer packaged/enabled) and capture the six desktop scenes at interpolated
   60 fps. For 60 fps,
   start with `NSMBU_INTERP=0` and use `NSMBU_TEST_MODE=1@0` so interpolation switches at the origin;
   avoid an absolute startup-step mode switch. Use
   `NSMBU_TEST_CAPTURE_LOAD_COUNTER=n` for a queued full-pass TV/GamePad/present capture at the
   restored game counter plus n. Paused menus freeze that counter: use
   `NSMBU_TEST_CAPTURE_LOAD_STEP=n` there. Keep offsets identical between modes. Require zero sync
   hazards/errors and byte-identical decoded frames for stable baseline scenes. Where the old path
   varies, use five captures per path: same pixels, no larger value ranges or difference counts.
   Keep validation and frame dumping disabled during performance captures.
3. Warm both variants. Let the device cool between runs, keep power/refresh/brightness fixed and
   close other GPU applications. Run five interleaved old/new pairs of the same 60-second gameplay
   route; alternate pair order. Extend by five pairs only if frame time is within ±2% and IQRs
   overlap. Keep startup, checkpoint restore and shader compilation out of the measured window.
4. Save the profiler log (`adb logcat -v threadtime`) for each run. Report median and Q1–Q3 across
   run medians for `ms/frame` and `render thread CPU ... ms/frame`. Reject missing state loads,
   changed scene/input, thermal throttling and incomplete profiler windows, retaining the reason.
5. Capture each measured window with Perfetto or Android GPU Inspector System Profiler.
   A minimal Perfetto text config (save as `barriers.pbtxt`) is:

   ```text
   buffers { size_kb: 65536 fill_policy: RING_BUFFER }
   duration_ms: 60000
   data_sources { config { name: "linux.ftrace" ftrace_config {
     ftrace_events: "sched/sched_switch"
     ftrace_events: "sched/sched_wakeup"
     ftrace_events: "power/cpu_frequency"
     atrace_categories: "gfx"
     atrace_apps: "org.nsmbrecomp.nsmbu"
   } } }
   data_sources { config { name: "android.surfaceflinger.frametimeline" } }
   data_sources { config { name: "gpu.renderstages" } }
   ```

   ```sh
   adb push barriers.pbtxt /data/local/tmp/barriers.pbtxt
   adb shell perfetto --txt -c /data/local/tmp/barriers.pbtxt -o /data/misc/perfetto-traces/barriers.perfetto-trace
   adb pull /data/misc/perfetto-traces/barriers.perfetto-trace
   ```

   Start the trace at the same script checkpoint in every run and rename it with variant/pair.
   Inspect SurfaceFlinger frame intervals (when available for the app's SurfaceView) and
   scheduled CPU time for the `GX2 render` thread. Use the runtime profiler for frame time if
   FrameTimeline does not expose the SDL surface.
   GPU render stages require driver support: report unavailable GPU time as unavailable. If AGI
   exposes GPU duration/counters, export those with the trace; do not substitute CPU GPU-wait time
   or emulator timing. Vulkan timestamp intervals (`NSMBU_VK_GPU_TIMESTAMPS=1`) are submission
   intervals, not exclusive GPU busy time; calibrate their overhead before using them in a comparison.

References: [FrameTimeline](https://perfetto.dev/docs/data-sources/frametimeline),
[AGI GPU render stages](https://developer.android.com/agi/sys-trace/gpu-render-stages).

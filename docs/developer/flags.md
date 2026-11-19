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
The canonical list is `./build/debug/nsmbu --help` (or the release binary's `--help`).

To add a setting, add one `ENV` line in `runtime/src/core/env.h`.
The flag name follows the rule above.
Document what it does in the feature page that owns the behaviour.

## Writes

An in-process write uses `env::set`, `env::set_new`, or `env::set_named`.
`env::set` overwrites a value already applied from a flag.
`env::set_new` fills a default only when nothing else set that name.
`env::unset` clears the in-process value.
None of those calls write the process environment.

What a flag does stays in the doc that already describes it ([automation.md](automation.md), [vulkan.md](vulkan.md), and the player pages under [user/](../user/)).
This file is the flag contract in code, not a catalog of gameplay effects.

The host test `env` in `tests/host/env_test.cpp` covers `.name()`, a value after `env::set`, and `env::unset`.

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
Other interpolation flags (`--interp-paced`, and the rest in `env.h`) are listed only in `--help` unless a feature doc describes them.

## Scripted compare runs

These flags are used by `nsmbu scenario` and `nsmbu compare-runs` (see `docs/developer/automation.md`).

- `--test-status-trace=path` appends one line per logic step: step, scene, map, course, pack, and player x, y, z.
- `--test-align-scene=name` names the scene (`Title`, `Map`, `CourseTask`, ...) whose first logic step is the align point.
  The runtime logs `[test] align at logic step N scene=NAME` there and records the TV frame.
- `--test-fixed-seed=1` counts `--press`, `--keys`, `--stick`, `--rstick`, and `--dump-frames` from that TV frame, which counts as frame 1800.
  It writes no guest memory.
  `--exit-at-frame` stays on raw TV frames.
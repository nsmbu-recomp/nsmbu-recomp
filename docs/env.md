# Runtime environment variables

`runtime/src/core/env.h` is the list of `NSMBU_*` names the runtime reads.
Each name is an `inline` `env::Var` in that header.
`runtime/src/core/env.cpp` implements `env::unset`.

A read is a call on the object.
`env::NO_HOST_INPUT()` returns the same pointer `getenv` returns.
`env::NO_HOST_INPUT.name()` is the string `"NSMBU_NO_HOST_INPUT"` when a log or a `setenv` needs it.
`integer_or`, `real_or`, `u64_or`, and `ul_or` return the parsed number, or the fallback when the variable is unset.
`enabled` is true when the value is set and `atoi` is not `0`.
`present` is true when the variable is set.

The C++ name is the environment name without `NSMBU_`.
`NSMBU_VK_DRAW_BATCH` is `env::VK_DRAW_BATCH`.

To add a variable, add one `ENV` line in `runtime/src/core/env.h`.

A write stays `setenv` at the call site (`_putenv_s` on Windows).
An `NSMBU_*` write passes `.name()`, as in `setenv(env::STATE_DIR.name(), path, 0)`.
`env::unset(env::RENDERER_RUNTIME)` clears a catalog variable on every platform.

Host names stay on `getenv`: `HOME`, `APPDATA`, `USER`, `USERNAME`, `USERPROFILE`, and `XDG_CONFIG_HOME`.
The MoltenVK settings `MVK_CONFIG_USE_METAL_ARGUMENT_BUFFERS` and `MVK_CONFIG_PREFILL_METAL_COMMAND_BUFFERS` stay on `setenv` in the Vulkan backend.
Those names are not in `runtime/src/core/env.h`.

On Android, `env.txt` lines are file data applied with `setenv`.
They are not a second list of variable names.

What a variable does stays in the feature doc that already describes it (`docs/automation.md`, `docs/vulkan.md`, and the other feature pages).
This file is how a variable is defined in code, not a catalog of gameplay effects.

The host test `env` in `runtime/test/env_test.cpp` covers `.name()`, a read after `setenv`, and `env::unset`.

## Frame interpolation

Course models blend between the last two logic poses when in-between frames are on.
A picture's pose is the uniform block copied when its display list runs.
Snapshot that block at submit, as `vendor/ZeldaWWHDRecomp/runtime/src/gx2/submitted_uniforms.h` does.
`vendor/decomp/docs/frame-loop.md` is the order of `ModelG3d::calc`, `calcGPU`, and the render-thread copy.
`interp_picture_test` checks that a later write to the live block leaves the copied pose unchanged.
A pose or camera change is done when that test passes and two consecutive TV frames are dumped with `NSMBU_DUMP_FRAMES`.
The dump steps are in `docs/automation.md`.
`NSMBU_INTERP_FPS` is a present cap.
30, 60, 120, 144, 165, and 240 are the listed rates.
Any other value from 60 to 1000 is a custom cap.
A value below 60 other than the 30 preset is 60.
Guest logic stays at 60 steps/s.
The 30 fps preset runs two logic steps per picture.
`NSMBU_UNCAPPED=1` starts in Fast forward: one game step per picture and no frame limit.
`NSMBU_INTERP=0` forces interpolation off for that start and takes precedence over Debug.
`NSMBU_INTERP=1` forces it on.
When `NSMBU_INTERP` is unset, in-between frames above 60 fps are on.
Debug can turn them off.
That choice is saved as `frameInterpolation`.
Off keeps one picture per game step and paces those pictures at 60 fps.
The 30 fps preset still runs two logic steps per picture.
Other interpolation variables (`NSMBU_INTERP_PACED`, and the rest in `env.h`) are listed only in `env.h` unless a feature doc describes them.

## Scripted compare runs

These variables are used by `tools/gametest/run_scenario.sh` and `tools/gametest/compare.py` (see `docs/automation.md`).

- `NSMBU_TEST_STATUS_TRACE=path` appends one line per logic step: step, scene, map, course, pack, and player x, y, z.
- `NSMBU_TEST_ALIGN_SCENE=name` names the scene (`Title`, `Map`, `CourseTask`, ...) whose first logic step is the align point.
  The runtime logs `[test] align at logic step N scene=NAME` there and records the TV frame.
- `NSMBU_TEST_FIXED_SEED=1` counts `NSMBU_PRESS`, `NSMBU_KEYS`, `NSMBU_STICK`, `NSMBU_RSTICK`, and `NSMBU_DUMP_FRAMES` from that TV frame, which counts as frame 1800.
  It writes no guest memory.
  `NSMBU_EXIT_AT_FRAME` stays on raw TV frames.

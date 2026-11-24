# Findings

## Report

- On first play in normal mode, the opening cinematic plays fine.
- The second cinematic stalls for a very long time on a black screen.
- The reporter names the Koopalings split-world cinematic as the stuck scene.
- They waited about an hour with no progress after the first cinematic.
- Platform is Android 0.10 or earlier on a Redmi 14C with Mali-G52.
- Game is USA 1.3.0.
- Logs say only "Black screen"; no in-app diagnostics were attached.

## Issue metadata

- GitHub issue 83 is open with label `bug`.
- JimmyMota opened it on 2026-11-22.
- `.tmp/issues/83.md` has no maintainer or reporter comments.

## Changelog

- `CHANGELOG.md` does not cite issue 83.
- Version 0.9.0 added **Async shader compile**, off by default.
- That release says a new shader keeps the last picture until the shader is ready.
- It also says the shader cache loads at startup so first frames skip the disk read.
- Setup shader precompile exists on desktop; Android setup has no shader stage per `docs/developer/android/implementation.md`.
- Versions 0.10.0 and 0.10.1 list Android UI and setup fixes only; they do not mention opening cinematics or shader stalls.

## Code

- `runtime/src/gfx/vulkan/settings.cpp` defines `async_shaders_enabled()`; async compile defaults off unless the env override is set.
- `runtime/src/overlay/overlay.cpp` documents that the default sync path can freeze the picture during compile; async leaves draws out until ready.
- `runtime/src/gfx/vulkan/shaders.cpp` compiles on the render thread when async is off, or queues work and calls `present_while_shader_pending()` when async is on.
- `runtime/src/gfx/vulkan/backend.cpp` implements `present_while_shader_pending()` to repeat the last presented frame during pending compiles.
- `runtime/src/gfx/vulkan/shader_job.cpp` runs background SPIR-V compile jobs when async is enabled.

## Related reports

- Issues 57 and 75 report brief Android Mali hitches during play after 0.9.0; issue 83 describes a much longer black-screen stall at the second cinematic.

## Assessment

- The timing and black screen fit a heavy first-play shader compile burst at a new scene, but the repo does not prove an hour-long hang or name this cinematic explicitly.
- No tracked fix or changelog entry targets issue 83 yet.
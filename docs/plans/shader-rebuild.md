# Rebuild shader cache (setup)

## Goal

Add a player control to wipe and warm the shader cache through setup Precompile, without porting Zelda WW HD's full shader-prebuild generations.

The performance overlay work from the old combined plan is shipped (`perf_metrics`, overlay UI, `perf_metrics` ctest).

## Todos

- Add `shader_rebuild.h`, Graphics/menu button, setup Precompile wipe on `rebuild.request` + installer test
- Append Unreleased CHANGELOG notes when the control ships

## Design

Do not port Zelda `shader_prebuild.h`.
NSMBU warms via setup Precompile shaders.

- Thin [runtime/src/gfx/shader_rebuild.h](../../runtime/src/gfx/shader_rebuild.h): marker at `{config_dir}/shader-rebuild/rebuild.request`, `request_rebuild` / `requested` / `setup_hint` pointing at Precompile shaders; wipe helper for setup only.
- Graphics tab top: Shader cache section + Rebuild button (Zelda `shader_cache_section` pattern).
- macOS: same action in [runtime/src/gfx/metal/menu.mm](../../runtime/src/gfx/metal/menu.mm).
- [tools/installer/setup.py](../../tools/installer/setup.py): on Precompile, if marker exists, wipe `shadercache` then warm then remove marker; add assertion in [tools/installer/test_setup.py](../../tools/installer/test_setup.py).
- UI does not delete live in-memory caches mid-session.

## Out of scope

- Zelda 30 fps / True 60 radios and full shader-prebuild generations.

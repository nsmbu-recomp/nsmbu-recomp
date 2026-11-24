# Findings

## Report

- Reporter youtubeman2001-ctrl played v0.7.2 (56c4fe7) on macOS arm64 with a USA base disc.
- Opening a Boost Rush challenge crashes the game instantly or on the challenge loading screen.
- Steps: open Boost Rush, pick a challenge (mushroom course), then crash before play.
- Hardware: Apple M5 Pro, 24 GB.
- A crash log path was attached but not included in the repo.

## Status

- GitHub issue 45 is closed with label `bug`.
- Opened 2026-10-31; closed the same day by IrishBruse.
- IrishBruse pointed to a possible fix in v0.8.1 and asked the reporter to verify.
- The maintainer note ties the crash to a Mii draw before its shader exists during Boost Rush load.

## Changelog

- CHANGELOG.md does not cite issue 45 by number.
- Version 0.8.1 (2026-10-31) Fixed lists Boost Rush no longer crashing when a Mii draws before its shader exists.
- The same entry says Boost Rush on a USA or Europe base disc no longer crashes while the course loads.

## Code

- `recomp/hooks.txt` hooks `024EFE58` and `024F009C` for `ModelFFL::drawOpa` and `ModelFFL::drawXlu`.
- `runtime/src/hooks/cs_probe.cpp` defines `hook_024EFE58` and `hook_024F009C`.
- Those hooks call `mii_model_drawable()`, which uses `mii_model_draw_ready()` from `runtime/src/hooks/mii_draw.h`.
- `mii_model_draw_ready()` returns false when `Mii::CentralMgr` or its shader program pointer is null.
- `runtime/test/mii_draw_test.cpp` covers the null-pointer guard.
- Decomp sources include `vendor/decomp/src/graphics/ModelFFL.cpp`, `Mii_Shader.cpp`, and `FieldGameMode::cBoostRush`.

## Likely files and symbols

- `hook_024EFE58`, `hook_024F009C`, `mii_model_drawable()`, `mii_model_draw_ready()`
- `kMiiCentral`, `kMiiShaderProgram`, `kMiiShaderObject`
- `ModelFFL::drawOpa`, `ModelFFL::drawXlu`, `FieldGameMode::cBoostRush`
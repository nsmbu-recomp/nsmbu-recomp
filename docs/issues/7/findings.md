# Findings

## Report

- Reporter TTurbo23 saw heavy stutter and freezing on Windows 0.4.2 with an RTX 2070 Super.
- Stutter appeared at load, in cutscenes, and in gameplay from a fresh save.
- They compared it to a bad shader cache and worse than emulation.
- They filed no logs and left Renderer blank on the form.

## Status

- GitHub issue 7 is closed as a bug.
- Maintainer IrishBruse pointed to the v0.5.0 release on 2026-10-19.
- Reporter TTurbo23 replied on 2026-10-20 that stutter stopped and play felt great.
- IrishBruse then noted F12 opens graphics options for resolution and anti-aliasing.

## Changelog and code

- CHANGELOG.md does not cite issue 7.
- Version 0.5.0 lists “Large courses run smoother” under Fixed; that is the nearest shipped note.
- Later releases add shader and frame-time work that postdates this report.
- Examples include async shader compile, setup shader precompile, and deferred shader-cache load.
- `docs/developer/automation.md` and `docs/developer/verification.md` list no deterministic playtest for this report.
- Those docs tie coverage to shader precompile and the Vulkan smoke suite instead.

## Likely files and symbols

- `installer/setup.py`: `precompile_shaders()`
- `installer/gui/setup_gui.cpp`: shader-cache refresh UI
- `CHANGELOG.md`: 0.5.0 Fixed entry and later gfx performance notes
- User shader cache paths under `nsmbu/shadercache/` per `installer/setup.py` and `installer/README.md`
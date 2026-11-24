# Findings

## Report

- Reporter Joka2010 could not change resolution from the Quiver Launcher on Linux.
- They asked for a launcher graphics settings tab.
- They wanted separate TV and GamePad resolution.
- They wanted scaling, texture filtering, and anti-aliasing options.

## Status

- GitHub issue 17 is closed.
- Label: enhancement.
- Maintainer IrishBruse replied within minutes on 2026-10-25.
- IrishBruse said settings live in the game and open with F12 or backtick.
- No further reporter reply appears in the issue thread.

## Changelog and code

- CHANGELOG.md does not cite issue 17.
- CHANGELOG already documents an in-game Settings overlay opened with F12 or backtick.
- The Graphics tab lists internal resolution, anisotropic filtering, FXAA, and Vulkan presentation.
- The Display tab lists picture scaling (Smooth, Sharp, Integer), aspect ratio, and GamePad screen modes.
- README.md documents the same shortcuts and graphics options.
- No launcher-side graphics settings turned up in this search.

## Likely files and symbols

- `runtime/src/overlay/overlay.cpp`: `tab_graphics()`, `tab_display()`, `kTabNames`
- `runtime/src/overlay/overlay.cpp`: `hostui::res_scale()`, `render::set_aniso()`, `render::set_fxaa()`
- `runtime/src/overlay/overlay.cpp`: `hostui::set_scale_filter()`, `hostui::drc_mode()`
- `runtime/src/gfx/vulkan/settings.cpp`: saved graphics settings
- `README.md`: Settings shortcuts and feature summary
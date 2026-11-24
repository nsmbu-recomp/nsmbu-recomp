# Findings

## Report

- Chadman246 filed issue #59 on 2026-11-02 for version **0.8.3** on Windows.
- The report bundles three problems: 120 fps interpolation smear, vertical screen tearing, and Wii U Pro Controllers not working.
- The reporter enables interpolation at 120 fps on the title screen.
- Every sprite smears and looks nauseating at 120 fps; 60 fps looks clear with good frame times.
- Vsync was on with a locked 120 fps and no stutter when the reporter replied.
- Screen tearing appears vertically on the 2D side scroller at higher frame rates.
- Wii U Pro Controllers do not work at all for this reporter.
- Multiplayer over Parsec (Soda Arcade) stays limited to GamePad and one player.
- The game reportedly supports four players with remotes and Pro Controllers on original hardware.

## GitHub status

- GitHub marks issue #59 **CLOSED** as of 2026-11-04T18:53:23Z.
- Label: `bug`.
- IrishBruse asked for performance-overlay numbers and suggested graphics debug settings.
- IrishBruse could not reproduce the smear at 120 fps on their machine.
- IrishBruse said local play works with two Xbox controllers and pointed to Parsec for Pro Controller trouble.
- Chadman246 still saw title-screen character smear with vsync and locked 120 fps.
- IrishBruse closed the issue as fixed on a newer build, without a linked fix or reporter confirmation.

## Changelog

- CHANGELOG.md does not cite `#59`.
- Version **0.8.1** (2026-10-31) fixes 120 fps camera backward steps, world-map shadow flicker, and in-between player animation blending.
- None of those entries describe title-screen smear or Pro Controller detection on Windows.
- Version **0.8.3** only fixes settings-menu crashes and scrolling; it does not mention interpolation or controllers.

## Code

- Frame interpolation and presentation pacing live in `runtime/src/hooks/interp.cpp` (`set_present_vsync`, `paced_interpolation`).
- Graphics overlay settings for interpolation are in `runtime/src/gfx/vulkan/overlay_sdl.cpp`.
- Pro Controller mode toggles through `input::pro_controller()` in `runtime/src/platform/input_sdl.cpp`.
- The settings overlay wires Pro Controller choice in `runtime/src/overlay/overlay.cpp`.
- Four-player bindings use `runtime/src/input/input_map.cpp` (added with #9).
- Issue #9 findings list #59, #68, and #76 as follow-up Pro Controller or multiplayer reports.
# Findings

## Report

- GitHub issue #38, label `enhancement`, filed 2026-10-29 on NSMBU v0.7.2 (Windows).
- At 21:9, menus and HUD sit in a centered 16:9 band instead of reaching the screen edges.
- The reporter also sees purple crop borders, cropped level transitions, background pop-in at the old 16:9 edge, and empty gaps at level structure edges.
- They note intermittent frame dips on the world map and in levels; no crash logs.

## Status

- State: CLOSED (updated 2026-11-07).
- IrishBruse said full ultrawide support needs many patches and may fit a mod better.
- The reporter said older builds looked stretched full-screen and asked for a stretch-versus-widen user choice.
- IrishBruse closed the issue because stretch was added in v0.9.0.

## Changelog

- v0.7.0 widened the view on non-16:9 windows and kept the HUD in its original frame.
- v0.7.1–v0.7.2 list targeted 21:9 fixes (HUD margins, fades, world map bars, scenery pop-in).
- v0.9.0 adds **Stretch picture instead of widening the view** under Settings → Display, on by default.
- That entry directly answers the reporter's requested toggle; no changelog line names #38.

## Code

- `runtime/src/hooks/aspect.cpp` owns `stretch_image()`, `set_stretch_image()`, `is_hud_edge_pane()`, and pane layout when stretch is on or off.
- `runtime/src/gfx/aspect_panes.h` and `runtime/src/gfx/aspect_view.h` define HUD pane roles and widened projection math.
- `runtime/src/overlay/overlay.cpp` exposes the stretch setting in the Display tab.
- Tests in `runtime/test/aspect_view_test.cpp`, `tv_stretch_test.cpp`, and `bg_scroll_wide_test.cpp` cover widen and stretch behavior.

## Relation to today

- The centered HUD the reporter describes matches the widen-view design from v0.7.0 onward, not a standalone bug.
- Turning stretch on restores full-frame 16:9 picture scaling; turning it off keeps the wider field of view.
- Partial ultrawide polish landed in v0.7.1–v0.7.2; the performance stutter in the title was not part of the closure reason.
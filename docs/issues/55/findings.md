# Findings

## Report

- ChenBrian833 pressed F12 on the Linux title screen in v0.8.2 (affa9dd).
- The port settings overlay opened and the game crashed immediately.
- Reproduction was a single F12 press on the title screen.
- The report included a crash log attachment and an in-app crash path.

## Status

- GitHub issue 55 is closed with the `bug` label.
- IrishBruse commented on 2026-11-01 that v0.8.3 fixed the crash.
- No reporter follow-up appears in the thread.

## Changelog and history

- CHANGELOG.md does not cite issue 55.
- Version 0.8.3 lists a Controls-tab settings crash fix for short controller diagrams.
- Commit `8e691326` matches that entry and landed between v0.8.2 and v0.8.3.
- A related pull request references issue 56, not 55.

## Likely files and symbols

- `runtime/src/overlay/overlay.cpp`: `hostui::key()` opens the overlay on F12 or backtick.
- `runtime/src/overlay/overlay.cpp`: `Ui::tab` defaults to `kControls`; `tab_controls()` builds the diagram.
- `runtime/src/overlay/controls_view.cpp`: `controls_diagram_fits()`, `layout()`, `compact_side`.
- `runtime/src/overlay/controls_view.h`: `controls_diagram_fits()` declaration.
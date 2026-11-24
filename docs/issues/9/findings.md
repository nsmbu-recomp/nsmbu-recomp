# Findings

## Report

- IrishBruse opened enhancement #9 on 2026-10-20.
- The report says only one controller or keyboard works today.
- It asks for multiple controllers, per-player keybinds, and a sync-all-layouts option in the controller menu.

## GitHub status

- GitHub marks issue #9 **CLOSED** as of 2026-10-24T03:21:30Z.
- IrishBruse (the author) closed it.
- No issue comments explain the closure.
- Closure had no linked commit in GitHub events.

## Changelog

- CHANGELOG.md does not cite `#9`.
- Version **0.6.0** (2026-10-24) adds up to four controllers as separate players.
- That release notes per-player bindings and one shared layout for every player.

## Code

- Commit `d2926b91` ("Add local multiplayer and per-player controls (#9)") landed on 2026-10-20.
- `runtime/src/input/input_map.{h,cpp}` stores four `Mapping` entries, a `sync` flag, and `controls.json` version 2.
- `overlay.cpp` `tab_controls()` exposes "Same controls for every player" and per-player tabs.
- `input_sdl.cpp` assigns up to `input::kMaxPlayers` (4) controller slots.
- `padscore.cpp` maps Wii U channels to host player indices.
- `runtime/test/input_map_test.cpp` defines `test_issue_9_multiplayer_bindings`; `docs/developer/automation.md` ties it to `ctest -R input_mapping`.
- README lists up to four players, each with a controller.

## Related follow-ups

- Later open issues (#59, #68, #76) report multiplayer or Pro Controller problems on some setups.
- Those reports are separate from this enhancement request.
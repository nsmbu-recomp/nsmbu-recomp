# Findings

## Report

- wodnevets filed issue #76 on 2026-11-14 for version **0.9.1** on SteamOS (Steam Deck).
- The game runs smoothly on the Deck.
- Two-player mode fails at the player select screen.
- recomplauncher shows both controllers connected.
- The second controller does not register in the game during local two-player setup.

## GitHub status

- GitHub marks issue #76 **OPEN** with label `bug`.
- wodnevets opened it on 2026-11-14; GitHub last updated it on 2026-11-22.
- The issue has no comments explaining cause or resolution.

## Changelog

- CHANGELOG.md does not cite `#76`.
- Version **0.6.0** (2026-10-24) added up to four controllers as separate players with per-player bindings.
- No later entry mentions second-controller detection on Steam Deck or Linux handhelds.

## Code

- `runtime/src/platform/input_sdl.cpp` opens SDL gamepads into `g_slot[0..3]` via `open_controller()` and `free_slot_locked()`.
- `controller_connected(player)` returns true only when `g_slot[player]` holds a gamepad id.
- `runtime/src/hle/padscore.cpp` maps Wii U channels to host player indices in `player_for_channel()`.
- `connected()` always reports player 0 present; players 1–3 need `controller_connected()`.
- `KPADReadEx()` and `WPADProbe()` gate input on `connected(chan)` and read per-player state through `input::read(player)`.
- Issue #9 findings list #59, #68, and #76 as follow-up multiplayer or Pro Controller reports.
- Issue #68 (open, Windows) describes the same class of bug: the second controller never serves Player 2.
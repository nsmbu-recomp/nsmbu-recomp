# Findings

## Report

- Natalinoino filed issue #68 on 2026-11-09 for version **v0.9.1** on Windows.
- The reporter connects two Nacon controllers (GC-200WL and GC100) for local multiplayer.
- Both are configured in F1 settings as Wii U Pro Controller with per-player bindings.
- The second controller drives Player 1 instead of Player 2.
- The reporter uses the Europe base disc and closed Steam during testing.

## GitHub status

- GitHub marks issue #68 **OPEN** with label `bug`.
- IrishBruse asked for logs and for confirmation that both controllers work in other apps.
- Natalinoino attached `nsmbu.log` and `nsmbu-previous.log` on 2026-11-11.
- No maintainer reply or closure followed the log upload.

## Changelog

- CHANGELOG.md does not cite `#68`.
- Version **0.6.0** (2026-10-24) adds up to four controllers with per-player bindings (#9).
- No later entry mentions Nacon hardware or second-player routing on Windows.

## Code

- `runtime/src/platform/input_sdl.cpp` keeps one global `pro_controller()` flag in `g_pro`.
- Settings expose that flag once as "Keyboard and controllers act as" in `overlay.cpp` `tab_controls()`.
- There is no per-player Pro Controller toggle; per-player tabs edit bindings only.
- `open_controller()` assigns each SDL gamepad to the first free slot in `g_slot[]` (up to four).
- The poll loop maps each connected pad through `input_map::player(slot)` into `g_pads[player]`.
- `runtime/src/hle/padscore.cpp` `player_for_channel()` maps KPAD channel to host player when Pro mode is on.
- `KPADReadEx` calls `crashrec::read(1 + chan, player)`; `VPADRead` in `system_stubs.cpp` reads Player 1 only and clears GamePad input in Pro mode.
- Likely symbols: `g_pro`, `g_slot`, `player_for_channel`, `connected`, `input_map::player`.

## Assessment

- The report matches a multiplayer input-routing problem, not a missing four-player feature.
- Code and changelog support four players, but this setup-specific failure has no recorded fix.
- Issue #9 findings lists #68 among follow-up multiplayer or Pro Controller reports.
- Related open issue #76 reports a second controller not detected.
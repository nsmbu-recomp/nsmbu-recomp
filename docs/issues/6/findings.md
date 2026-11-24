# Findings

## Report

- The reporter built from a game ISO and an update folder on Windows.
- The game window closed immediately after pressing Play.
- Later launches of `NSMBU.exe` also opened and closed instantly.
- They tried multiple versions, starting from the latest (v0.4.2 at report time).
- No crash log was attached in the original report.

## Status and discussion

- Issue state is **CLOSED** (author `kostipero6-ops`, label `bug`).
- Maintainer IrishBruse asked for a proper `nsmbu.log` and later for logs from `docs/logs.md`.
- The reporter's crash dump shows a startup access violation on v0.4.2 with Vulkan on Windows 11.
- Boot loaded the 1.3.0 update RPX from title `0005000e10101d00`, not a base-disc build.
- The last log line warns that `content/Common/Pack` is missing in the game folder.
- IrishBruse said the install had only the 1.3.0 update and was missing the base game (`.wux`/`.wud`).
- v0.5.0 was about to add clearer setup errors and broader version support.
- The issue closed on 2026-10-20 with a note to reopen if the crash persists.

## Changelog and code

- CHANGELOG does not cite `#6` directly.
- v0.5.0 (2026-10-19) adds setup refusal for update-only dumps and clearer crash or setup error dialogs.
- `installer/setup.py` function `require_playable_game_folder` raises `SetupError` when only an update title id (`0005000e…`) or base course files are missing.
- `vendor/ZeldaWWHDRecomp/runtime/src/game_languages.cpp` logs when `content/Common/Pack` is absent; it does not stop the game.
- `docs/developer/automation.md` links playtest case `startup` to this issue.

## Assessment

- This looks like an incomplete install (update without base game), not a generic Windows startup bug.
- Setup validation in v0.5.0+ should block that path before play.
- The runtime crash on v0.4.2 matches update-only content hitting guest code during startup.
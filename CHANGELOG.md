# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).
Player-facing notes only; within each section, order by importance to someone playing the game.
Use the `changelog` skill when editing.

## [Unreleased]

### Changed

- Mods run on LuaJIT and load `.lua` scripts.
  Luau mods with `.luau` files do not load until the scripts are converted.
- The mod examples and the SDK ship TypeScript source compiled to Lua.

## [0.5.0] - 2026-10-19

### Added

- Europe NSMBU 1.3.0 works.
- The USA and Europe NSMBU + NSLU packs work.
- The USA base disc runs without the 1.3.0 update.
  Mods still need that update.
- Crash and setup error dialogs stay compact and offer Report crash / Report issue.

### Changed

- Report crash and Report issue leave Logs empty for you to attach, fill Platform from this machine, and put the error dialog text in From the app.
- You can install from the base disc alone.
  The 1.3.0 update folder is optional.
- Setup refuses update-only dumps with a clear message.
- The Keys screen accepts a pasted disc key.
- Save states use `.nsstate` names and a new full-state format.
  Older full states no longer load.
- Mod settings live in one `mods.json`.
- Mods can use radio options.
- Number options in the Mods tab honour their step.
- Installer progress layout is clearer.
- The Keys screen layout is clearer.
- Save import looks for the real NSMBU save files.
- Bug-report copy names the real NSMBU save files.

### Fixed

- Large courses run smoother.
- World-map paths stay above the hill.
- Report crash closes the error dialog after opening GitHub and the logs folder.
- Closing the error viewer no longer pauses or slows the map.

## [0.4.2] - 2026-10-14

### Changed

- Install progress gives space to the steps.
  Details stays readable.
- Translating the game code shows live progress.

### Fixed

- The setup window follows the system theme.
- Setup buttons and long labels no longer clip.

## [0.4.1] - 2026-10-14

### Fixed

- The first-run game build no longer fails while translating the game code.

## [0.4.0] - 2026-10-14

### Added

- Drop-in Cemu content packs work without a player `manifest.json`.
- Keep running in the background when the window is not focused.

### Changed

- Mod packages live under **Mods → Open mods folder**.
  The Mods tab opens that folder and rescans while every package is disabled.
- The TV window size is remembered across launches.
- Guest texture decode is faster.

### Removed

- The old `ModManager/` tree is removed with no migration.
- Bloom and ambient-occlusion controls are removed from Graphics.

## [0.3.0] - 2026-10-13

### Added

- Luau mods with optional game content and hot reload.
- Mods can list and spawn live level actors, and use high-level level and player helpers.
- Settings Audio tab: mute, volume, and TV / GamePad / Auto output.
- Performance overlay lag breakdown for GPU, renderer CPU, vsync, waits, and idle.

### Changed

- The settings overlay opens with `` ` `` or F12.
- F1–F5 load save-state slots.
- The Controls stick dead zone applies to gamepad input.
- Faster texture decode.
- Faster save-state load and restore.
- The port is USA 1.3.0 only.

### Fixed

- Boot and draw-queue crashes are fixed.
- The overworld path seam is fixed.
- Save states load reliably after a rebuild.

## [0.2.0] - 2026-10-10

### Added

- World 1-1 through 1-3 are playable.
  Performance is low and frame rate often stays below 60 fps.

### Changed

- Default keyboard maps run to Y and spin jump to ZR.
- Controls UI sizing is more consistent.

### Fixed

- World 1-1 no longer crashes.
- Background flicker is fixed.
- Starting at 2× resolution no longer corrupts textures.
- Save states load correctly.
- Audio playback is more reliable.

## [0.1] - 2026-10-09

### Added

- The download is the runtime and installer only.
  The first start builds the game from your dump.
- The frame menu offers 60–240 fps.
  Game logic stays at 60 steps a second.

### Changed

- The TV and GamePad present together on one vsync.

[Unreleased]: https://github.com/nsmbu-recomp/nsmbu-recomp/compare/v0.5.0...HEAD
[0.5.0]: https://github.com/nsmbu-recomp/nsmbu-recomp/compare/v0.4.2...v0.5.0
[0.4.2]: https://github.com/nsmbu-recomp/nsmbu-recomp/compare/v0.4.1...v0.4.2
[0.4.1]: https://github.com/nsmbu-recomp/nsmbu-recomp/compare/v0.4.0...v0.4.1
[0.4.0]: https://github.com/nsmbu-recomp/nsmbu-recomp/compare/v0.3.0...v0.4.0
[0.3.0]: https://github.com/nsmbu-recomp/nsmbu-recomp/compare/v0.2.0...v0.3.0
[0.2.0]: https://github.com/nsmbu-recomp/nsmbu-recomp/compare/v0.1...v0.2.0
[0.1]: https://github.com/nsmbu-recomp/nsmbu-recomp/releases/tag/v0.1

# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).
Append each change under Unreleased as it lands; at release the `changelog` skill cleans that block into player-facing version notes.

## [Unreleased]

### Changed

- Renamed `.git-hooks` to `.githooks` (update `.git/hooks/pre-push` if it still points at the old path).
- Removed leftover docs and runtime checks that said mods need the 1.3.0 update or do not work on base-disc builds.
- Removed unused developer tools (`tools/check/`, `rpxinfo.py`, Python disc reader, `run_bench.py` CLI).
  `nsmbu extract` uses `nsmbu-extract`; use `nsmbu bench` for performance scenarios.
- Dropped `docs/ts-tools-port.md`, Android `--service-fixture` packaging, and the dead `game_sample_report.py` doc reference.
  Renamed bench tests to `test_bench_lib.py`.
## [0.8.1] - 2026-10-31

### Added

- Mods work on the USA and Europe base disc (title version 0), not only on the 1.3.0 update or combo packs.
- iOS and iPadOS build: the Metal renderer in a UIKit app with on-screen controls, game controllers, and hardware keyboards.
  Picture-in-picture puts the GamePad picture over the TV picture, where it takes touches.
- Press `T` in the game window to toggle between 60 fps and 120 fps frame interpolation (Vulkan SDL and macOS Metal).
- Metal decodes BC1–5 textures on upload when the GPU cannot sample them.

### Changed

- The performance overlay uses a clearer layout with labeled rows for output FPS, logic rate, CPU and GPU timing, and presentation settings.
  Debug overlay sections are independent (output, graph, presentation, waits, and more).
  It no longer repeats the GPU name, driver, and Vulkan version in the corner (those stay in Copy performance report).
- Settings uses a compact header: tabs sit on the same row as the close button, without the controller help line under the title.
- Graphics card choice for the next start is at the bottom of the Graphics tab instead of Debug.
- In-between frames blend every player's head and course model animation with the camera.
  Mario's head, Luigi, and both Toads move with the rest of the body instead of staying on the 60 Hz step.
  Moving with the camera no longer leaves models half a step behind the view.
- Setup and a full recompile pick up hook changes again, so fixes that rely on new hooks ship after you rebuild.
- A full recompile no longer stops on save-debug hooks after hook files change.

### Fixed

- Boost Rush no longer crashes when a Mii is drawn before its shader exists, and Boost Rush on a USA or Europe base disc no longer crashes while the course loads.
- Android setup finishes translating when the phone's Python has no multiprocessing module.
  It uses one process instead of stopping.
- Course tiles stay visible on Metal when internal resolution is above 1x.
- World-map shadows no longer flicker at 120 fps.
  The picture is taken on the in-between present, after the light maps are ready.
- 120 fps no longer steps the camera backward on the in-between frame.
  Changing the frame rate drops saved poses instead of blending across the gap.
- iOS: the on-screen stick works while the GamePad picture fills the screen, as on the world map.
- The performance overlay frame graph uses green for good frame times and fills under the line correctly when a spike appears.
- The Settings close button sits higher in the header row.

## [0.8.0] - 2026-10-30

### Added

- Android setup app for phones and emulators.
  You add your own game; the download does not include game code.
  Touch controls, full screen, and save import and export are included.
- Frame rate choices: unlocked, a custom rate from 60 to 1000, a 30 fps preset, and 360p and 540p internal resolution.
- Start and Select together, or R3, open Settings, as well as `` ` `` or F12.
- Windows on ARM64 builds.

### Changed

- The GamePad screen starts off.
- High frame rates keep normal game speed.
- Android Settings is larger and easier to use with touch or a controller.
- Setup and a full recompile use every CPU core, so they finish sooner.

### Fixed

- High frame rates no longer stall on black loading screens or make Mario jitter, and they hold the rate you pick.
  A logic step that runs a little long no longer drops the picture rate back to 60.
- On Android, the Controls diagram no longer overlaps the options.
- 30 fps no longer crashes on some screen changes, and switching back to 60 fps stays at 60.
- Settings text fields accept keyboard input while the overlay is open.
- You can pick which graphics card Vulkan uses.
  On Metal, used ? blocks stay visible.
  The window title no longer shows garbled punctuation.

## [0.7.2] - 2026-10-27

### Fixed

- On 21:9, enemies, blocks, bushes, and other course scenery no longer pop in at the old 16:9 edge.
  The title parade stays centered.

## [0.7.1] - 2026-10-27

### Changed

- Aspect ratio is under Settings → Display and is marked experimental.
- Ultrawide layouts keep edge HUD on the screen with the same side margin as 16:9.
  Centered menus and title UI stay in a 16:9 frame while the world view expands.

### Fixed

- The aspect ratio chosen in Settings is restored on the next launch.
  A set `NSMBU_ASPECT` value still overrides the saved choice.
- Setup lets you choose **Copy saves and settings from another NSMBU folder** when no earlier install is detected.
- Install and setup work when the install path contains square brackets.
- On 21:9, world-map Menu, Map View, and Items labels stay on their buttons instead of clipping at the screen edge.
- On 21:9, title characters stay centered under the logo.
- On 21:9, fades fill the wide picture.
- On 21:9, file-select full-width art fills the frame; buttons and cards stay 16:9.
- On 21:9, title-screen scenery at the edges no longer pops in late.
- On 21:9, the title screen no longer shows a one-pixel bright column on the far right.
- On 21:9, the world map no longer shows dark bars just outside the old 16:9 frame.
- Ultrawide no longer stretches the GamePad picture; only the TV view grows.

## [0.7.0] - 2026-10-26

### Added

- Settings → Saves shows a short preview of the real game save (files, world, lives, play time) and opens that saves folder from the top of the tab.
- The performance overlay shows average fps and logic steps/s, plus GPU and CPU timing.
  Reset clears those averages; Copy performance report includes them.

### Changed

- Wider and taller windows show more of the scene instead of stretching a 16:9 picture.
  The HUD stays in its original frame.
- Settings language and content-mod Restart relaunches the app, the same way as switching Metal and Vulkan.
- Setup precompiles shaders as part of install, repair, and update after the game is built.
  Menu and Ready to play keep a Recompile shaders button to run that pass again.
- Settings → Language lists only the text languages on the installed disc.
  A USA dump shows English, French, and Spanish; a European dump adds the other European languages.
- Settings tabs: Controls is first; Saves sits between Audio and Mods; Debug is capitalized.
- The window title no longer shows a live frame rate.
  Use the performance overlay for fps.
- The performance overlay no longer shows the renderer, scale, and frame-mode footer under the graph.

### Fixed

- World-map shadows no longer flicker at 120/144 fps frame interpolation.
- 144 and 165 fps keep the rate you select when a frame runs early or late.
  Late frames no longer restart the schedule and settle well below the selection.
  Early world-map frames show on the next time slot instead of being dropped.
- High frame rates keep normal game speed (60 steps a second).
  Dropping every in-between frame no longer leaves the game running about twice as fast.
- The Settings restart confirmation stays centered on screen.
- The performance overlay shows the frame rate once, above the frame-time graph.

## [0.6.3] - 2026-10-25

### Fixed

- Entering another course from the world map at a high frame rate no longer crashes.
- A Quick Save is still there after you quit and start the game again.
  The file shows that course progress, and opening the file keeps it.
- Save states can be taken on the world map and inside a course.

### Changed

- A portable save state that does not match your game save is refused instead of loading.
  Use a state file from the same save slot on this machine.

### Removed

- The published game no longer accepts the live control link that `nsmbu` uses to press buttons and read status while you play.
  Builds you compile yourself still support it.

## [0.6.2] - 2026-10-24

### Fixed

- The screen no longer freezes while the music keeps playing if a frame's pictures are different sizes.

## [0.6.1] - 2026-10-24

### Removed

- Opening a course directly no longer rewrites the loading card's world name or green level number.

### Fixed

- The world map accepts input after the opening cinematic.
- The world map shows the course number after the opening cinematic.
- Loading another course while you are already playing one no longer crashes.
- Quitting to the world map from a course you opened no longer crashes.

## [0.6.0] - 2026-10-24

### Added

- The Europe base disc runs without the 1.3.0 update.
  Mods still need that update.
- Up to four controllers work as separate players, with per-player bindings and an option to keep one layout for every player.
- Setup can precompile shaders before you play, with a progress bar.
  You can skip it, and shaders still compile during play which may cause stutters.

### Changed

- Higher frame rates keep normal game speed and draw in-between frames, so characters move between the game's 60 steps.
  144 fps is available, and these options are experimental.
- The renderer uses less CPU each frame, so busy scenes and high frame rate settings hold their speed better.
- Save states are small again.
  Turn on Full save states in the Saves tab for an exact copy of the running game.
  A full state already in a slot still loads when it is the newest file there.
- The settings overlay is wider, and its title bar matches the panel with no separator line so it no longer looks like a movable window.

### Removed

- The frame rate is removed from the window title.

### Fixed

- Quitting to the world map from a course you started in puts Mario on that course's node.
- Frames stay evenly spaced while the game is waiting for the display.
- The Keys screen takes the disc key and the Wii U common key typed or pasted as 32 hex digits.
  A key file saved by Notepad or a Cemu `keys.txt` works too.

## [0.5.0] - 2026-10-19

### Added

- Europe NSMBU 1.3.0 works.
- The USA and Europe NSMBU + NSLU packs work.
- The USA base disc runs without the 1.3.0 update.
  Mods still need that update.
- Crash and setup error dialogs stay compact and offer Report crash / Report issue.
- Cemu content mods are supported drop them into the mods folder and enable them in F12 menu then reboot.

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

[Unreleased]: https://github.com/nsmbu-recomp/nsmbu-recomp/compare/v0.8.1...HEAD
[0.8.1]: https://github.com/nsmbu-recomp/nsmbu-recomp/compare/v0.8.0...v0.8.1
[0.8.0]: https://github.com/nsmbu-recomp/nsmbu-recomp/compare/v0.7.2...v0.8.0
[0.7.2]: https://github.com/nsmbu-recomp/nsmbu-recomp/compare/v0.7.1...v0.7.2
[0.7.1]: https://github.com/nsmbu-recomp/nsmbu-recomp/compare/v0.7.0...v0.7.1
[0.7.0]: https://github.com/nsmbu-recomp/nsmbu-recomp/compare/v0.6.3...v0.7.0
[0.6.3]: https://github.com/nsmbu-recomp/nsmbu-recomp/compare/v0.6.2...v0.6.3
[0.6.2]: https://github.com/nsmbu-recomp/nsmbu-recomp/compare/v0.6.1...v0.6.2
[0.6.1]: https://github.com/nsmbu-recomp/nsmbu-recomp/compare/v0.6.0...v0.6.1
[0.6.0]: https://github.com/nsmbu-recomp/nsmbu-recomp/compare/v0.5.0...v0.6.0
[0.5.0]: https://github.com/nsmbu-recomp/nsmbu-recomp/compare/v0.4.2...v0.5.0
[0.4.2]: https://github.com/nsmbu-recomp/nsmbu-recomp/compare/v0.4.1...v0.4.2
[0.4.1]: https://github.com/nsmbu-recomp/nsmbu-recomp/compare/v0.4.0...v0.4.1
[0.4.0]: https://github.com/nsmbu-recomp/nsmbu-recomp/compare/v0.3.0...v0.4.0
[0.3.0]: https://github.com/nsmbu-recomp/nsmbu-recomp/compare/v0.2.0...v0.3.0
[0.2.0]: https://github.com/nsmbu-recomp/nsmbu-recomp/compare/v0.1...v0.2.0
[0.1]: https://github.com/nsmbu-recomp/nsmbu-recomp/releases/tag/v0.1

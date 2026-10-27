# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).
Append each change under Unreleased as it lands; at release the `changelog` skill cleans that block into player-facing version notes.

## [Unreleased]

### Added

- A decomp verification checklist (`vendor/decomp/docs/TODO.md`) that breaks the decomp notes into single address and offset claims, checked against the USA and Europe RPX.
- `tools/rpx/ppcdis.py` annotates import calls and calls to a name in `tools/recomp/headers_names.txt`, and finds the dump through `game/active`.
- A plan for test isolation, scripted run compare, CI checks, and release packages taken from later Wind Waker HD work.
- Decomp notes for the sead / ApplicationFramework frame loop, and a plan to drive present rate from wall-clock time while keeping 60 logic steps/s.
- Settings → Saves shows a short preview of the real game save (files, world, lives, play time) and opens that saves folder (not save states) from the top of the tab.
- The performance overlay shows average fps and logic steps/s, GPU/device, Game/Render CPU, and GPU interval.
  Reset also clears those averages; Copy performance report includes them.
  The overlay no longer shows the renderer, scale, and frame-mode footer under the graph.

### Changed

- Boot recipes, build/launch notes, and profiler docs fold into `docs/automation.md`, `docs/cli.md`, and `docs/performance.md` (drop `shortcuts.md`, `build.md`, and `perf.md`).
- Aspect ratio moved from Settings → Graphics to Settings → Display and is marked experimental.
- The window title never shows a live frame rate, including in development builds.
  Use the performance overlay for fps.
- Settings language and content-mod Restart now relaunches the app (same path as switching Metal and Vulkan) instead of soft-rebooting the guest in the same window.
- Setup precompiles shaders as part of install, repair, and update (after the game is built).
  Menu and Ready to play keep a Recompile shaders button to run that pass again.
- `nsmbu installer` prints each build step (toolchain, SDL, compile, package, setup) instead of staying quiet until the end.

### Fixed

- 21:9 enemies, blocks, and bushes are on screen before they reach the old 16:9 edge.
  The title parade still uses the 16:9 scroll window, so those characters stay centered.
- 21:9 course lives, coins, score, and timer stay on the screen edges.
  They no longer jump back to the 16:9 frame, and off-screen popups stay off the sides.
- The aspect ratio chosen in Settings is restored on the next launch.
  A set `NSMBU_ASPECT` value is used instead of that saved choice.
- 21:9 button labels stay on the buttons.
  Menu, Map View, Items, and the world name were drawn past the pills and clipped at the screen edge.
- 21:9 title characters stay centered under the logo, as they do at 16:9.
  The wide background still extends past the 16:9 frame.
- Widescreen HUD sits on the screen edges.
  World-map Menu, Map View, and Items, and the course HUD, keep the 16:9 margin.
  Centered menus stay in the 16:9 frame.
- 21:9 fades cover the wide picture.
  Fader draws use the full target, so a fade is black in the side strips.
- 21:9 file-select plates fill the wide picture.
  Full-width backgrounds and bars stretch to the frame.
  Buttons and cards stay at 16:9.
- 21:9 title UI stays at 16:9.
  Shaders that write window pixels are centered in the wide picture.
  The “Press 2!” prompt keeps that size while its ortho zooms.
  World cameras still use the full width.
- The 21:9 scroll window matches the camera.
  Scenery that spawns from the screen edges is already in frame, including the title bush.
- 21:9 no longer leaves a one-pixel column on the far right of the picture.
  The wide target is 1707 pixels and the scissor was cut off at 1706, so the title sky showed through.
- 21:9 world map no longer draws dark bars just outside the original 16:9 frame.
  The screen overlay stays on that frame, and the wider map is still visible beside it.
- 21:9 no longer widens the GamePad picture.
  Targets at 854×480 and its smaller copies stay that size; the TV picture is still the one that grows.
- 144 and 165 fps keep 60 logic steps/s.
  Pass timing treated each present slot as work, so the pacer skipped in-between frames; with one wait per present that left about two presents per step (~80 steps/s at 165 fps).
  Those rates now keep every planned present, and pad only when a step shows fewer frames than planned.
- High frame rates keep 60 logic steps/s when the pacer drops every in-between frame.
  A full drop left no wait, so vsync at 120 Hz ran the game about twice as fast.
- The mod package test builds with the Scene restart Lua call.
- 144 and 165 fps keep the rate you select when a frame runs past its time slot.
  A late frame was starting the schedule over, so each following frame waited a full slot and the picture settled well below the selection (about 105 fps with 165 fps selected).
  The display refresh rate is taken from the monitor's mode list when the desktop does not report one.
- World-map shadows no longer flicker at 120/144 fps frame interpolation (#20).
  In-between passes skip redrawing outside a course, so light maps and shadow maps keep the last logic frame.
- The Settings restart confirmation stays centered on screen.
- The performance overlay shows the frame rate once, above the frame-time graph.
- A 144 or 165 fps target keeps each new frame on its own time slot.
  In-between frames were sharing one vsync wait, then the next frame waited out the rest of a 60 fps slice.
- On the world map, a picture that finishes early is shown on the next time slot while the next one keeps drawing.
  It was dropped, so the overlay sat near half the selected rate while the window title still counted every swap.
- Sampler setup keeps the last few sampler states.
  Each texture was building a text key and searching the full sampler map.
- Recompiling Europe 1.3.0 accepts the error-viewer hook quietly.
  Europe chooses that viewer's layout from a language table.
  The hook still skips the viewer, so closing it cannot pause the game.
- Recompiling a base disc no longer warns about hooks that wrap a differently compiled function, or about hooks the base disc does not have.
  The draw-queue, course-setup, and continue-first-save wraps still apply on those bodies.
  Hooks with no matching function on the base disc are skipped without a message; mods stay unsupported there.

### Changed

- Wider and taller aspect ratios show more of the scene instead of stretching the picture.
  Edge HUD keeps the same margin from the screen sides that it has at 16:9.
  Centered menus stay in the 16:9 frame.
- Settings tabs: Controls is first; Saves sits between Audio and Mods; Language drops the About blurb; Debug is capitalized.
- Settings → Language lists only the text languages on the installed disc.
  A USA dump shows English, French, and Spanish; a European dump adds the other European languages.
- Rename `runtime/tools` to `runtime/test`.
- Run playtests in parallel and split each case into its own file.
- Drop `downloads/` from the game dump layout notes.
- Bump `vendor/decomp` with more course, camera, boot, and system decomp.

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

[Unreleased]: https://github.com/nsmbu-recomp/nsmbu-recomp/compare/v0.6.3...HEAD
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

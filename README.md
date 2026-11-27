# New Super Mario Bros. U: Native Port / Recompilation

This project compiles your copy of New Super Mario Bros. U into a native program.
The release includes the runtime, build tools, and installer.

> [!IMPORTANT]
> There is no Nintendo code, no assets, and no game data in this project or its releases.
> You need your own legally dumped copy of the game.
> Nothing in this repo, in any release, or in the build process contains or downloads Nintendo IP.

<a href="https://recomplauncher.com/game/nsmbu-recomp"><picture><source media="(prefers-color-scheme: dark)" srcset="https://recomplauncher.com/assets/press/launcher/badge-light.svg"><img alt="Get it on RecompLauncher" src="https://recomplauncher.com/assets/press/launcher/badge-dark.svg" height="48"></picture></a>

## Features

- Windows, macOS, Linux, Android, and iOS — [per-system notes](docs/user/).
- **Up to four players**, each with a controller.
- **[Save states](docs/features/portable-save-states.md)** — <kbd>Shift</kbd>+<kbd>F1</kbd>–<kbd>F5</kbd> saves, <kbd>F1</kbd>–<kbd>F5</kbd> loads.
- **[Mods](docs/modding/README.md)** for Cemu graphics packs and shader packs.
- **[GamePad screen](docs/features/gamepad-screen.md)** in its own window, over the TV picture, or hidden.
- **[On-screen controls](docs/features/on-screen-controls.md)** on Android and iOS.
- **[Screenshots](docs/features/screenshots.md)** of the TV picture — <kbd>F10</kbd> by default.
- **[Sound and language](docs/features/audio.md)** — volume, mute, and console language.
- **[Gyro aiming](docs/features/gyro.md)** with supported controllers, a mouse, or the right stick.
  Set the source, sensitivity, and recalibrate under Settings, then Controls, then Gyro.

Experimental — each has a page under [docs/features](docs/features/):

- **[Higher frame rates](docs/features/frame-rate.md)** — Settings → Graphics.
- **[Wider or taller picture](docs/features/aspect-ratio.md)** — Settings → Display.
- **[Internal resolution and image filters](docs/features/internal-resolution.md)**.

### Controls

- **Settings.**
  Press <kbd>F12</kbd>, or click the right stick (<kbd>R3</kbd>).
  On macOS, <kbd>Cmd</kbd>+<kbd>,</kbd>.
- **Full screen.**
  <kbd>F11</kbd> or <kbd>Alt</kbd>+<kbd>Enter</kbd>.
  On macOS, <kbd>Cmd</kbd>+<kbd>F</kbd>.
- **Save states.**
  <kbd>Shift</kbd>+<kbd>F1</kbd>–<kbd>F5</kbd> saves slots 1–5.
  <kbd>F1</kbd>–<kbd>F5</kbd> loads those slots.
- **Screenshot.**
  <kbd>F10</kbd> saves the TV picture.
- **Mute.**
  Press <kbd>M</kbd> to mute or unmute the game.
  The game keeps that choice.
- **GamePad screen.**
  <kbd>Ctrl</kbd>+<kbd>G</kbd> shows or hides it.
  On macOS, <kbd>Cmd</kbd>+<kbd>G</kbd>.

## Supported versions

USA 1.3.0 is the version this port is written against.
A matching 1.3.0 update is the full support path for a standalone USA or Europe disc.
The USA and Europe base discs alone (title version 0) are playable.
Combo packs are title version 0 and have no separate update folder.

| Region | Title ID | Title version | Status |
|--------|----------|---------------|--------|
| USA | `00050000-10101d00` | 64 (1.3.0) | Supported |
| USA base disc | `00050000-10101d00` | 0 | Supported |
| Europe | `00050000-10101e00` | 80 (1.3.0) | Supported |
| Europe base disc | `00050000-10101e00` | 0 | Supported |
| USA (NSMBU + NSLU) | `00050000-1014b700` | 0 | Partially Supported |
| Europe (NSMBU + NSLU) | `00050000-1014b800` | 0 | Partially Supported |
| Japan | `00050000-10101c00` | — | Not supported yet |

## Install

Download the release for your system from the [Releases](https://github.com/nsmbu-recomp/nsmbu-recomp/releases) page.
Put it in a folder you can write to.
Start the app.
The first start opens the installer.
Each later start launches the game.
Which file to open, and what the system requires, is on the page for your system under [docs/user](docs/user/).

### Choose your game

The installer accepts the versions in the table above.
Give it one of these:

1. **Disc image.**
   Use a `.wux`, `.wud`, or raw `.iso` file and its keys.
   A matching 1.3.0 update is optional.
2. **Cemu archive.**
   Use a `.wua` file.
   This option needs no keys.
3. **Extracted folder.**
   Use a folder with `code`, `content`, and `meta`.
   A 1.3.0 update merged into that folder is optional.

The installer asks you to pick that file or folder.

### Keys

Skip this step if you chose a Cemu archive or an extracted folder.

A disc image needs its disc key and the Wii U common key.
Type or paste each key, or point at a key file.
You can also add a matching 1.3.0 update.
Key file formats are in [docs/user/keys.md](docs/user/keys.md).

### Wait for the build

The installer prepares the game on your computer.
A disc image or a Cemu archive is extracted into the release folder.
An extracted folder stays where it is.
The installer then builds the native game.

The first start takes a few minutes.
When it finishes, start the app again to play.

## Command line

These options belong to the game program.
A release keeps that program in `data/bin/nsmbu` (`nsmbu.exe` on Windows).
On macOS the same program is `NSMBU.app/Contents/MacOS/nsmbu`.
`-h` or `--help` prints this list and exits.

- `--game DIR` sets the folder that holds `code`, `content`, and `meta`.
  The default folder is `game`.
- `--save DIR` sets the save folder.
  The default folder is `nsmbu/save`.
- `--open W-C` opens that course and skips the intro.
  `W-C` is one world digit and a course number, such as `1-1`.
- `--renderer API` chooses `metal` or `vulkan` for this start.
  `--renderer=vulkan` is the same option.
- `--trace` writes each host library call to the log.
- `--warm-shaders` fills the Metal shader cache and then exits.
  With another renderer, the program prints an error and exits.
- `--renderer-smoke` starts Vulkan, runs its smoke test, and then exits.

## More

- [Feature guides](docs/features/)
- [Keys](docs/user/keys.md)
- [Repair or change the game](docs/user/repair.md)
- [Where files live](docs/user/files.md)
- [If the system blocks the app](docs/user/blocked-app.md)
- [Terminal installer](docs/user/terminal-installer.md)
- [Logs for bug reports](docs/user/logs.md)
- [Install and platform guides](docs/user/)

## Contributing

Build from source, CLI commands, and developer docs are in [CONTRIBUTING.md](CONTRIBUTING.md).

## Notes

This repository was forked from the [Wind Waker HD recompilation](https://github.com/ZeldaWWHDRecomp/ZeldaWWHDRecomp).
The Collision Viewer is based on the collision viewer from [Tsuru](https://github.com/Zenith-Team/Tsuru).
The headers and the decompilation are from [aboood40091/NSMBU-Headers](https://github.com/aboood40091/NSMBU-Headers).

> [!NOTE]
> This project is made with the help of AI agents.
# New Super Mario Bros. U: Native Port / Recompilation

This project compiles Wii U game assets into a standalone, native executable.  
The release package includes the necessary runtime, build tools, and installer script.  
You must supply your own copy of the game files, disc image, and encryption keys.  

> [!IMPORTANT]
> There is no Nintendo code, no assets and no game data anywhere in this project or its releases.  
> You need your own legally dumped copy of the game.  
> Nothing in this repo, in any release, or in the build process contains or downloads Nintendo IP.  

## Controls

Press `` ` `` or `F12` to open or close the settings overlay.

On a controller, press **Home** or click the right stick (**R3**), or press **Start** and **Select** together (Xbox **Menu** + **View**, Wii U **+** and **−**).

Hold **Select** (−) for half a second to open Settings when the overlay is closed; press **Select** again to close it.

With the default keyboard layout, press **Enter** and **Tab** together for the same menu.

On Android, the system **Back** gesture also opens and closes Settings; see [New Super Mario Bros. U on Android](docs/android.md).

While Settings is open, touch the menu directly or use a controller: D-pad or left stick to move, **A** to activate, **B** to go back, **LB**/**RB** for tabs, **Start+Select** to close.

**F1**–**F5** load save states; **Shift+F1**–**F5** save.
Open **Audio** for mute, volume, and TV / GamePad output.  
Open **Controls** to change keys and controller buttons.  

A release stores the mapping in `data/nsmbu/controls.json`.
A build from source stores it in the port config directory.
That directory is `~/.config/nsmbu` on Linux, `%APPDATA%\NSMBU` on Windows, and `~/Library/Application Support/NSMBU` on macOS.
Set `NSMBU_CONTROLS` to use another file.

Up to four controllers work as separate players.
Player 1 uses the keyboard and the first controller.
Each extra controller joins as the next player.
In **Controls**, keep **Same controls for every player** on to use one layout for every controller.
Turn it off to bind buttons for one player at a time.
In GamePad mode Player 1 is the Wii U GamePad and extra controllers act as Wii U Pro Controllers.
In Pro Controller mode every controller acts as a Wii U Pro Controller.

Host gamepads use the same layout as the Wii U GamePad by default (face buttons by position).
The bottom face button (Xbox **A**) is Wii U **B**.
The right face button (Xbox **B**) is Wii U **A**.
Controller labels below use Xbox names.

Default layout:

| Keyboard | Controller | Wii U | In game |
|----------|------------|-------|---------|
| W A S D | left stick | left stick | walk / move |
| arrow keys | right stick | right stick | camera |
| K or Space | B (right) | A | jump |
| J | A (bottom) | B | jump |
| L | Y (top) | X | run / hold item |
| Left Shift | X (left) | Y | run / hold item |
| Q | LB | L | map view |
| E | RB | R | map zoom |
|  | LT | ZL | spin jump |
| Left Control | RT | ZR | spin jump |
| Enter | Menu | + | pause |
| Tab | View | − | inventory |
| H | Home | Home | Home menu |
| 1 2 3 4 | D-pad | D-pad | move / crouch / pipes |
| X / V | L3 / R3 | stick click | stick click |


## Compatibility

Addresses in the repo are USA 1.3.0.
Europe, the USA and Europe base discs, and the NSMBU + NSLU packs use USA→build maps ([docs/builds.md](docs/builds.md)).
Standalone USA and Europe discs should use their matching 1.3.0 update for full support.
The USA and Europe base discs alone (title version 0) are playable and support mods.
Combo packs are title version 0 (no separate update folder) and support mods.

| Region | Title ID | Title version | Status |
|--------|----------|---------------|--------|
| USA | `00050000-10101d00` | 64 (1.3.0) | Supported and playtested |
| USA base disc | `00050000-10101d00` | 0 | Supported; mods not supported |
| Europe | `00050000-10101e00` | 80 (1.3.0) | Supported |
| Europe base disc | `00050000-10101e00` | 0 | Supported; mods not supported |
| USA combo (NSMBU + NSLU) | `00050000-1014b700` | 0 | Supported |
| Europe combo (NSMBU + NSLU) | `00050000-1014b800` | 0 | Supported |
| Japan | `00050000-10101c00` | — | Not supported yet |

## Install

### 1. Download and start

Download the zip for your system from the [Releases](https://github.com/nsmbu-recomp/nsmbu-recomp/releases) page.
Unzip it into a folder that you can write to.
Start **NSMBU**:

| System | File to start |
|--------|----------------|
| macOS, Apple Silicon, macOS 14 or newer | `NSMBU.app` |
| Windows, x86-64, Windows 10 or 11, Vulkan | `NSMBU.exe` |
| Linux, x86-64, glibc 2.35 or newer, Vulkan | `nsmbu-launcher`  |
| Linux, arm64, glibc 2.35 or newer, Vulkan | `nsmbu-launcher`   |

On Android, install the APK instead and follow [New Super Mario Bros. U on Android](docs/android.md).

The first start opens the installer.
Each later start launches the game.

### 2. Choose your game copy

The installer accepts USA or Europe NSMBU at version 1.3.0, the matching base disc, or either NSMBU + NSLU pack at version 0.
USA is title `00050000-10101d00`, title version 64, or base disc title version 0.
Europe is title `00050000-10101e00`, title version 80, or base disc title version 0.
The USA combo pack is title `00050000-1014b700`, title version 0.
The Europe combo pack is title `00050000-1014b800`, title version 0.

Choose one of these:

1. **Disc image.**
   Use a `.wux` or `.wud` file and its keys.
   The matching **1.3.0** update folder is optional.
2. **Cemu archive.**
   Use a `.wua` file that already has that title version.
   This option needs no keys.
3. **Extracted folder.**
   Use a game folder with `code`, `content`, and `meta`.
   A 1.3.0 update merged into that folder is optional.

The installer opens a file window so you can pick that file or folder.
macOS and Windows use the system file chooser.
Linux uses Zenity or KDialog when one of those programs is installed.
If no file window opens, type the path.
You can also drag the file into the window and press Enter.

### 3. Disc image keys and update

Skip this step if you chose a Cemu archive or an extracted folder.

A disc image also needs:

- the disc key
- the Wii U common key
- an optional matching 1.3.0 update

On the Keys screen, type or paste each key's 32 hex digits into its own field.
The disc key belongs to that disc image.
The common key is the same on every Wii U.

Put the update in an `update/` folder next to the disc image (`code/`, `content/`, `meta/`).
You can also choose that folder in the installer.

Key files are optional.
If a `.key` file with the same name as the image sits next to the image, the installer uses it.
If `common.key` sits next to the image, the installer uses that too.
You can also choose a key file.
A key file can be 16 raw bytes, one line of 32 hex digits, or a Cemu `keys.txt`.
The text field hides the key.
Keys stay in memory until extraction starts.
The setup log never stores keys.

### 4. Wait for the build

The installer prepares the game on your computer.
For a disc image or a Cemu archive, it extracts the files into the release folder.
An extracted folder stays where it is.
The installer then translates the code to C, compiles it, and links it with the runtime.

The first start takes a few minutes.
When it finishes, start **NSMBU** again to play.

### Repair or change the game

To repair, update, or choose a different game file, open the installer again:

- **macOS or Windows** — hold Shift while you start **NSMBU**.
- **Linux** — run `nsmbu-launcher --setup`, or use the Setup action on `NSMBU.desktop`.

### Where files live

The release folder is portable.
The built game, game files, saves, and settings stay in `data/` inside that folder.
Keep `NSMBU.app` inside the unzipped folder on macOS.
Move the whole folder when you want the game in another place.

Run, crash, and setup logs for bug reports are listed in [docs/logs.md](docs/logs.md).
A portable release keeps them under `data/nsmbu/logs/` and `data/setup.log`.

### If the system blocks the app

**macOS** — the app is unsigned.
On macOS 14, right-click `NSMBU.app`, choose **Open**, then choose **Open** again.
On macOS 15 and newer, open **System Settings → Privacy & Security**, then choose **Open Anyway**.

**Windows** — SmartScreen can block an unsigned program.
Choose **More info**, then **Run anyway**.

### Terminal installer

Use the terminal installer when the window cannot run:

| System | Command |
|--------|---------|
| macOS | `tools/Setup in Terminal.command` |
| Windows | `tools/Setup in a console window.bat` |
| Linux | `tools/setup-in-terminal.sh` |

It asks the same questions.
It opens the same file window when the system provides one.

## Contributing

Build from source, CLI commands, and developer docs are in [CONTRIBUTING.md](CONTRIBUTING.md).

## Notes

This repository was forked from the [Wind Waker HD recompilation](https://github.com/ZeldaWWHDRecomp/ZeldaWWHDRecomp).

The Collision Viewer is based on the collision viewer from [Tsuru](https://github.com/Zenith-Team/Tsuru).

This repository contains no Nintendo property.  
It has no game code, no game assets, and no keys from Nintendo.  

>  [!IMPORTANT]
> This project is made with the help of AI agents.

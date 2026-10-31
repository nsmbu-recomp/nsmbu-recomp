# iOS and iPadOS

The iOS build is the Metal renderer in a UIKit app.
It runs on iOS / iPadOS 17 or newer, arm64.
The recompiled game is built into the app, so the app is built on a Mac for one game version.

## Build

1. Set up the game on the Mac as for the macOS build ([game/README.md](../game/README.md)), with `--recomp`.
   That fills `build/gen`.
2. Run `ios/build_ios.sh`.
   It writes `ios/build/NSMBU.app` and an unsigned `ios/build/New Super Mario Bros. U.ipa`.
   The home screen icon is made from the first PNG in `static/` (or `ICON`).
3. Sign and install the `.ipa` with a signing service or tool (Signulous, AltStore, Xcode device install).

| Variable | Default | Meaning |
|----------|---------|---------|
| `GEN_DIR` | `build/gen` | recompiler output |
| `OUT` | `ios/build` | build directory |
| `IOS_MIN` | `17.0` | deployment target |
| `CPU` | `apple-a14` | `-mcpu` of the recompiled game; `apple-a12` runs on every iOS 17 device |
| `SIGN` | unset | `codesign` identity; `-` signs ad hoc with `ios/nsmbu.entitlements` |
| `SIM` | unset | `1` builds for the iOS Simulator (`ios/build-sim`, no `.ipa`) |
| `ICON` | first `static/*.png` | square PNG for the home screen icon |
| `IPA_NAME` | `New Super Mario Bros. U` | name of the `.ipa` |

## Entitlements

On a device the guest's 4 GiB address space is reserved at `0x800000000` (`runtime/include/ppc.h`).
The Simulator runs as a Mac process that cannot map that low; it uses the macOS address.
iOS gives an app that much address space only with `com.apple.developer.kernel.extended-virtual-addressing`.
The profile that signs the app must grant it.
Without it the app stops at start with "cannot reserve guest address space".
`com.apple.developer.kernel.increased-memory-limit` raises the memory the system lets the app use.
Both are in `ios/nsmbu.entitlements`.

## Game files

Copy the extracted game (`code`, `content`, `meta`) into the app's Documents as a folder named `game`.
Use the Files app (On My iPhone / iPad › NSMBU) or Finder (the device › Files › NSMBU).
The app's working folder is Documents:

| Path in Documents | Contents |
|-------------------|----------|
| `game/` | the game |
| `nsmbu/save/` | game saves |
| `NSMBU/` | logs, controls, save states, screenshots |
| `env.txt` | optional `NSMBU_*=value` lines, read at start |

Without a usable `game` folder the app shows what is missing and quits.

## Controls

On-screen controls are drawn over the picture.
They act as player 1's controller, with Wii U button labels.
The stick appears where a finger lands on the left side of the screen.
A finger that slides from one button to another presses the button it is on.
They hide while a game controller is connected.
`NSMBU_TOUCH_CONTROLS=1` always shows them, `0` always hides them.

The two buttons at the top centre open the settings overlay (⚙) and switch the GamePad picture (picture-in-picture, GamePad only, off).
While the settings overlay is open, one finger is its mouse.
A touch on the GamePad picture is a GamePad touch.

Game controllers work as on macOS (GameController framework).
A hardware keyboard uses the keyboard layout and hotkeys of the macOS build.

## Differences from macOS

- The GamePad picture has no window of its own. It starts off; picture-in-picture goes in the top right corner.
- A GPU without BC texture compression (the Simulator, some iPhones) gets BC1–5 textures decoded to RGBA8 on upload.
- The game pauses whenever the app is not active, because iOS does not let apps use the GPU in the background.
- Presentation always waits for the display, so the uncapped debug mode only uncaps the game.
- Changing the renderer or restarting from the overlay is not available.
- A fatal error offers no crash report link; the log is in `NSMBU/logs`.

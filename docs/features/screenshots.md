# Screenshots

The port saves PNG files of the TV picture.
You can also save the GamePad picture when it is visible.

## Take a screenshot

Press <kbd>F10</kbd> by default.
Change the binding in **Settings → Controls**.

The file uses the **internal resolution**, not the size of the window on screen.
The settings menu is not in the picture.

In **Settings → Saves → Screenshots** the note lists the key or controller button bound for screenshots.

## GamePad picture

Turn on **Also save the GamePad screen (while it is shown)** in **Settings → Saves**.
A second file named `…_GamePad.png` is written when the GamePad picture is on screen in its window or in the overlay.

## Folder

Screenshots go to a `screenshots` folder next to your other port data.

| How you run the game | Folder |
| --- | --- |
| Portable release (zip with `portable.txt`) | `<release>/data/nsmbu/screenshots/` |
| Linux AppImage | `~/.config/nsmbu/screenshots/` |
| Non-portable Windows | `%APPDATA%\NSMBU\screenshots\` |
| Non-portable macOS | `~/Library/Application Support/nsmbu/screenshots/` |
| Built from source | `<repo>/nsmbu/screenshots/` |

**Open screenshots folder** in **Settings → Saves** opens that folder when the system allows it.

`NSMBU_SCREENSHOT_DIR` writes screenshots somewhere else for one start.
`NSMBU_SCREENSHOT_GAMEPAD=1` turns on the GamePad file for one start.
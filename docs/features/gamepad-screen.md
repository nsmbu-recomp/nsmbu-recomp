# GamePad screen

The Wii U drew the touch screen on the GamePad.
The port can show that picture in its own window, over the TV picture, or not at all.

## Choose a mode

Open **Settings → Display → GamePad screen**.

| Mode | What you see |
| --- | --- |
| Separate window | The GamePad picture in another window |
| Picture-in-picture | The GamePad picture in a corner of the TV window |
| Automatic picture-in-picture | The picture appears when a GamePad menu opens, then hides again |
| Off | The GamePad picture stays hidden while the game still runs |
| GamePad only | Only the GamePad picture fills the main window |

**Show GamePad screen** turns the current mode on or off without changing the mode.

In automatic mode the overlay stays up for a few seconds after a big change on the GamePad screen, such as a menu opening.

## Picture-in-picture placement

When the mode is picture-in-picture or automatic, **Settings → Display** also has:

- **Corner** — top left, top right, bottom left, or bottom right
- **Size** — 20%, 25%, 33%, or 40% of the TV picture width
- **Opacity** — 100%, 85%, 70%, or 50%

A click or touch on the GamePad picture is a GamePad touch.
That works in the separate window and in the overlay on the TV picture.

## Shortcut

Press <kbd>Ctrl</kbd>+<kbd>G</kbd> to show or hide the GamePad screen.
On macOS, use <kbd>Cmd</kbd>+<kbd>G</kbd>.

## Android and iOS

On phones and tablets the GamePad picture has no separate window.
Picture-in-picture and **Off** are the usual choices.
On iOS the picture starts off and picture-in-picture sits in the top right corner.
See [Android](../user/android.md) and [iOS](../user/ios.md).

## Launch override

`NSMBU_DRC_MODE` sets the mode for one start.
The values match the list in **Settings → Display**.
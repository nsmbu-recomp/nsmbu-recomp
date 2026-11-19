# Course pause menu Settings

The feature adds a Settings button to the course pause menu.

## Rows

The story pause menu has three rows: Continue, Quit, and Change Controller.
This feature keeps Change Controller in the layout and hides it.
A new Settings button sits on that same row.
The blue panel keeps its original height.

## Open

With `NSMBU_INGAME_SETTINGS=1`, A or Enter on Settings opens the port settings overlay and hides this pause menu.
The controller window stays closed.
The course stays paused.

## Close

Closing the overlay shows this pause menu again.
The course stays paused.
Continue returns to the course.
Settings opens the overlay again.

## Option

Settings, Debug has a checkbox named "Settings button on the pause menu".
That checkbox starts on.
The game builds the menu when the course loads.
Leave the course and enter it again after you change the option.

## World map

The world-map start menu has its own Settings button.
That button stays off until the player turns it on.
`NSMBU_INGAME_SETTINGS=1` is also required.
Unset and `0` keep that button off.

## Flag

`NSMBU_INGAME_SETTINGS=1` makes the course pause Settings button open the overlay and hide that menu.
Closing the overlay shows the pause menu again.
The variable is off when it is unset or `0`.
The button then keeps the game's controller window.

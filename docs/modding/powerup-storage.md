# Power-up storage

The example package was removed.
`PlayerObject::setItem` (`0x02931C1C`) is not hooked.
The game draws the stock item from `Common.szs` (`ItemList`) on each player through the `Multi2D` at `PlayerObject+0x1804` (`f_0252F900` / `f_0252FA90`).
`PlayerObject::setItem` already receives the actor and the new mode.

## Rules

- You are Small: the pickup applies normally.
- You are Super or better and the bubble is empty: the pickup goes into the bubble and your form stays.
- The bubble is already full, including an item brought from the map: the pickup follows the game’s normal rules.
- Using the bubble is the game’s control (Up on the +Control Pad, and the GamePad item list).
- The bubble’s lifetime is the game’s stock-item lifetime, including death and doors.
- A mode the bubble cannot show is not intercepted.

`use_button` and `show_hud` stay out of the example.
## API

These calls are not in the runtime yet:

- A switch that turns the rule above on for every player.
- A read of the stored mode, empty when the bubble is empty.
- A write that sets or clears that player’s bubble.

## Runtime

Decompile the stock field and the item-type values the `ItemList` panes use, and record them under `vendor/decomp`.
Hook `setItem`.
When store-extra is on, the player is Super or better, the bubble is empty, and the mode has a stock type: write the stock field and skip the original call.
Otherwise call `f_02931C1C_orig`.
Do not keep a second copy of the stored mode in the runtime.

## Example

There is no example package.
When the hook exists, player-facing notes are: the reserve is the game’s icon above you, you use the game’s stock control, and an item you already brought is left alone.

## Check

After the stock field is known, one headless `nsmbu` script checks three cases on player 1:

- Small applies a mushroom.
- Super with an empty bubble stores a flower and stays Super.
- A second pickup with a full bubble changes form and leaves the bubble.

## Work

1. Decompile the per-player stock field, item types, and ItemList update path.
   Name them in `vendor/decomp`.
2. Add `set_store_extra`, `stock`, and `set_stock` on the Player API and implement them in the `setItem` hook.
3. Add the powerup-storage example as the store-extra switch, with no HUD or use-button options, and update the README and changelogs.
4. Run the headless check above.

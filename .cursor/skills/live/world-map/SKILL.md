---
name: world-map
description: "Open the world map, move between nodes, and use the start menu. Use when the check is the world map, the overworld, Acorn Plains, scene=CourseSelect, or the start menu."
---

# World map

The world map is `scene=CourseSelect` or `scene=Map` with `map=1`.
The repo save starts on node `W1M0` in world 0 (Acorn Plains).
Status names that node in `name=`.
`goto` with `target` `map` applies save file 0 and opens the map.
From `scene=Menu`, one A confirms file 0 and opens the same map.
The `file-select` skill owns that menu.

`A` on `W1M0` stays on the map.
One stick-right reaches `W101`.
`A` on `W101` enters course `1-1`.
The `course` skill owns that enter and the quit back to the map.

This boot keeps `nsmbu/settings.ini` (frame cap, resolution, present mode).
Leave `--interp-fps`, `--res-scale`, `--vk-present-mode`, and `--hidden-windows` off when the check is that session.

`python3 tools/run/launch.py -- --mcp` uses the repo `nsmbu/` settings and the active dump.
That is the one live game.
The control files live under `.tmp/agent/`.
A `*-base` dump only boots.
A scene check needs a `*-1.3.0` dump, such as `usa-1.3.0`.
If a game is already running, that game is already up.
Call `status` on it.
`session_close` stops it.
Close it before a build that relinks the executable.

```bash
python3 tools/run/launch.py -- --mcp
```

Then `goto` with `target` `map`, then `status`.

`goto` with `target` `map` waits until the map is current.
Done when status shows `map=1` and `scene=CourseSelect` or `scene=Map`.
The map iris can still be up on that line.
A picture then misses Map View and Items.
Wait until those labels are drawn, then call `screenshot` with `path` `.tmp/repro/overworld.png`.
On a 21:9 map the world name reads Acorn Plains.
A map with no Luigi U add-on can show "Check out New Super Luigi U" and an eShop question.
No stores that notice in the save.
It does not mount the add-on.

A quit from course `2-1` stands on `W201` (Stone-Eye Zone).
The picture can still be the world-2 entrance `W2M0`: the pond, the island, and the stump, with Mario at the right edge.
Read `name=` for the node.

## Start menu

The start menu is `CourseSelectMenu`.
It is not the in-level pause menu.
The `pause-menu` skill owns that menu.
Plus opens the start menu.
Plus closes it.
Plus during the map iris leaves the map up and the menu closed.
`status` includes `screen=start-menu` and the selected button.
`ui` lists the buttons and the selected name.
`wait` with `cursor` `Settings` waits for that button.

The five game buttons are Star Coins, Number of Players, and Change Controller on the top row.
Quick Save and Main Menu sit on the bottom row.
From Quick Save, Up selects Star Coins and Down selects the button guide.
Down from Number of Players selects Main Menu.
From Settings, Up selects Change Controller and Left selects Main Menu.
Inside Change Controller, Down stays on Switch to GamePad.
Settings is a sixth tile under Change Controller when Settings, Debug, Settings button on the start menu is on.
The game builds that tile when the map loads.
Leave to the title, then come back, before you look for it.
A on Settings opens the port overlay.
The six buttons stay hidden while that overlay is open.

Walk to the first course node with:

```text
stick { "x": 1, "y": 0, "frames": 40 }
status
```

Done when status shows `name=W101`.
The only unlocked neighbour of `W101` is back to `W1M0`.
Stick left for that return.

Hidden map boots often pass `--hidden-windows`, `--vk-present-mode=immediate`, and `--res-scale=1` (see [automation.md](../../../docs/developer/automation.md)).
Use it only for a log that must match `docs/developer/automation.md`.
The press list there is the same map, measured in TV frames.

A check that must keep passing is a playtest case.
The `cli` skill owns playtests.
When the check is controller input you cannot script, `docs/developer/replay-format.md` has the line layout.

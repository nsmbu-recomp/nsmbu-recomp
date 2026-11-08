---
name: world-map
description: "Open the world map, move between nodes, and use the start menu. Use when the check is the world map, the overworld, Acorn Plains, scene=CourseSelect, or the start menu."
---

# World map

The world map is `scene=CourseSelect` or `scene=Map` with `map=1`.
The repo save starts on node `W1M0` in world 0 (Acorn Plains).
Status names that node in `name=`.
`nsmbu goto map` applies save file 0 and opens the map.
From `scene=Menu`, one A confirms file 0 and opens the same map.
The `file-select` skill owns that menu.

`A` on `W1M0` stays on the map.
One stick-right reaches `W101`.
`A` on `W101` enters course `1-1`.
The `course` skill owns that enter and the quit back to the map.

This boot keeps `nsmbu/settings.ini` (frame cap, resolution, present mode).
Leave `NSMBU_INTERP_FPS`, `NSMBU_RES_SCALE`, `NSMBU_VK_PRESENT_MODE`, and `NSMBU_HIDDEN_WINDOWS` unset when the check is that session.

`nsmbu open` uses the repo `nsmbu/` settings and the active dump.
A `*-base` dump only boots.
A scene check needs a `*-1.3.0` dump, such as `usa-1.3.0`.
If open reports a game already running, read `nsmbu status` or run `nsmbu close`, then open again.
`nsmbu bench` and `nsmbu game … --recomp` also refuse while that game is running.

```bash
nsmbu open
nsmbu goto map
nsmbu status
```

`nsmbu goto map` waits until the map is current.
Done when status shows `map=1` and `scene=CourseSelect` or `scene=Map`.
The map iris can still be up on that line.
A picture then misses Map View and Items.
Wait until those labels are drawn, then save `nsmbu screenshot .tmp/repro/overworld.png`.
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
`nsmbu ui` lists the buttons and the selected name.
`nsmbu wait --cursor Settings` waits for that button.

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

```bash
nsmbu stick 1 0 --frames 40
nsmbu status
```

Done when status shows `name=W101`.
The only unlocked neighbour of `W101` is back to `W1M0`.
Stick left for that return.

Hidden map boots often set `NSMBU_HIDDEN_WINDOWS=1`, immediate present, and resolution scale 1 (see [automation.md](../../../docs/developer/automation.md)).
Use it only for a log that must match `docs/developer/automation.md`.
The press list there is the same map, measured in TV frames.

A check that must keep passing is a playtest case.
The `cli` skill owns playtests.
When the check is controller input you cannot script, `docs/developer/replay-format.md` has the line layout.

# Replays

`.replay` files here are text input logs.
`nsmbu record <course> [name]` writes one and `nsmbu replay <name>` plays it.

A file has initial config lines, then one line for each pad change.

## Initial config

- `version=1` — format marker, keep it first
- `course=1-1` — course to launch
- `origin=control` — start steps when the player gets control
- `exit=none` — keep running on live input after the last line
- `player=0` — player index `0`–`3`
- `lives=5` — lives at step 0
- `player_coins=0` — coins for that player, at step 0
- `coins=0` — course-wide coin total, at step 0
- `mode=fire` — `small`, `normal`, `fire`, `mini`, `propeller`, `penguin`, `ice`, `squirrel`, `psquirrel`

## Per step config

```
<step> <buttons> <lx> <ly> <rx> <ry> <touch> <tx> <ty> [<x> <y> <z>]
```

- `<step>` — simulation tick, not a frame
- `<buttons>` — `-` for none, or `+`-joined names such as `A+Y`
- `<lx> <ly>` — left stick, `-1` to `1`
- `<rx> <ry>` — right stick, `-1` to `1`
- `<touch>` — `0` or `1`
- `<tx> <ty>` — touch point
- `<x> <y> <z>` — player position, optional

Button names: `A`, `B`, `X`, `Y`, `L`, `R`, `ZL`, `ZR`, `Plus`, `Minus`, `Home`, `DpadUp`, `DpadDown`, `DpadLeft`, `DpadRight`, `LeftStickClick`, `RightStickClick`.

The last line holds until the next line.
The replay quits when the last line has played, unless `exit=none` is set.

Full format and legacy directives: [../docs/developer/replay-format.md](../docs/developer/replay-format.md).

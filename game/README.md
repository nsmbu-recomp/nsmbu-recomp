# `game/` — your copies of NSMBU

Your Wii U dumps, keys, and extracted files live here.
Nothing here except this readme is in git.

## Quick start

```bash
nsmbu game
```

1. Drop a `.wux` (or `.wud`) into `game/`.
2. Put keys in `game/keys.txt` (Cemu format).
3. Run `nsmbu game`.
   It renames the image to the standard USA/Europe name, extracts into a version folder, and can recomp.

`nsmbu game usa-1.3.0` activates that folder without the menu.
Add `--recomp` to refresh `build/gen`.

## Layout

```text
game/
  active                                              # e.g. usa-1.3.0
  keys.txt                                            # Cemu keys (common= + title ids)
  New Super Mario Bros. U (USA).wux                   # disc image
  New Super Mario Bros. U (Europe).wux
  usa-base/                                           # extract only
    code/ content/ meta/
  usa-1.3.0/
    code/ content/ meta/
    update/                                           # optional 1.3.0 update merge
  eu-base/
    code/ content/ meta/
  eu-1.3.0/
    code/ content/ meta/
```

Version folders only need `code/`, `content/`, and `meta/` to play.
Disc images stay at the `game/` root.
Recommended ids: `usa-base`, `usa-1.3.0`, `eu-base`, `eu-1.3.0`, `nsmbu-nslu-usa`, `nsmbu-nslu-eu`.

`*-base` is title version 0.
`*-1.3.0` is the base extract with the update applied (mods).

## Keys

One file: `game/keys.txt`.

```text
common = <32 hex digits>
0005000010101d00 = <USA disc key>
0005000010101e00 = <Europe disc key>
```

`WIIU_COMMON_KEY` can supply the common key instead of `common=`.

## Updates

Optional matching **1.3.0** update as `game/<id>/update/` (`code/`, `content/`, `meta/`), applied on extract.

USA 1.3.0 is title version **64**.
Europe 1.3.0 is title version **80**.
A base disc alone (title version 0) is playable.

## More

See [CONTRIBUTING.md](../CONTRIBUTING.md) and [docs/developer/nsmbu/port.md](../docs/developer/nsmbu/port.md).
See [docs/developer/builds.md](../docs/developer/builds.md) for address maps.

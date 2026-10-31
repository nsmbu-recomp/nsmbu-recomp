# Build maps

Addresses in this repository are for the **USA** build.
The title is `00050000-10101d00`.
The executable is `code/red-pro2.rpx` from USA 1.3.0 (title version 64).
Those addresses are the *canonical ids* of the game's functions and globals.

| Build | Title | Title version | Mods | `red-pro2.rpx` SHA-256 | Map |
| --- | --- | --- | --- | --- | --- |
| USA (canonical) | `00050000-10101d00` | 64 | yes | `ebd147ce…dc8e` | the identity |
| USA base disc | `00050000-10101d00` | 0 | no | `ec31a785…5069` | [`tools/recomp/builds/usa_v0.json`](../tools/recomp/builds/usa_v0.json) |
| Europe | `00050000-10101e00` | 80 | yes | `0ea59f40…ea51` | [`tools/recomp/builds/eu.json`](../tools/recomp/builds/eu.json) |
| Europe base disc | `00050000-10101e00` | 0 | no | `2f4f2da5…05c9` | [`tools/recomp/builds/eu_v0.json`](../tools/recomp/builds/eu_v0.json) |
| USA + Luigi U | `00050000-1014b700` | 0 | yes | `1031c741…2b05` | [`tools/recomp/builds/nsmbu_nslu_usa.json`](../tools/recomp/builds/nsmbu_nslu_usa.json) |
| Europe + Luigi U | `00050000-1014b800` | 0 | yes | `1faa7ce3…a285` | [`tools/recomp/builds/nsmbu_nslu_eu.json`](../tools/recomp/builds/nsmbu_nslu_eu.json) |

A standalone base disc is title version 0.
`nsmbu extract` and the installer apply a matching 1.3.0 update folder when present, then check the RPX hash.
Without an update, a known base-disc RPX (USA_V0 or EU_V0) is allowed; the Mods tab explains that mods are unsupported on that build.
An NSMBU + NSLU pack is already a complete title at version 0 and supports mods.

## Regional and pack builds

Europe is the same program as USA 1.3.0 with a few language-related functions compiled differently.
Each combo pack is that program plus about two thousand NSLU functions inserted in the executable.
A **build map** turns a canonical USA address into the other build's address so one set of hooks and one runtime serve them.

Derive or re-check a map with `tools/recomp/mkbuildmap.py` from your own dumps, then commit the JSON only:

```sh
python3 tools/recomp/mkbuildmap.py usa/code/red-pro2.rpx eur/code/red-pro2.rpx \
    --name EU --title 0005000010101e00 --title-version 80 --out tools/recomp/builds/eu.json
python3 tools/recomp/mkbuildmap.py usa-1.3/code/red-pro2.rpx usa-base/code/red-pro2.rpx \
    --name USA_V0 --title 0005000010101d00 --title-version 0 \
    --out tools/recomp/builds/usa_v0.json
python3 tools/recomp/mkbuildmap.py usa-1.3/code/red-pro2.rpx eur-base/code/red-pro2.rpx \
    --name EU_V0 --title 0005000010101e00 --title-version 0 \
    --out tools/recomp/builds/eu_v0.json
python3 tools/recomp/mkbuildmap.py usa/code/red-pro2.rpx combo-usa/code/red-pro2.rpx \
    --name NSMBU_NSLU_USA --title 000500001014b700 --out tools/recomp/builds/nsmbu_nslu_usa.json
python3 tools/recomp/mkbuildmap.py usa/code/red-pro2.rpx combo-eu/code/red-pro2.rpx \
    --name NSMBU_NSLU_EU --title 000500001014b800 --out tools/recomp/builds/nsmbu_nslu_eu.json
```

Japan (`00050000-10101c00`) has no map yet.
When the running build is Europe or a Europe combo pack, its own text files are used.

## How the port uses the registry

- **The recompiler** identifies `red-pro2.rpx` by SHA-256, maps hook addresses, and names every generated function by its canonical address when the function exists in USA.
  Functions only present in a pack build get a distinct symbol id so names never collide.
- **The runtime** uses `GC(…)` / `GD(…)` ([runtime/include/guest_addr.h](../runtime/include/guest_addr.h)).
  For the USA build the map is the identity.
- **A hooks file** may say which builds it is for (`# builds: USA`).
  An entry wrap on a function whose body differs between builds is allowed.
  The recompiler warns so you can check it.
  Write `same` after that address once the replacement applies to every body.

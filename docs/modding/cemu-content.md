# Native Cemu content packs (levels and file replacements)

Status: **live**.
This covers large Cemu file-only packs that replace game content (levels, actors, layouts, sound).
An example is a pack such as Cloudy SMBU 2: a pack root with `rules.txt`, a `content/` tree, and optional Cemu `meta/` title art.

The Mods tab has no import UI.
Players copy package folders into the mods directory ([mod-manager.md](mod-manager.md)).
On scan, the port detects a file-only Cemu content pack from Definition-only `rules.txt` and the `content/` tree and keeps synthesized package metadata in memory only.
See live file-replacement behaviour in [content.md](content.md) and graphics-only Cemu packs in [cemu.md](cemu.md).

## Player workflow

1. Open **Mods → Open mods folder**.
2. Drop the **pack root** into `mods/` (folder name may differ from the package id).
3. Leave any Cemu `meta/` title art in place; the host ignores it.
4. Do not add a hand-written `manifest.json` unless you want to override id or display fields.
5. Enable the package and restart.
6. Reads under `/vol/content/` use the replacements until you disable and restart again.

Graphics and shader packs stay on `kind: cemu` ([cemu.md](cemu.md)).
Scripted mods stay on `kind: lua` ([lua-mods.md](lua-mods.md)).

## Detection (no player JSON)

When scanning `mods/<entry>/`, classify as follows.

1. If the tree has a valid `manifest.json` with `kind` / `content_dir` / `lua` / `cemu`, keep the existing package path.
2. Else if `rules.txt` has graphics sections (`[Texture…]`, `[Control…]`, `[Preset…]`, shaders, and so on), treat it as `kind: cemu` and synthesize the graphics manifest ([cemu.md](cemu.md)).
3. Else if `rules.txt` is Definition-only (or Definition plus ignored non-load sections) **and** exactly one `content/` tree exists under the pack, treat it as a **Cemu content pack**.
4. Else leave the folder as a broken package with a clear problem string (same as a missing manifest today).

For step 3 the host synthesizes manifest fields in memory.
It does not create or update `manifest.json` in the player's pack folder.

Fields from `[Definition]`:

| `rules.txt` | Generated manifest |
| --- | --- |
| `name` | `name` |
| `version` (Cemu pack version integer or string) | `version` as three-part semver when possible; else `1.0.0` with the raw value in `description` |
| `titleIds` | Must include USA NSMBU `0005000010101d00` (same check as today) |
| (none) | `id` = the folder name when it is already a valid id; else a `content.` slug from `name` |
| (none) | `kind` = `lua`, `content_dir` = relative path to the detected `content/` |
| `description` | Optional `description` |

An optional player-supplied `manifest.json` in the pack may still override id or display fields when present and valid.

## Layout rules

Accepted pack under the mods folder (example):

```text
mods/cloudy-smbu-2/
  rules.txt          # [Definition] only; titleIds includes 0005000010101d00
  content/           # game-relative replacements
  meta/              # ignored (boot/icon TGA, meta.xml); not mapped into /vol/content
```

Rules:

- Map only files under `content/` into the replacement index (same engine as [content.md](content.md)).
- Ignore `meta/` for content packs; do not map it into `/vol/content`.
- Reject `code/`, non-empty patch payloads (`.asm`, `patches.txt`), shader files, and mixed graphics+content in one folder (same refusal policy as [content.md](content.md): do not drop half the pack silently).
- Title ID checks stay on supported 1.3.0 builds (USA or Europe).
- The folder name may differ from the package `id`.
- Content-only Lua packages may keep a pack-root Definition-only `rules.txt`.

## Size limits

| Limit | Value |
| --- | --- |
| Per file | **512 MiB** |
| Per package (total) | **2 GiB** |
| Entry count | **4096** |

These caps are shared by content indexing and archive staging.
Symlinks and path traversal stay rejected.

## Runtime and UI

- Keep using the existing immutable content map and restart-on-enable rules ([content.md](content.md)).
- **Open mods folder** stays the only install action in the Mods tab.
- The Mods tab shows the generated name and version from `rules.txt`.
- Title ID misses, mixed graphics+content, and size/entry failures surface as package problems.
- Profiles and disable/update behaviour match other `content_dir` packages.

## Out of scope

- Applying Cemu PPC / `.asm` code patches or randomizers.
- Mapping `meta/` into the host window icon or boot splash.
- Folding graphics packs into Lua.
- Guaranteeing every fan level archive loads; the host only replaces files the game already opens.
- An in-game file picker or ZIP import UI.

## Tests

`mod_packages` covers synthetic Definition-only packs with `content/` and ignored `meta/`, wrong title IDs, mixed Definition+Preset+`content/`, and files above the old 128 MiB cap.
See [mod-manager.md](mod-manager.md#validation).

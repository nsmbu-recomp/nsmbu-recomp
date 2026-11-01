# File replacements (`content_dir` on Lua packages)

Status: **live**.
File replacements (models, textures, UI, language packs) ship as Lua packages with an optional `content_dir`.
`kind: content` is a one-release read-only alias and migrates to `kind: lua` on scan.
See [lua-mods.md](lua-mods.md) for the package model.
See [mod-manager.md](mod-manager.md) for install and shared manifest fields.

Packages may be content-only: a manifest and a `content/` tree, with no script.
They do not run host or guest code unless they also ship a Lua entry.

## Player workflow

Use **Mods → Open mods folder** and place a package directory under `mods/` with a `manifest.json` and a `content/` tree (`content_dir`).
Enable the package and restart the game.
Disable it and restart to restore original reads; then replace or remove the folder.
`enabled` in `mods.json` chooses the next launch's content set.
Active content is deliberately immutable for the session.
Script-only Lua packages (no `content_dir`) still load live; a package with `content_dir` always needs a restart.

There is no in-game file picker or import step.
Build the package layout yourself, or drop in a Cemu file-only pack root as in [cemu-content.md](cemu-content.md) (Definition-only `rules.txt` + `content/`; the host detects the pack on scan without writing into the folder).

## Package layouts

A content package is a folder the player copies into the mods directory:

- `mods/<folder>/manifest.json` with `kind: lua` and `content_dir`
- `mods/<folder>/<content_dir>/...` game-relative replacements (usually `content/`)

Tests and tooling may still call the legacy importer helpers for SDCafiine trees, Definition-only Cemu packs, and loose known `.pack` files.
Those helpers are not exposed in the Mods tab.
Explicit SDCafiine and Cemu title IDs must include NSMBU USA `0005000010101d00`.

## Fan translations

A fan translation is usually a replaced 2D language pack (`permanent_2d_<Region><Language>.pack`: every message, the fonts, and the 2D layouts), often with a replaced title logo (`Common/Layout/Title_00.szs`).
Install it like any content mod: put the package under the mods folder, enable it, and restart.
The language pack applies to the language of the same name whatever region its file name has.
A translation shipped as `permanent_2d_EuEnglish.pack` (made for the European game) replaces English in the USA game.
A pack with the exact name the game asks for always comes first.
Choose the language the translation replaces (usually English) in Settings → Language.
No renaming is needed, and the game folder is never changed.
Arabic and Hebrew translations are shaped and laid out right to left by the port itself.
Install the language pack as a content mod, enable it, and restart.
When the message font has enough Arabic or Hebrew letters, the session log reports `[rtl] right-to-left text on`.
Use `NSMBU_RTL=0` or `NSMBU_RTL=1` to force off or on.
Shaping and order are covered by CTest `rtl_text` and `runtime/src/text/`.
A Cemu code patch that such a translation ships for that purpose is not needed.
Ship only the content files in the package: a mod that also carries a code patch is refused as a whole.

## What is rejected

Support covers **content file replacement only**.
Packages (and legacy tooling) reject `code/` and DLC folders, PPC patches (`patches.txt` and Cemu `.asm` code patches), shader files, and non-Definition Cemu rule sections.
Cemu title `meta/` next to a content pack is ignored and is not mapped into `/vol/content`.
A mod that ships content files together with a Cemu code patch is refused as a whole, so part of it is never dropped silently.
To use only its content files, put a content-only package folder in the mods directory.
Randomizers and mixed code/data mods are not supported by this adapter.

These conventions follow the upstream [SDCafiine documentation](https://github.com/wiiu-env/sdcafiine_plugin) and [Cemu graphic-pack format](https://github.com/cemu-project/cemu_graphic_packs/wiki/How-to-create-Graphic-Packs).

## Manifest example

```json
{
  "format_version": 1,
  "id": "my-model",
  "name": "My local model replacement",
  "version": "1.0.0",
  "game_id": "nsmbu-usa",
  "minimum_manager_version": "1.0.0",
  "kind": "lua",
  "content_dir": "content"
}
```

No `lua.entry` or `main.lua` is required for a content-only package.
Keep the original game-relative filenames and directory structure under `content`.
The game must already be able to load the replacement format: archive/model sizes, joints, animations, and resource names must match the mod's target.
Raw PNG/DDS texture packs that expect Cemu's renderer interception are not equivalent to replacements of game archives and need a separate adapter.
A manifest marked compatible only means the host can load the package, not that every modified model or UI archive has been visually tested.

## Runtime behaviour

`runtime/src/mods/content.cpp` validates and indexes files at startup.
Enabled Lua packages that set `content_dir` feed the same immutable case-insensitive map.
Read-only FS opens, path stats, and read-only savestate handle reopens consult that map.
Writes, saves, code, and meta paths use the original resolver.
Missing paths fall through unchanged.
Directory enumeration retains original names but reports replacement sizes for replaced entries.
This adapter targets replacement of existing resources, not discovery of new files or deletion/hiding.
Conflicting enabled packages are rejected, rather than silently choosing a load order.
Content-only Lua packages cannot declare runtime options or dependencies unless they also ship a script entry that uses them.

Installed payloads should not be edited externally while the game runs.
Savestates must be used with the same active content set; savestate metadata does not record an asset fingerprint yet.
Indexing limits are 4096 entries, 512 MiB per file, and 2 GiB per package.
Those caps cover full Cemu level packs (including large soundtrack archives).
See [cemu-content.md](cemu-content.md).
Symlinks and path traversal are rejected.
Content files stay in the package folder under the mods directory; the original game files are not changed, and game assets stay outside Git.

## Tests

`mod_content_startup`, `mod_content_fs_off`, and `mod_content_fs_on` cover startup activation and filesystem HLE against synthetic files.
See [mod-manager.md](mod-manager.md#validation).

# File replacements

Status: **installed**.
The manager installs `kind: content` packages: a `content_dir` tree of game-relative
replacement files (SARC archives, textures, layouts, sounds).
See [mod-manager.md](mod-manager.md) for install and shared manifest fields.
See [cemu.md](cemu.md) for the graphics and shader packs the Mods tab also installs.

## Package

A content package is a folder in the mods directory with a `manifest.json`
(`format_version` 1 or 2, `kind: content`, `content_dir`) and the replacement
tree it names. It carries no dependencies and no runtime options.

```json
{"format_version": 2, "id": "my-pack", "name": "My Pack", "version": "1.0.0",
 "game_id": "nsmbu-usa", "kind": "content", "content_dir": "content"}
```

- Replacement paths are relative to `content_dir` and map to game-relative
  paths under `/vol/content/` (matching is case insensitive).
- Every enabled package must own each path it replaces: two enabled packages
  that replace the same file fail to enable with a `Content file conflict` error.
- Size limits and symlink/traversal rules are in [cemu-content.md](cemu-content.md).

## Runtime

Enabling or disabling a content package needs a restart.
While enabled, reads under `/vol/content/` (and relative content paths) open the
replacement file instead of the game's: opens, stats, and directory enumeration
see the replacement's size and content, and saves stay untouched.
`mods/content.cpp` holds the index and the open-path hook (`hle/fs.cpp`).

Cemu file-only level packs are imported as content packages too.
See [cemu-content.md](cemu-content.md).

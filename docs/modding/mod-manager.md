# Recomp mod manager

The mod manager installs, enables, and configures packages from inside the game.
No game content is part of the repository or of published packages.
Mods that need game data read it from the player's own game files.

This page describes the manager shell as it works today.
Manager version **1.0.0**.

## Package kinds

| Kind | Status | Doc |
| --- | --- | --- |
| `cemu` | Live | [cemu.md](cemu.md) |
| `content` | Rejected | [content.md](content.md) |

The only package the manager installs is a Cemu graphics or shader pack.
Any other kind is rejected.
File-only Cemu level packs are rejected the same way.

## Player workflow

Open the in-game settings overlay (`` ` `` or F12, Cmd+, or Settings in the menu) and select **Mods**.
The tab manages packages that already sit in the mods folder.
There is no built-in gameplay catalogue and no cheat list.

**Open mods folder** opens `<host config directory>/mods` in the system file manager.
Put each Cemu graphics pack in that folder as its own subdirectory.
The package `id` comes from the manifest; the folder name can differ.
There is no in-game import or install step.
The Mods tab rescans that folder about once per second while it is open, when every package is disabled (same rule as the old refresh).
Installed packages start disabled.
Nothing from a package is loaded until you enable it.
Enabling resolves required dependencies.
Missing versions, cycles, and declared conflicts produce an error.
The details show metadata, status, and bool, number, string, enum, and radio options.
A number option moves by its `step`.
A radio option shows one button for each choice.
The button text is the label.
The stored value is the choice.
String edits commit with Enter.
Disable a package and restart before replacing or removing its folder.
Cemu packs need a restart after enable, disable, or preset changes.
Replace the same folder id while disabled to update; configuration is preserved by ID.

`mods.json` stores package toggles in top-level `enabled` and option values in top-level `config`.
An older nested `profiles` object (or legacy `profiles.json`) still loads: the active profile's `enabled` and `config` move to the top level.
The next save writes the flat file and drops `profiles`, `active`, `builtins`, and `builtin_options`.
Disable all covers installed packages.
Explicit startup environment values, including zero, override saved choices at startup.

Storage is `<host config directory>`: `mods/<folder>/manifest.json` plus package files, and `mods.json`.
`NSMBU_MOD_MANAGER_DIR` selects isolated storage (same layout under that directory).
`NSMBU_NO_HOST_INPUT` skips user preferences and package storage unless an explicit manager directory is supplied for a test.
Package files live under the mods folder; the original game files are not changed.
Game assets and saves are never committed, uploaded, or redistributed.

Test aids (only with `NSMBU_NO_HOST_INPUT`): `NSMBU_TEST_MOD_ENABLE=<id>` ticks that package's checkbox once when the Mods tab is drawn (with `NSMBU_TEST_OVERLAY=open:mods`).
It needs an explicit `NSMBU_MOD_MANAGER_DIR`, so it never applies to a player's storage.

## Shared manifest fields

There is no separate manifest schema file.
The fields below are what the manager reads.

| Field | Meaning |
| --- | --- |
| `format_version` | `1` |
| `id`, `name`, `version` | Stable lowercase ASCII identifier, display name, three-part version |
| `game_id` | `nsmbu-usa` (the runtime also verifies its exact RPX entry) |
| `author`, `description` | Optional display metadata |
| `minimum_manager_version` | Optional three-part minimum |
| `kind` | `cemu`. Any other kind is rejected |
| `dependencies` | Objects with `id` and optional `minimum_version`. `builtin:<id>` is rejected |
| `conflicts` | Package IDs. `builtin:<id>` is rejected |
| `options` | Typed defaults and names; numeric min/max/step or enum choices |

Kind-specific fields:

| Field | Kind | Doc |
| --- | --- | --- |
| `cemu_dir` | `cemu` | [cemu.md](cemu.md) |

The desktop folder and file install workflow is the supported UI.
Online downloads and catalogues are outside this manager.

## Packaging a mod

A package is a folder, or a ZIP archive of that folder's contents renamed to `.nsmbumod`, with `manifest.json` at its root.
Packages you publish must not contain game files.
Cemu packs are copied locally by the player into the mods folder.
See [cemu.md](cemu.md).

## Validation

`mod_packages` exercises install, missing dependencies, configuration, disable, and removal.
A package that names `builtin:` fails to install.
The next save drops leftover nested-profile and `builtins` keys.
The same test installs synthetic Cemu packs: preset validation and the restart-only lifecycle.
Install of every non-Cemu kind in the host test is asserted to fail.
A folder that is not a Cemu graphics pack is rejected.

`mod_cemu_startup` and `mod_cemu_backend` start the manager on prepared storage and check startup activation.
`mod_content_fs_off` and `mod_content_fs_on` call the guest filesystem HLE handlers against synthetic files.
`mod_cemu` covers `rules.txt` parsing.
All fixtures are synthetic; these are host tests, not proof that a given community mod looks right in game.

## Public project references (checked 2026-10-06)

- [Zelda64Recomp](https://github.com/Zelda64Recomp/Zelda64Recomp): built-in manager UX reference.
- [Its mod template](https://github.com/Zelda64Recomp/MMRecompModTemplate/blob/main/mod.toml): manifest shape reference.
- [BlueWake mod documentation](https://github.com/chrissotraidis/bluewake/blob/main/docs/MODS.md): architectural research only.
- [ModernGekko](https://github.com/ExpansionPak/ModernGekko): loader research only; packages are not ABI-compatible here.

No source from those projects has been copied into this mod manager.

# RecompLauncher support

[RecompLauncher](https://recomplauncher.com) is a game launcher for recompilation ports.
It lists the port, guides the player to their own copy of the game, runs the setup
once without a window, shows the port's settings on the game's page, and starts the game.
Support for it lives in three places: the profile `recomplauncher.json`, the
`--launcher-progress` mode of `installer/setup.py`, and the packaging.

The agent guide for adding or updating support is at
<https://recomplauncher.com/sdk-files/recomplauncher-agent.md>,
and the JSON schema at <https://recomplauncher.com/schema/recomplauncher.json>.

## The profile

`recomplauncher.json` sits at the root of the repository.
It describes the game, the setup step, the start command, the settings, and the folders.

- `game.kind` is `iso_or_folder`: the player's copy is picked as one file or one folder.
  `game.check.file` is `code/red-pro2.rpx`, so an extracted folder is the game and not the Luigi U add-on.
  `game.check.ids` lists the supported title ids (USA, Europe, and the two combo packs).
  `installer/setup.py` still checks the copy when setup runs.
- `dumps` lists the raw disc images Redump has verified, so the launcher can show a verified badge.
  Both entries are 25025314816 bytes.
  Europe is CRC32 `b2e2cb84` (redump.info disc 41351, serial WUP-P-ARPP-EUR-0).
  Europe with New Super Luigi U is CRC32 `d069637e` (redump.info disc 41086, serial WUP-P-ATWP-EUR-0).
  GameTDB lists the same size, CRC32, MD5, and SHA-1 for both.
  USA, Japan, and the USA combo pack have no Redump page, so they are left out.
  A `.wux`, a `.wua`, or an extracted folder is not that raw image, so it does not get the badge.
  Setup still accepts it.

`tools/release/package.py` writes the profile at the root of every release zip.
It sets `prepare.version` to that release, so an update runs setup again.
The built game is linked against the runtime in the release.
The AppImage step copies the folder, so the image has the same file at `/recomplauncher.json`.
`installer/recomplauncher-prepare.sh` is on the packaging list for the same reason.

Validate the profile after every edit:

```sh
python3 -c "import json, jsonschema; jsonschema.validate(json.load(open('recomplauncher.json')), json.load(open('SCHEMA_FILE')))"
```

with `SCHEMA_FILE` the downloaded schema.

## What the player picks

The label names the three forms the port accepts, and `installer/setup.py` takes all three
through the same `--image {game}` argument the launcher passes:

- **A Cemu archive (`.wua`)**: no keys; the setup picks the supported title out of the archive.
- **A disc image (`.wud` or `.wux`)**: needs the disc key file next to it (`IMAGE.key`)
  and the Wii U common key (`common.key` beside the image, or `--common-key`).
- **The extracted game folder** (`code/`, `content/`, `meta/`).
- **The 1.3.0 update** is a separate optional input, `update`.
  The launcher passes it as `--update-dir={input:update}`.
  When the player skips it, that argument is left out.
  A base disc and a base game folder both receive that folder.
  A copy that is already 1.3.0 does not need it.
- **New Super Luigi U** is a separate optional input, `luigi`.
  The launcher passes it as `--dlc-dir={input:luigi}`.
  When the player skips it, that argument is left out.
  A folder that is only the add-on is refused as the game, with a line that says to pick the game folder and add this folder as the add-on.

A folder arriving through `--image` is routed to the same folder source as `--game-dir`
(`installer/setup.py`, in `run()`): a `.wua` extension still selects the archive source,
and anything else selects the disc image source.
Without that routing the launcher's `{game}` placeholder would fail for a folder pick,
because the schema has no way to swap the prepare arguments by what the player picked.

## The setup step (prepare)

The launcher runs the first program in `prepare.program` that exists:

- Windows: `NSMBU.exe`.
- Linux: `tools/nsmbu-launcher`.
  A release from before this path still has `nsmbu-launcher` at the top of the folder.
  That name is listed after it.
- macOS: `NSMBU.app/Contents/MacOS/nsmbu-launcher`.
- `installer/recomplauncher-prepare.sh` is the fallback when that program is not in the folder.

Each of those consumes `--console-setup` and runs `installer/setup.py` on the remaining arguments, without the installer window.
The shell script drops the flag wherever it sits, then execs `python3 installer/setup.py`.
`setup.py` also accepts `--console-setup` and ignores it.
A forwarded flag cannot end setup with exit code 2.

The remaining arguments are `--launcher-progress --yes --no-launch --no-shortcuts
--data-dir {install}/data --image {game}`, with `timeout` 3600.
The step is "done" when `{install}/data/install.json` exists.
That file is written at the end of a successful setup.
An extracted folder is not copied into `data/game`, so the done check is not the RPX there.

## `--launcher-progress`

`installer/setup.py --launcher-progress` prints machine progress for the launcher's
setup screen: one `progress 0.42 Text` line per update on stdout, the fraction running
from 0 to 1, and a final `progress 1.0 Done` when the setup leaves with code 0.
It reuses the setup's existing step and progress plumbing:

- `step(n, total, text)` reports the current numbered step.
- `Progress.update()` reports a sub-fraction inside the current step
  (downloads, extraction, translation, compilation all flow through it),
  including the `progress DONE TOTAL` lines forwarded from `nsmbu-extract`.

The `LAUNCHER` and `LAUNCHER_STEP` globals carry the state.
Without the flag nothing changes: the terminal and graphical setups behave as before,
and the game needs no new argument to play.

## Settings

The `settings` section maps launcher choices to the game's INI keys.
The config file is `{install}/data/nsmbu/settings.ini`.

Current settings and their keys:

| Setting | INI keys |
| --- | --- |
| Window | `tvFullScreen` |
| Resolution | `resScale` |
| Aspect ratio | `aspect` |
| Frame rate | `fps60`, `fpsCustom`, `interpFps`, `fpsUnlocked` |
| V-Sync | `vkPresentMode`, `vkPresentChosen` |
| Anti-aliasing | `fxaa` |
| Texture filtering | `aniso` |
| GamePad screen | `drcMode` |
| Language | `language` |
| Volume | `audioVolume` |

Every default was taken from the runtime code, not guessed.

## Saves, logs and errors

- `saves` covers `{install}/data/nsmbu/save` and the `settings.ini` beside it.
- The `saves`, `logs` and `data` actions open those folders.
  Logs are `{install}/data/nsmbu/logs`.
  "Clear shader cache" empties `{install}/data/nsmbu/shadercache`.
- `exit_codes` maps the game's exit code 1 (a fatal error, see `user_error.cpp`)
  to a message that points at the Setup button and the Logs folder.

## Start

`start` runs the built game.
On Windows that is `data/bin/nsmbu.exe`.
On Linux, Play starts `nsmbu` at the top of the folder.
That runs `data/bin/nsmbu`.
On macOS that file is `data/bin/nsmbu`.
The arguments are `--install {install}/data`.
That reads `install.json` for the game folder and uses `{install}/data/nsmbu/save` for saves.
A disc image or a Cemu archive is recorded as `game` under the data folder.
An extracted folder is recorded as the path the player picked.
The setup program is only the fallback when that built game is not there yet (`NSMBU.exe`, `tools/nsmbu-launcher`, or the Mac app).
It is not at the top of the Linux folder.
A program left there whose name contains "nsmbu" is what Play would start instead, including `NSMBU.desktop`.
`--data-dir {install}/data` is for that setup program.
The setup program ignores `--install`.
The game ignores `--data-dir`.

## What is not supported yet

- USA, Japan, and the USA combo pack are not in `dumps` (no Redump page for those discs).
- No RecompLauncher achievements wired into the runtime
  (see <https://github.com/elripalda/RecompAchievements>).
- The launcher's `RECOMPLAUNCHER_MODE` / `RECOMPLAUNCHER_PAD` environment variables
  are not read by the runtime.
- The launcher check is the title id and `code/red-pro2.rpx`.
  Setup still rejects a copy the port does not support.

## Catalog and badge

The launcher's catalog lists the port as `nsmbu-recomp`
(<https://recomplauncher.com/game/nsmbu-recomp>).
The README carries the required badge linking there.
The developer should claim the entry from a developer account
(<https://recomplauncher.com/dev>) to confirm the profile and control the page.

## A folder for RecompLauncher

`nsmbu recomplauncher` writes `build/recomplauncher`.
Add that folder in RecompLauncher with Developer tools, Add a build.
The command keeps `data/` on a later run and replaces the launcher program, `installer/setup.py`, and `recomplauncher.json`.
`prepare.version` is `local-` plus the git commit, so a new commit runs setup again.

## Testing the setup step locally

```sh
# Unattended setup with launcher progress (fails cleanly without a dump, exit 1)
cd SOME_RELEASE_DIR
python3 installer/setup.py --launcher-progress --yes --no-launch --no-shortcuts \
    --data-dir OUT_DIR --image PATH_TO_COPY
```

Without `--launcher-progress` the same command must behave exactly as before.
`--console-setup` tells the launcher program to skip its window.
`setup.py` accepts the flag and ignores it.

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
  There is no `check` section, so the launcher itself does not verify the copy;
  `installer/setup.py` validates it instead (see below).
- `dumps` is left out on purpose.
  Redump has no Wii U section and No-Intro publishes no disc hashes for this format,
  so there are no verifiable CRC32 values.
  The installer's own check of the image stands in for it.

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

A folder arriving through `--image` is routed to the same folder source as `--game-dir`
(`installer/setup.py`, in `run()`): a `.wua` extension still selects the archive source,
and anything else selects the disc image source.
Without that routing the launcher's `{game}` placeholder would fail for a folder pick,
because the schema has no way to swap the prepare arguments by what the player picked.

## The setup step (prepare)

The launcher runs the first program in `prepare.program` that exists:

- Windows: `NSMBU.exe --console-setup …` — the launcher shim consumes `--console-setup`
  and runs the bundled Python on the rest, without opening a window when stdout is piped.
- macOS and Linux: `installer/recomplauncher-prepare.sh` drops a leading `--console-setup`,
  changes to the release root, and execs `python3 installer/setup.py`.

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
The config file is `settings.ini`, resolved the way the game resolves it:

- `{install}/data/nsmbu/settings.ini` when `data/bin/portable.txt` exists
  (every release zip is portable, so this is the usual case).
- `{config}/nsmbu/settings.ini` otherwise (the AppImage drops `portable.txt`).

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

- `saves` covers `{install}/data/nsmbu/save`, the portable `settings.ini` beside it, and `{config}/nsmbu/settings.ini` when the build is not portable (the Linux AppImage).
- The `saves`, `logs` and `data` actions open those folders;
  `logs` follows the same portable/config split as the settings file,
  and "Clear shader cache" empties `shadercache` in the same place.
- `exit_codes` maps the game's exit code 1 (a fatal error, see `user_error.cpp`)
  to a message that points at the Setup button and the Logs folder.

## Start

`start` runs the built game, `data/bin/nsmbu.exe` or `data/bin/nsmbu`, with
`--install {install}/data`.
That reads `install.json` for the game folder and uses `{install}/data/nsmbu/save` for saves.
A disc image or a Cemu archive is recorded as `game` under the data folder.
An extracted folder is recorded as the path the player picked.
`--data-dir {install}/data` is the same data folder for the setup program, which is the fallback when the built game is not in the release yet (`NSMBU.exe`, `nsmbu-launcher`, or the AppImage).
The setup program ignores `--install`.
The game ignores `--data-dir`.

## What is not supported yet

- No `dumps` section (no verifiable Wii U hashes exist; see above).
- No RecompLauncher achievements wired into the runtime
  (see <https://github.com/elripalda/RecompAchievements>).
- The launcher's `RECOMPLAUNCHER_MODE` / `RECOMPLAUNCHER_PAD` environment variables
  are not read by the runtime.
- The launcher-side check is empty, so the copy is validated only when the setup runs.

## Catalog and badge

The launcher's catalog lists the port as `nsmbu-recomp`
(<https://recomplauncher.com/game/nsmbu-recomp>).
The README carries the required badge linking there.
The developer should claim the entry from a developer account
(<https://recomplauncher.com/dev>) to confirm the profile and control the page.

## Testing the setup step locally

```sh
# Unattended setup with launcher progress (fails cleanly without a dump, exit 1)
cd SOME_RELEASE_DIR
python3 installer/setup.py --launcher-progress --yes --no-launch --no-shortcuts \
    --data-dir OUT_DIR --image PATH_TO_COPY
```

Without `--launcher-progress` the same command must behave exactly as before.
`--console-setup` is not a setup.py argument: only the Windows shim and the
wrapper script consume it.

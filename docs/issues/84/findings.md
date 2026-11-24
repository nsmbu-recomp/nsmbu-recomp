# Findings

## Report

- The reporter plays NSMBU through RecompLauncher.
- They say `recomplauncher.json` is mostly complete.
- They ask for Verified Copies: `game.dumps`.
- RecompLauncher was filling that field in for now.
- They link the RecompLauncher SDK agent guide and manual setup docs.

## Status

- GitHub shows issue **#84** as **CLOSED** on 2026-11-23.
- The title is "RecompLauncher support".
- There are no issue comments and no labels.
- `.tmp/issues/84.md` is not in the repo, so closure reason is not recorded locally.

## Changelog

- **0.10.0** added RecompLauncher support with `recomplauncher.json` in every release.
- **0.10.1** removed RecompLauncher from that release.
- **0.11.0** restored RecompLauncher setup, settings, and play from the player's copy.
- The current `CHANGELOG.md` does not cite **#84**.
- Commit `88ee47be` added an Unreleased note that Europe verified discs satisfy **#84**; that note is no longer in the file.

## Code

- `recomplauncher.json` ships in the tree and in release packages via `tools/release/package.py`.
- The `game` input includes `dumps` with two Europe Redump entries (`size` and `crc32`).
- The same input defines `check.file` and title `ids` for copy validation.
- `installer/setup.py` supports `--launcher-progress` for RecompLauncher setup.
- `installer/recomplauncher-prepare.sh` and `installer/gui/setup_gui.cpp` (`RECOMPLAUNCHER`) tie into launcher-driven setup.

## Assessment

- The request targets verified copy metadata on the game input, not a runtime bug.
- The repo already lists Europe disc hashes under `inputs[game].dumps`.
- Only Europe images are listed; USA or other regions are not in the file today.
- Closure with no comments likely reflects shipping that metadata after **0.10.0**, before the brief **0.10.1** removal and **0.11.0** restore.
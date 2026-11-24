# Findings

## Report

The reporter says the game worked until they updated.
Play now shows "Game folder not found" even though the folder is present.
They use a USA Rev 2 `.wux` copy.
The app error lists path `game`, not a full directory path.
They filed the report on v0.9.1 (f99ee83) on Windows.

## Status

GitHub marks issue #85 OPEN with label `bug`.
Author tzonnn opened it on 2026-11-22.
Maintainer IrishBruse said a fix ships in 0.10.1.
That fix makes portable Play resolve the game folder in the `data` folder beside the program.
The reporter downgraded and says the game works again on the older version.

## Changelog

`CHANGELOG.md` version 0.10.1 (2026-11-24) cites #85.
The entry says Play shows the full path when the game folder is missing and asks you to run setup again.

## Code

The default game path is the relative name `game` in `config::game_dir` (`runtime/src/core/core.cpp`).
`runtime/src/core/game_folder.cpp` function `unusable_reason` emits "Game folder not found."
`shown_path` there turns that into an absolute path for the error dialog.
`installer/gui/setup_gui.cpp` function `game_ready` reads `data/install.json`.
It resolves relative `game_dir` values against the `data` folder, then `launch_game` passes `--game` with cwd set to `data`.
`installer/setup.py` functions `resolved_state` and `abs_from_data` resolve the same relative paths during setup.

## Assessment

The failure matches Play starting the runtime with relative path `game` instead of `data/game`.
The reporter's error lacks the absolute path that 0.10.1 adds and likely predates the launcher fix.
0.10.1 should address this; confirmation from the reporter on that version would justify closing the issue.
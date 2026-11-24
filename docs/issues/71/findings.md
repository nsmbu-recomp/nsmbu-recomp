# Findings

## Report

The reporter plays v0.8.4 on Windows with a USA base disc.
They finish World 1, unlock the secret level, and collect all Star Coins in the big castle.
They enter the boss-fight cannon with Fire Mario and the game closes.
They list no log contents in the issue body.
The app wrote `crash-20261111-145654.log` on their machine.

## Status

GitHub issue #71 is open with the `bug` label.
IrishBruse asked for the crash log file, not only the path, and to update to the latest build.
`dante2009in-blip` attached crash logs on 2026-11-13 and 2026-11-14.
No comment confirms a retest on a newer release or closes the issue.

## Changelog

`CHANGELOG.md` `[0.10.1]` fixes the same scene: the game no longer closes when you take the cannon from the World 1 castle to the airship (#82).
That entry cites #82, not #71.
The report targets v0.8.4, which predates v0.10.1.

## Code

Commits for #82 add runtime hooks in `runtime/src/hooks/cs_probe.cpp`.
`hook_024E25F4` clears low null-page registers before an optional light-field copy.
`hook_024E29B8` returns early when course-exit teardown sees a null-page object pointer.
`recomp/hooks.txt` registers symbols `024E25F4` and `024E29B8` for those hooks.

## Assessment

The crash location and steps match #82 and the v0.10.1 changelog entry.
This issue likely describes the same bug on an older build without the cannon hooks.
Leaving #71 open may still help until the reporter confirms v0.10.1 or newer.
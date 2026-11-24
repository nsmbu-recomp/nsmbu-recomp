# Findings

## Report

The reporter plays v0.9.1 on Windows with the Europe NSMBU + NSLU disc.
They reach the World 1 castle last level.
The game crashes when they are about to defeat the villain.
They give no clear reproduction steps beyond normal play.
The app wrote `crash-20261119-130652.log` on their machine.
They pasted only the local path, not the file contents.

## Status

GitHub issue #79 is open with the `bug` label.
IrishBruse asked on 2026-11-19 for the crash log file, not only the path.
No follow-up comment attaches the log or confirms a retest on a newer build.

## Changelog

`CHANGELOG.md` has no entry for #79.
`[0.10.1]` fixes a World 1 castle crash when you take the cannon to the airship (#82).
That fix predates this report's v0.9.1 build by one minor release.
The reporter describes defeating the boss, not entering the end cannon.

## Code

The #82 fix lives in `runtime/src/hooks/cs_probe.cpp`.
`hook_024E25F4` clears low null-page registers before a light-field copy.
`hook_024E29B8` returns early when course-exit teardown sees a null-page object pointer.
`recomp/hooks.txt` registers symbols `024E25F4` and `024E29B8`.
The repo shows no other World 1 castle boss hooks tied to this report.

## Assessment

The scene matches World 1 castle endgame, but the timing differs from #82's cannon step.
Without the crash log, the stack trace and exact trigger stay unknown.
A retest on v0.10.1 or newer would show whether the #82 fix covers this case.
The issue should stay open until the reporter uploads the crash file or confirms a fix.
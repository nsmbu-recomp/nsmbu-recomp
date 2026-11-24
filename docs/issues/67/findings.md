# Findings

## Report

- The reporter says version 0.9.0 cannot build anymore on Windows with an Intel CPU.
- They say 0.8.4 and earlier versions work without problems.
- Reproduction is simply running setup or build.
- They use the USA base disc and attached a setup log on GitHub.
- The report does not name a specific error in the issue body.

## Issue metadata

- GitHub issue 67 is closed with label `bug`.
- kolomeets80 opened it on 2026-11-07.

## Comments

- IrishBruse replied on 2026-11-07 that version 0.9.1 is published.
- That reply says setup on the USA base disc finishes in 0.9.1.
- The comment links to the v0.9.1 release tag.

## Changelog

- CHANGELOG.md lists a 0.9.1 fix: setup finishes on the USA or Europe base disc (#67).
- Version 0.9.0 shipped on the same day and added several world-map menu hooks.
- No other CHANGELOG entry references issue 67.

## Code

- Commit `b72d95aa` (v0.9.1) skips the base-disc cursor site so setup can finish translating that disc.
- 0.9.0 left hook `@025A4C5C` in `recomp/hooks.txt` for all builds.
- On USA_V0 and EU_V0 base discs, that site sits in differently compiled `CourseSelectMenu::execute` code.
- The recompiler then refuses the hook and setup exits with translating the game code failed.
- v0.9.1 moved the hook to `recomp/hooks_menu_cursor.txt` with a build filter that excludes base-disc builds.
- Likely files: `recomp/builds.py`, `recomp/hooks_menu_cursor.txt`, `recomp/recomp.py`, `installer/test_setup.py`.

## Assessment

- The report matches a known 0.9.0 setup failure on USA base disc.
- v0.9.1 fixed it; the issue closed with a release pointer only.
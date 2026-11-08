---
name: changelog
description: "Clean Unreleased into a player-facing version block when cutting a release. Do not use when appending ordinary commit notes under Unreleased."
---

# Changelog

`CHANGELOG.md` follows [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).
GitHub Release bodies copy the matching version block from `CHANGELOG.md` via `tools/release/notes.py`.

Day-to-day notes under `## [Unreleased]` follow `AGENTS.md`.
Record player-facing changes only.
Include the issue number when the change fixes a tracked issue.

## Which file

`CHANGELOG.md` is the game, installer, and Mods tab.
A note about loading packages, the Mods tab, or install stays in `CHANGELOG.md`.

## When this skill runs

Only when preparing a release or when asked to clean the changelog for release.

At cleanup, move the cleaned block under a dated `## [X.Y.Z] - YYYY-MM-DD` heading and leave Unreleased empty.
Refresh the compare links at the bottom of `CHANGELOG.md` when versions change.

## Audience

Rewrite for someone who plays or installs the game.
Keep installer, crash reporting, mods UI, and in-game behaviour the player can notice or act on.
Drop CLI, tools, CI, docs paths, env vars, address maps, and other developer-only detail.
When a change is purely technical, write only the performance the player feels.
Drop the note when there is no player-visible change.

## Sections

Group notes under these `###` headings only, and omit empty ones:

1. `### Added`
2. `### Changed`
3. `### Deprecated`
4. `### Removed`
5. `### Fixed`
6. `### Security`

## Wording

Lead with the outcome the player notices.
One bullet is one change.
One sentence per line.
Wrap a continuation line with two spaces under the bullet.

Version headings use brackets and an ISO date (`## [X.Y.Z] - YYYY-MM-DD`).
Unreleased has no date.

## Steps

1. Read `## [Unreleased]` in `CHANGELOG.md`.
2. Drop or rewrite notes that are not player-facing using Audience.
3. Place remaining notes under the right `###` section.
4. Move the cleaned block under `## [X.Y.Z] - YYYY-MM-DD`, leave Unreleased empty, and refresh compare links when the version changes.
5. Done when the new version block is Keep-a-Changelog grouped and free of developer-only notes.

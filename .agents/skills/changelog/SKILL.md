---
name: changelog
description: "Clean Unreleased into a player-facing version block when cutting a release. Do not use when appending ordinary commit notes under Unreleased."
---

# Changelog

`CHANGELOG.md` and `modding/examples/CHANGELOG.md` follow [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).
GitHub Release bodies copy the matching version block from `CHANGELOG.md` via `tools/release/notes.py`.

Day-to-day appends belong under `## [Unreleased]` per `AGENTS.md` (commit message or short change note, rough wording is fine).
This skill runs only at release cleanup, when the `pre-release` skill (or an explicit cleanup ask) turns that block into the published version notes.

## Which file

`CHANGELOG.md` is the game, installer, and Mods tab.
`modding/examples/CHANGELOG.md` is the example mods in that folder.
A note about one example mod's behaviour goes only in the examples changelog.
A note about loading packages, the Mods tab, or install stays in `CHANGELOG.md`.

## When this skill runs

Only when preparing a release or when asked to clean the changelog for release.
Leave ordinary Unreleased appends to `AGENTS.md`; do not gate those on player-facing rules.

At cleanup, move the cleaned block under a dated `## [X.Y.Z] - YYYY-MM-DD` heading and leave Unreleased empty.
Refresh the compare links at the bottom of `CHANGELOG.md` when versions change.
The examples changelog keeps its own headings and is not copied into the GitHub Release.

## Audience (published block)

Rewrite for someone who plays or installs the game.
Keep installer, crash reporting, mods UI, and in-game behaviour the player can notice or act on.
Drop CLI, tools, CI, docs paths, env vars, address maps, and other developer-only detail.
When a change is purely technical (upload paths, decode, caches, and similar), write only the performance the player feels, not the mechanism.
Drop the note when there is no player-visible performance or behaviour change.

## Sections

Group notes under these `###` headings only, and omit empty ones:

1. `### Added`
2. `### Changed`
3. `### Deprecated`
4. `### Removed`
5. `### Fixed`
6. `### Security`

Within each section, list notes from most to least important to a player:

1. New playable builds or content (regions, packs, disc layouts).
2. Gameplay, performance, and display fixes that change how a session feels.
3. Crash or setup failures that block play, and clearer recovery (report dialogs).
4. Install and first-run changes that affect getting into the game.
5. Breaking changes the player must act on (save states, paths, formats).
6. Mods and packaging behaviour the player or mod author sees.
7. Smaller UI polish.

## Wording

Lead with the outcome the player notices.
Prefer plain product names over title ids, keep a title id only when it disambiguates an install.
One bullet is one change.
Do not combine separate features or fixes in one bullet.
Use a continuation line only for a caveat or how-to of that same change.
One sentence per line, wrap continuation lines with two spaces under the bullet.

## Entry shape

```markdown
## [Unreleased]

### Added

- Short outcome in plain language.
  Optional second sentence for the caveat or how-to.

### Fixed

- …

## [0.4.2] - 2026-10-14

### Fixed

- …
```

Version headings use brackets and an ISO date (`## [X.Y.Z] - YYYY-MM-DD`).
Unreleased has no date.
Keep each published bullet to one outcome plus at most one short supporting sentence unless a breaking caveat needs a third line.

## Steps

1. Read `## [Unreleased]` in the right file (`CHANGELOG.md` or `modding/examples/CHANGELOG.md`).
2. Drop or rewrite notes that are not player-facing using Audience.
3. Place remaining notes under the right `###` section using Sections and Wording.
4. Re-sort each section so importance order still holds.
5. Move the cleaned block under `## [X.Y.Z] - YYYY-MM-DD`, leave Unreleased empty, and refresh compare links when the version changes.
6. Done when the new version block is Keep-a-Changelog grouped, top-down by player importance, and free of developer-only notes.

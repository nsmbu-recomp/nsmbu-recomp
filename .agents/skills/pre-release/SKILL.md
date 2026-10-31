---
name: pre-release
description: "Prepare an NSMBU release for a person to review: changelog cleanup, nsmbu test, and the installer. Use when the user asks to prepare a release, run pre-release, or clean the changelog before a tag. Leave the checkout uncommitted and unpushed."
---

# Pre-release

Prepare version `X.Y.Z` in this checkout and stop for a person to review.
Run every command in `/home/econn/git/nsmbu-recomp`.
The `release` skill tags `main` and watches `.github/workflows/release.yml` after that review is on `origin/main`.

## Steps

1. Confirm the version string with the user when they did not name one (example `0.4.0`).
   The changelog heading is `## [0.4.0] - YYYY-MM-DD` with today's date.
   The later git tag is `v` plus that string (`v0.4.0`).
2. Use the open pull request whose head is `X.Y.Z`, or the branch the user names.
   Check that branch out and fast-forward it (`git pull --ff-only`).
   With no such branch, check out `main` and fast-forward it.
   Leave other open pull requests as they are.
   When the branch has diverged, stop and report that.
3. Follow the `changelog` skill on this branch.
   Clean `CHANGELOG.md` and, when its Unreleased block has notes, `modding/examples/CHANGELOG.md`.
   Move the cleaned game block under `## [X.Y.Z] - YYYY-MM-DD` and leave `## [Unreleased]` empty above it.
   `tools/release/notes.py` copies that block when the tag workflow publishes, so the heading has to be in the commit a person tags.
4. From this checkout, run `nsmbu test`, then `nsmbu installer --no-open`.
   Follow the `nsmbu` skill when a case fails.
   A failure from any other checkout, or from a binary that does not match `game/`, is a checkout mistake.
   Use this checkout and rerun.
   Leave the setup window to the person at the machine.
   Keep every fix in the working tree with the changelog edits.
5. Report the version, branch, both command results, and `git diff --stat`.
   List anything still broken.
   The person reviews this diff, commits, merges into `main` when the work is on another branch, and pushes `main`.
   Their push runs `.git-hooks/pre_push.py` (`ctest`, `tools/installer/test_setup.py`, and `tools/recomp/test_builds.py`).
   Linux does not compile the macOS or Windows package jobs.
   Those failures show up later in `gh run view <id> --log-failed`.

Done when the version block is in the working tree, `nsmbu test` and `nsmbu installer --no-open` have passed in this checkout, and the review report is delivered.
The remote is unchanged and `git status` still shows the preparation as local changes.

## Rules

- Leave every edit uncommitted.
- Leave every remote untouched, including `main` and tags.
- The person who reviews this checkout commits, merges, and pushes.
- Hand a clean `origin/main` to the `release` skill.

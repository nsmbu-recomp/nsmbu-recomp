---
name: release
description: "Publish an NSMBU GitHub release by committing the changelog, tagging the tip of main, and watching the release workflow. Use when the user asks to publish a release, push a v* tag, or watch a release workflow until it is published. Changelog cleanup and local tests are the pre-release skill."
---

# Release

Ship a versioned GitHub Release by pushing a `v*` tag on the tip of `main`.
The workflow is `.github/workflows/release.yml`.
It publishes on that tag push.
Run every command in `/home/econn/git/nsmbu-recomp` on `main`.
The `pre-release` skill leaves the cleaned changelog uncommitted for review.
This skill commits that changelog, pushes `main`, then tags that tip.
Keep retagging until the workflow conclusion is `success` and the GitHub Release exists and is not a draft.
Stop without retagging only when the user asks to watch or report.

## Steps

1. Confirm the version string with the user when they did not name one (example `0.4.0`).
   The git tag is `v` plus that string (`v0.4.0`).
   The changelog heading already on `main` is `## [0.4.0] - YYYY-MM-DD`.
2. Check out `main` and fast-forward (`git pull --ff-only`).
   When `main` has diverged from `origin/main`, stop and report that.
   When `CHANGELOG.md` or `modding/examples/CHANGELOG.md` has local changes and every other path is clean, commit those changelog files and push `main`.
   The commit message is `Note X.Y.Z.`
   The push runs `.git-hooks/pre_push.py`.
   When that hook fails, fix the failure and make a new commit.
   When any other path is modified, or `## [X.Y.Z]` is absent from `CHANGELOG.md` on `HEAD` after that push, stop and name the `pre-release` skill.
   Done when `HEAD` equals `origin/main` and that commit contains `## [X.Y.Z]`.
3. Tag that tip and push the tag: `git tag vX.Y.Z`, then `git push origin vX.Y.Z`.
   When the tag already exists on `origin` and `main` has moved, replace it: `git tag -d vX.Y.Z`, `git push origin :refs/tags/vX.Y.Z`, `git tag vX.Y.Z`, `git push origin vX.Y.Z`.
4. Find the run with `gh run list --workflow=release.yml --limit 3` and wait with `gh run watch <id> --exit-status`.
   On success, report the run URL and `gh release view vX.Y.Z --json url -q .url`.
   Confirm `gh release view vX.Y.Z --json isDraft -q .isDraft` is `false`.
   On failure, report the run URL and the failing step from `gh run view <id> --log-failed`, then stop.
   After a person lands a fix on `origin/main`, return to step 3.

Done when the tag is on `origin`, the release workflow conclusion is `success`, and the GitHub Release exists and is not a draft.

## Rules

- Tag pattern must be `v*` or the workflow does not publish.
- `tools/release/notes.py` copies the matching `## [X.Y.Z]` block from `CHANGELOG.md` into the GitHub Release body.
  That heading is already in the tagged commit.
- A changelog edit after the tag is published needs `gh release edit` to change the GitHub Release body.
- Dry run without publishing: `gh workflow run release.yml -f dry_run=true` (no tag required).
  Use that only when the user asks for a dry run.

## Jobs to expect

- `version` sets `VERSION` from the tag.
  On the nightly schedule it starts a development build when `main` moved since the `devel-build` tag.
- `macos`, `windows-x86_64`, `windows-arm64`, `linux-x86_64`, and `linux-aarch64` build, package, and run unit tests.
- `android-arm64` and `android-x86_64` build a signed setup APK without game code (`ANDROID_KEYSTORE_B64` secrets).
- `publish` uploads zips, the AppImage, and APKs, then runs `gh release create`.

Any failed package job skips publish.

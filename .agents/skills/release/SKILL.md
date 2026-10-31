---
name: release
description: "Cut an NSMBU GitHub release. Use when the user asks to release, cut a version, push a v* tag, or watch a release workflow until it is published."
---

# Release

Ship a versioned GitHub Release by pushing a `v*` tag on the tip of `main`.
The workflow is `.github/workflows/release.yml`.
It publishes on that tag push.
Run every command in `/home/econn/git/nsmbu-recomp` on `main`.
Keep fixing and retagging until the workflow conclusion is `success` and the GitHub Release exists and is not a draft.
Stop without retagging only when the user asks to watch or report.

## Steps

0. Merge the version branch into `main` before changelog, verify, or tagging.
   The version branch is the open pull request whose head is `X.Y.Z`, or the branch the user names.
   With no such branch, release the current `main`.
   Leave other open pull requests as they are.
   Check out `main`, pull, and confirm that checkout contains the release.
1. Confirm the version string with the user when they did not name one (example `0.4.0`).
   The git tag is `v` plus that string (`v0.4.0`).
   The changelog heading is `## [0.4.0] - YYYY-MM-DD`.
2. On `main`, follow the `changelog` skill.
   Move the cleaned Unreleased block under `## [X.Y.Z] - YYYY-MM-DD` and leave `## [Unreleased]` empty above it.
   Fold any later commits for this version into that same block before tagging.
   Commit on `main`.
   `tools/release/notes.py` copies that block when the tag workflow publishes, so the heading is in the tagged commit.
   A changelog commit after the tag is published needs `gh release edit` to change the GitHub Release body.
3. From this checkout, run `nsmbu test`, then `nsmbu installer --no-open`.
   Follow the `nsmbu` skill when a case fails.
   A failure from any other checkout, or from a binary that does not match `game/`, is a checkout mistake.
   Use this checkout and rerun.
   Leave the setup window to the person at the machine.
   Pushing `main` is the unit-test gate.
   `.git-hooks/pre_push.py` runs `ctest`, `tools/installer/test_setup.py`, and `tools/recomp/test_builds.py`.
   Linux does not compile the macOS or Windows package jobs.
   Those failures show up in `gh run view <id> --log-failed`.
4. Push `main`.
   The release commit is the tip of `main`.
5. Tag that tip and push the tag: `git tag vX.Y.Z`, then `git push origin vX.Y.Z`.
   When the tag already exists on `origin`, replace it: `git tag -d vX.Y.Z`, `git push origin :refs/tags/vX.Y.Z`, `git tag vX.Y.Z`, `git push origin vX.Y.Z`.
6. Find the run with `gh run list --workflow=release.yml --limit 3` and wait with `gh run watch <id> --exit-status`.
   On success, report the run URL and `gh release view vX.Y.Z --json url -q .url`.
   Confirm `gh release view vX.Y.Z --json isDraft -q .isDraft` is `false`.
   On failure, report the run URL and the failing step from `gh run view <id> --log-failed`, fix on `main`, push `main`, replace the tag as in step 5, and return to this step.

Done when the version branch is on `main` in this checkout, `nsmbu test` and `nsmbu installer --no-open` have passed here, the tag is on `origin`, the release workflow conclusion is `success`, and the GitHub Release exists and is not a draft.

## Rules

- Tag pattern must be `v*` or the workflow does not publish.
- `tools/release/notes.py` copies the matching `## [X.Y.Z]` block from `CHANGELOG.md` into the GitHub Release body.
  That heading must exist before the tag build publishes.
- Do not create an empty version heading.
- Do not push a tag that already exists on `origin` without retagging after a fix (the default loop) or an explicit retag request.
- Do not push `main` or the tag until step 0 (merge into `main`) is done and step 3 local verify has passed on `main`.
- Dry run without publishing: `gh workflow run release.yml -f dry_run=true` (no tag required).
  Use that only when the user asks for a dry run.

## Jobs to expect

| Job | Role |
| --- | --- |
| version | Sets `VERSION` from the tag; on the nightly schedule starts a development build when `main` moved since the `devel-build` tag |
| macos / windows-x86_64 / windows-arm64 / linux-x86_64 / linux-aarch64 | Build, package, unit tests |
| android-arm64 / android-x86_64 | Signed setup APK without game code (`ANDROID_KEYSTORE_B64` secrets) |
| publish | Upload zips / AppImage / APKs and `gh release create` |

Any failed package job skips publish.

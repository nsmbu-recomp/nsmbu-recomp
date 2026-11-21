---
name: release
description: "Ship or prepare an NSMBU release. Use when preparing a release, publishing a v* tag, or a named test build such as issue-60. Read the checkout and finish the stage that is not done yet."
---

# Release

Ship a GitHub Release from `/home/econn/git/nsmbu-recomp`.
Read the request and the checkout, then do the first stage whose done condition is still false.
A named test build uses the section at the end.
Every other request is a versioned release.

## Version

Confirm `X.Y.Z` when the user did not name one (example `0.4.0`).
The changelog heading is `## [X.Y.Z] - YYYY-MM-DD` with today's date.
The git tag is `v` plus that string (`v0.4.0`).
`tools/release/notes.py` copies that block into the GitHub Release body, so the heading has to be in the tagged commit.
A changelog edit after the tag is published needs `gh release edit` to change the body.

## Stages

Skip a stage that is already done.
When the checked-out branch has diverged from its upstream, stop and report that.
Stop before the push stage when the user asked to prepare or review and the version block is still uncommitted.
Continue through the tag when the user asked to publish or tag.

1. **Branch.** Use the open pull request whose head is `X.Y.Z`, or the branch the user names.
   Check it out and fast-forward (`git pull --ff-only`).
   With no such branch, check out `main` and fast-forward it.
   Leave other open pull requests as they are.
   Done when that branch is checked out and matches its upstream.
2. **Changelog.** Done when `CHANGELOG.md` has `## [X.Y.Z]` and `## [Unreleased]` is empty.
   When it is not, follow the `changelog` skill on this branch.
3. **Checks.** Done when `nsmbu test` and `nsmbu installer --no-open` have passed in this checkout on this tree, or when `HEAD` already contains `## [X.Y.Z]` and the worktree is clean.
   Follow the `cli` skill when a case fails.
   A failure from any other checkout, or from a binary that does not match `game/`, is a checkout mistake.
   Use this checkout and rerun.
   Leave the setup window to the person at the machine.
   Keep every fix in the working tree with the changelog edits.
   When stopping here for review, report the version, branch, both command results, and `git diff --stat`.
   List anything still broken.
   Linux does not compile the macOS or Windows package jobs.
   Those failures show up in `gh run view <id> --log-failed`.
4. **Push.** Check out `main` and fast-forward when this stage starts on another branch.
   When `## [X.Y.Z]` is absent from `CHANGELOG.md` on `main`, stop and report that the version block is still on the other branch.
   When `CHANGELOG.md` has local changes and every other path is clean, commit that file and push `main`.
   The commit message is `Release X.Y.Z`.
   The push runs `.githooks/pre_push.py` (`ctest`, `python3 -m unittest discover -s tests/py -p "test_*.py"`, and `npm test --prefix tools/cli`).
   When that hook fails, fix the failure and make a new commit.
   When any other path is modified, stop and report those paths.
   Done when `HEAD` equals `origin/main` and that commit contains `## [X.Y.Z]`.
5. **Tag.** `git tag vX.Y.Z`, then `git push origin vX.Y.Z`.
   `.github/workflows/release.yml` publishes on that tag push.
   The tag pattern has to be `v*`.
   When the tag already exists on `origin` and `main` has moved, replace it: `git tag -d vX.Y.Z`, `git push origin :refs/tags/vX.Y.Z`, `git tag vX.Y.Z`, `git push origin vX.Y.Z`.
   When the user asks only to watch or report, report the run and leave the tag.
   Dry run without publishing: `gh workflow run release.yml -f dry_run=true`.
   Use that only when the user asks for a dry run.
6. **Watch.** Find the run with `gh run list --workflow=release.yml --limit 3` and wait with `gh run watch <id> --exit-status`.
   On success, report the run URL and `gh release view vX.Y.Z --json url -q .url`.
   Confirm `gh release view vX.Y.Z --json isDraft -q .isDraft` is `false`.
   On failure, report the run URL and the failing step from `gh run view <id> --log-failed`, then stop.
   After a person lands a fix on `origin/main`, return to the tag stage.
   Done when the tag is on `origin`, the release workflow conclusion is `success`, and the GitHub Release exists and is not a draft.

## Named test build

Use this when the user wants a pre-release for one fix and the rolling `devel-build` tag stays as it is.
The workflow is `.github/workflows/test-release.yml`.
It starts `release.yml` with `test_name`.
The git tag is the name, for example `issue-60`, and it is marked as a pre-release.
Re-running the same name replaces only that tag.

1. Confirm the name.
   A bare issue number becomes `issue-` plus that number, for example `60` becomes `issue-60`.
   A name that starts with `v`, or the name `devel-build`, uses the versioned stages above.
2. Dispatch on the ref that contains the fix.
   That ref must already contain this workflow.
   `gh workflow run "test release" --ref <branch> -f name=issue-60`
3. Watch the `release` run that step starts, not the short test-release run.
   `gh run list --workflow=release.yml --limit 3`, then `gh run watch <id> --exit-status`.
4. Done when `gh release view <name> --json isPrerelease,url` shows a pre-release URL, and `gh release view devel-build --json tagName` is still `devel-build`.

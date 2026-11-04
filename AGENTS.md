# NSMBU native port

## Rules

- Assume that the Typescript to Lua watcher is already running.
- Never commit any game files to the repo they must always be gitignored.

## Changelog

Record every user-facing change in the changelog, including developer tooling and other work that ships in release notes.
Unreleased entries are cleaned up when cutting a release; you do not need to match final release formatting now.
When a change fixes a tracked issue, include its number in the entry (for example `#123`).

## Code style

Match the file you are editing.
Put the opening brace on the same line as the declaration.
Indent with 4 spaces.
The `nsmbu` target compiles with `-Wall`.

## Decomp

Any discoveries you find about the source code document it in the `vendor/decomp/docs` repo.

## Verification

Read the `verify` skill before you finish a change.
See [docs/developer/verification.md](docs/developer/verification.md) for the full check inventory.
Run every check a command, playtest, boot, or APK install can settle.
When a person still has to play through a level, end the turn with the verification steps for them.

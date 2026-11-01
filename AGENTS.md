# NSMBU native port

## Rules

- Assume that the Typescript to Lua watcher is already running.
- Add each commit message/change to the changelog it will be reformated later for release. 
  (Reference issue number if a fix for one was done eg #123)

## Code style

Match the file you are editing.
Put the opening brace on the same line as the declaration.
Indent with 4 spaces.
The `nsmbu` target compiles with `-Wall`.

## Decomp

Any discoveries you find about the source code document it in the `vendor/decomp/docs` repo.

## Verification

Read the `verify` skill before you finish a change.
See [docs/agent-verification.md](docs/agent-verification.md) for the full check inventory.
Run every check a command, playtest, boot, or APK install can settle.
When a person still has to play through a level, end the turn with the verification steps for them.

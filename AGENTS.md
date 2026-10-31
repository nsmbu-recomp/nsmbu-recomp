# NSMBU native port

## Rules

Assume that the Typescript to Lua watcher is already running.
Never comment on github issues.
Add each commit message/change to the changelog it will be reformated later for release.

## Verify

Playtests, benches, builds, and boots: `.agents/skills/nsmbu/SKILL.md`.
Write a host test or a playtest for each feature.

## Code style

Match the file you are editing.
Put the opening brace on the same line as the declaration.
Indent with 4 spaces.
The `nsmbu` target compiles with `-Wall`.

## Guest

Generated `f_XXXXXXXX` code, hooks, stubs, and guest memory are in `docs/how-it-works.md` and `docs/cli.md`.
Name a USA v1.3.0 address from `docs/names.md`.
Regional address maps are in `docs/builds.md`.
Map a crash log's `lr` / `ctr` to a USA symbol from `docs/logs.md`.
A model pose or a jittering picture: Frame interpolation in `docs/env.md`.

## Decomp

Any discoveries you find about the source code document it in the `vendor/decomp/docs` repo.

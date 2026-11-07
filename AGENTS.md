# NSMBU native port

## Rules

- Never commit any game files to the repo they must always be gitignored.

## Changelog

Record every user-facing change only in the changelog (Skip developer-facing changes).
When a change fixes a tracked issue, include its number in the entry (for example `#123`).

## Code style

Match the file you are editing.
Put the opening brace on the same line as the declaration.
Indent with 4 spaces.
The `nsmbu` target compiles with `-Wall`.

## Decomp

Any discoveries you find about the source code document it in the headers in `vendor/decomp/include` also add documentation comments explaining what the function does and any side effect or requirements for calling it.

## Verification

Read the `verify` skill before you finish a change.
See [docs/developer/verification.md](docs/developer/verification.md) for the full check inventory.

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

Document a discovery about original game code as a comment on that function's declaration in `vendor/decomp/include`.
State what the function does, and any side effect or requirement for calling it.
That declaration comment is the record.

## Tests

Add one test for every behavior change, in the suite that owns that behavior.
The new test fails on the old behavior and passes with the change.

- `ctest --test-dir build/linux --output-on-failure` runs the host tests.
- `python3 -m unittest discover -s tests/py -p "test_*.py"` runs the Python tests.
- `npm test --prefix tools/cli` runs the Node unit tests.
- `nsmbu test` runs only the game playtests in `tools/cli/test/playtests.test.ts`.

## Verification

Read the `verify` skill before you finish a change.

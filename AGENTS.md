# NSMBU native port

## Rules

- Never commit any game files to the repo they must always be gitignored.
- Always `nsmbu close` the running game session once finished.
- Always start nsmbu muted.

## Changelog

Record every user-facing change only in the changelog (Skip developer-facing changes).
When a change fixes a tracked issue, include its number in the entry (for example `#123`).

## Code style

Match the file you are editing.
Put the opening brace on the same line as the declaration.
Indent with 4 spaces.
The `nsmbu` target compiles with `-Wall`.

## Decomp

When decompiling a function, call the Task tool with `subagent_type` `decompile`.
When working with hooks make sure you understand the underlying code.
The prompt is the address, the symbol, or both.

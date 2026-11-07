# tools/cli

## Tests

Unit tests use Node's built-in runner: `npm test` from this directory (also `npm test --prefix tools/cli` from the repo root).
The files are in `tests/cli/`.

Game regressions are `tools/cli/test/playtests.test.ts`.
`nsmbu test` sets `NSMBU_REPO` and runs that file through `node --test`.

## Fish completion

After changing the CLI command or flag interface, reinstall the fish completion:

```bash
nsmbu completion fish --install
```

That writes the installed completion from `src/completion.ts`, so the command list and flags match the CLI.

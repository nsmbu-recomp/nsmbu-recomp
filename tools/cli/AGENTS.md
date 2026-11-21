# tools/cli

## Tests

The test commands are in the repo root [AGENTS.md](../../AGENTS.md) Tests section.

## Fish completion

After changing the CLI command or flag interface, reinstall the fish completion:

```bash
nsmbu completion fish --install
```

That writes the installed completion from `src/completion.ts`, so the command list and flags match the CLI.

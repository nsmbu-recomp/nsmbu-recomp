# NSMBU native port

## Rules

- Never commit any game files to the repo they must always be gitignored.
- Always close a live game with the MCP tool `session_close` when finished.
- Start that game from the debug executable with `--mcp` (it is muted).
- For automated testing, run headless with `just scripted <name>` after setting `NSMBU_EXIT_AT_FRAME` (see `docs/developer/automation.md`).
- Before a live check or a decompile, read the debug skill.

## Changelog

Record every user-facing change only in the changelog (Skip developer-facing changes).
When a change fixes a tracked issue, include its number in the entry (for example `#123`).

---
name: crash
description: "Read NSMBU crash, run, and setup logs and what each file contains. Use when a crash log, nsmbu.log, setup.log, or a player bug report needs reading."
---

# Crash logs

Paths and which file to attach are in `docs/user/logs.md`.
In a source checkout, read `nsmbu/logs/`.

## Files

- `crash-YYYYMMDD-HHMMSS.log` — newest host signal or guest `OSPanic`.
  Signal line (`CRASH: signal` or `Windows exception`), host pc and module, guest `lr` / `ctr` / call chain, crash context, then `--- last log lines ---` (the run-log ring).
  Guest addresses are the running build.
  Map them with the steps in `docs/user/logs.md`.
- `nsmbu.log` — this run.
  The previous run is `nsmbu-previous.log` (renamed at start).
- `setup.log` — installer and first start.
  `setup-window.log` is written when the setup window itself fails.

`NSMBU_LOG_FILE` overrides the run log (playtests often use `.tmp/`).
`NSMBU_LOG_FILE=0` skips the file.
Stderr still prints `[crash] wrote <path>`.

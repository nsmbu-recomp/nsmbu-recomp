# Findings

## Report

- The reporter cannot finish first-time setup on Windows 10.
- They run an Intel i3 with 8 GB shared system memory.
- Progress stops near 74% during the installer build.
- The attached log shows a `MemoryError` in the PowerPC-to-C translation step, not in the later C compile.
- Setup reached float analysis, then failed while translating ~81k functions into output files with four worker processes.

## Status and discussion

- GitHub marks issue #66 **CLOSED** (author `alexliradev`, label `bug`).
- Maintainer IrishBruse closed it on 2026-11-07 with one comment.
- IrishBruse said the machine ran out of RAM during compile and asked the reporter to close other programs and retry.
- There is no follow-up from the reporter.

## Changelog and code

- CHANGELOG.md does not cite `#66` and has no setup RAM or `MemoryError` entry.
- A later entry says setup and full recompile use every CPU core, which can raise memory use on small machines.
- `installer/setup.py` function `recompile()` calls `recomp/recomp.py` without `--jobs`.
- Setup `--jobs` only applies to the C compile step (`compile_gamecode`), not the translation step.
- On Windows, `recomp/recomp.py` uses spawn workers that reload the full `Recompiler` from a pickle in `_load_rc`.
- Failures in the log match `_load_rc` (line 159) and `_emit_job` (line 235) in `recomp/recomp.py`.
- `NSMBU_RECOMP_JOBS` or `recomp.py --jobs` can limit translation workers; setup does not set them today.

## Assessment

- The log supports out-of-memory during translation, not a bad game dump or hook mismatch.
- Hook address warnings in the log are non-fatal; the build stopped on `MemoryError` afterward.
- No code change tied to this report appears in CHANGELOG; closure treated it as a hardware limit.
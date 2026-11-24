# Findings

## Report

The reporter launches `run.sh` on Batocera 43.1 x86_64 with an AMD GPU (title: RX 6800).
The game shows a black screen with audio, then exits to the terminal after a few seconds.
The log cites `totk.log`, a RADV/amdgpu context-lost GPU hang, and a fatal Vulkan fault in `totk-recomp/runtime/nvn/vulkan/exec.c`.
The reporter says `RADV_DEBUG=nogpl ./run.sh` avoids the crash.
Hardware lines list Xeon CPU, USA 1.3.0, and a self-built binary.

## Issue status

GitHub issue #52 is closed with the `bug` label.
IrishBruse closed it on 2026-11-01 and said this is the NSMBU recomp repo, not TOTK recomp.
No maintainer reproduced or fixed an NSMBU-specific crash here.

## Changelog

`CHANGELOG.md` has no entry for #52.
No changelog line names AMDGPU, RADV, `nogpl`, or Batocera black-screen exits.

## Code and repo fit

This repo has no `runtime/nvn/vulkan/exec.c` and no `[hang]` submission logging like the report.
`RADV_DEBUG` and `nogpl` appear only in the issue text, not in project scripts or docs.
Vulkan rendering here lives under `vendor/ZeldaWWHDRecomp/runtime/src/gfx/vulkan/` (for example `draw.cpp`, `surfaces.cpp`).
That stack is unrelated to the NVN Vulkan path named in the log.

## Assessment

The report almost certainly targets Zelda: TOTK recomp, not NSMBU.
Close-as-wrong-repo is appropriate; there is no evidence of a reproducible NSMBU bug in this tree.
# Findings

## Report

- The game runs well overall, but it sometimes freezes briefly and then recovers.
- The hitch is not long, but it can interrupt gameplay.
- Reproduction is simply playing during normal gameplay.
- Version 0.8.2 on Android with NSMBU + NSLU (USA).
- Hardware: POCO X7 Pro, MediaTek Dimensity 8400-Ultra, Mali-G720, 12 GB RAM.
- The reporter attached no logs or in-app diagnostics.

## Issue metadata

- GitHub issue 57 is closed with labels `bug` and `performance`.
- Prozil2005 opened it on 2026-11-01 and updated it on 2026-11-14.

## Comments

- IrishBruse said on 2026-11-07 that 0.9.0 should fix the hitches.
- Prozil2005 replied on 2026-11-14 that the brief freezes still happen.

## Changelog and code

- CHANGELOG.md does not cite issue 57.
- Version 0.9.0 (2026-11-07) shipped async shader compile and shader-cache loading at startup.
- That release notes that shaders can still compile during play and may cause stutters.
- 0.9.1 fixed setup on base discs (#67) only; it does not mention gameplay hitches.
- The repo has no other references to issue 57 in tracked sources.

## Related report

- Issue 75 is open with the same reporter text, the same POCO X7 Pro hardware, and the same symptom.
- Issue 75 reports version 0.9.1 on Android with NSMBU + NSLU (Europe).
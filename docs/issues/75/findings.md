# Findings

## Report (English)

- The game runs well overall, but it sometimes freezes briefly and then recovers.
- The hitch is not long, but it can interrupt gameplay during a session.
- Reproduction is simply playing during normal gameplay.
- The reporter was in World 1.
- Version 0.9.1 on Android with NSMBU + NSLU (Europe).
- Hardware: POCO X7 Pro, MediaTek Dimensity 8400-Ultra, Mali-G720, 12 GB RAM.
- The reporter attached no logs or in-app diagnostics.

## Issue metadata

- GitHub issue 75 is open with label `bug`.
- Prozil2005 opened it on 2026-11-14 and updated it on 2026-11-22.
- The issue body matches issue 57 except for version, region, and location.

## Relation to issue 57

- Issue 57 is the earlier closed report from the same reporter on the same phone.
- Issue 57 used version 0.8.2 with NSMBU + NSLU (USA).
- On issue 57, IrishBruse said 0.9.0 should fix the hitches.
- Prozil2005 replied that brief freezes still happen after 0.9.0.
- Issue 75 is a follow-up on 0.9.1 with the same symptom and hardware.

## Changelog and code

- CHANGELOG.md does not cite issue 75 or issue 57.
- Version 0.9.0 (2026-11-07) shipped async shader compile and shader-cache loading at startup.
- Async shader compile is off by default; changelog notes shaders may still compile during play and cause stutters.
- Version 0.9.1 fixed setup on base discs (#67) only; it does not mention gameplay hitches.
- The repo has no other tracked references to issue 75 beyond this issue folder and the issue 57 findings.
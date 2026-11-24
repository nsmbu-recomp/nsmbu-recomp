# Findings

## Title (English)

My game crashes in NSMBU in (French) Challenges volume 2.

## Report

- A Windows reporter on v0.9.1 (f99ee83) hits an access violation in Intel driver module `ig11icd64.dll`.
- They play Challenges ("défis") volume 2 among Goombas when the game crashes.
- Hardware is an Intel Celeron N5100 with 8 GB RAM and Intel HD Graphics Gen11.
- Game version is NSMBU + NSLU on a USA base disc.
- Steps cite a screenshot; prose is brief and partly French.
- The attached log shows boot, file select, and sustained play before the fault.

## Issue metadata

- GitHub issue 73 is open.
- `.tmp/issues/73.md` is not in the checkout, so comments were not reviewed locally.

## Crash summary

- Exception C0000005 is a read access violation inside `ig11icd64.dll`.
- Crash context names the Vulkan renderer at 0.5x internal resolution.
- The log chooses Intel Gen11 on driver 101.2130 with classic render passes.
- That path matches the Intel Windows rule for drivers older than 101.5000.
- The snapshot lists many `NSMBU_VK_*` CPU optimization flags enabled.
- One earlier log line drops a draw item with a null model; the crash site is not named.

## CHANGELOG

- No CHANGELOG entry cites issue 73.
- Versions 0.8.2 and 0.8.4 record issue 60 fixes for older Intel title-screen crashes after the intro.
- Those entries name `igvk64.dll` at the title screen, not in-game Challenges play.

## Code and docs

- `docs/developer/vulkan.md` documents `igvk64.dll` faults when the title screen draws shadow maps.
- That page does not mention `ig11icd64.dll`, Challenges gameplay, or Goomba courses.
- The issue 60 render-pass workaround was already active in this log.
- The repo does not record a confirmed cause or fix for this report.
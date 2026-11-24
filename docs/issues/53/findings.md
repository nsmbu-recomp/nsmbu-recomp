# Findings

## Report

The reporter cannot finish setup after upgrading to the newest release (v0.8.1).
They use a USA base disc on Linux (x86-64) with WUX, key, and Wii U common key files in place.
Game code translation fails at step 0 with exit code 1.
The app error points to `hooks.txt:26` and address `0252F900`.
The recompiler says the USA_V0 build compiled that function differently from canonical USA code.

## Issue status

GitHub issue #53 is closed (`bug`).
PelonchasXD filed it on 2026-11-01.
IrishBruse closed it the same day with: "Fixed in `0.8.2`."

## Changelog

v0.8.2 (2026-11-01) lists: "Setup finishes translating a USA or Europe base disc."
The entry does not cite #53.
No earlier changelog lines mention #53 or this hook error.

## Code and hook layout

`recomp/hooks.txt` line 31 still hooks `0252F900` for the USA 1.3.0 course HUD path.
`recomp/hooks_base_hud.txt` targets USA_V0 and EU_V0 with hook `0252D9C0` instead.
That file header notes base discs lack the body at `0252F900`.
`recomp/builds.py` enforces per-build hook lists via `# builds:` tags and emits the reported error when a hook file does not match the detected build.
`recomp/builds/usa_v0.json` and `recomp/test_builds.py` cover USA_V0 skipping `0252F900` and applying the base-disc HUD hook.
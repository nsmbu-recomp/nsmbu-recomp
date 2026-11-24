# Findings

## Status

Issue #31 is **closed**.
IrishBruse closed it on 2026-10-27.
The maintainer wrote that **v0.7.1** should have fixed this.

## Report

The reporter used NSMBU **v0.7.0** on Windows (Europe, i9-12900K).
They upgraded from an earlier **v0.5.0** build with game progress.
During setup, the save-import screen appeared after compilation.
They could not select **Copy saves and settings from another NSMBU folder**.
The screen appeared frozen and would not accept that choice.

## Changelog

**v0.7.1** (2026-10-27) lists a fix for this symptom.
Setup now lets you choose **Copy saves and settings from another NSMBU folder** when no earlier install is detected.
A later release also says the installer save screen imports a folder that contains `rp_personal_savedata.dat`.
CHANGELOG does not cite `#31`.

## Likely code

The GUI save step is `screen_save()` in `installer/gui/setup_gui.cpp`.
`set_save_kind()`, `send_import()`, and `A.legacy` control which radio options appear.
Kind remapping (`kind == 4` → `3`, and `kind == 2` when `!A.legacy`) sits in that screen.
The Python backend exposes `maybe_import_save()` and `import_existing()` in `installer/setup.py`.
The `import_existing` RPC copies saves and settings from another release folder.

## Assessment

The report matches the v0.7.1 changelog fix for the optional copy path when no legacy install exists.
Closure rests on that release, without a separate repro note in the thread.
Residual risk may remain for users still on v0.7.0 or folders without `rp_personal_savedata.dat`.
# Findings

## Report

- Reporter Al-AmeenAdewunmi saw Android setup fail during code translation on v0.8.0.
- They chose an extracted NSMBU+NSLU (USA) folder that had worked with the Windows installer.
- The app surfaced `java.io.IOException: No module named '_multiprocessing'` and reported setup failed.
- Hardware was a Samsung Galaxy S25+ on Android 17.

## Status

- GitHub issue 46 is closed as a bug.
- Maintainer IrishBruse pointed to a possible fix in v0.8.1 on 2026-10-31.
- Reporter Al-AmeenAdewunmi replied that v0.8.1 still failed the same way.
- Reporter Prozil2005 confirmed the same failure on 2026-11-01.
- Maintainer IrishBruse said it was fixed in v0.8.2 on 2026-11-01.
- Reporter Al-AmeenAdewunmi confirmed the fix on 2026-11-01.

## Changelog and code

- CHANGELOG.md does not cite issue 46.
- v0.8.1 lists a fix: Android setup finishes when Python lacks the multiprocessing module.
- v0.8.2 lists a broader fix: Android setup finishes translating instead of stopping.
- The bundled Python on some Android phones lacks the `_multiprocessing` extension.
- `recomp/recomp.py` defines `_import_mp()`, `_open_pool()`, and `_SeqPool` to fall back to one process.
- `recomp/singleflow.py` mirrors that guard in `_import_mp()` and in `solve()` for float analysis.
- `recomp/test_pool.py` asserts the sequential pool path when `_multiprocessing` is missing.
- Android setup runs the translator through `installer/setup.py` `recompile()` or the embedded path in `tools/android/setup_adapter.py`.

## Likely files and symbols

- `recomp/recomp.py`: `_import_mp()`, `_open_pool()`, `_SeqPool`
- `recomp/singleflow.py`: `_import_mp()`, `_fork_ok()`, `solve()`
- `recomp/test_pool.py`: `test_missing_multiprocessing_uses_one_process`
- `installer/setup.py`: `recompile()`
- `tools/android/setup_adapter.py`: `translate()`
- `CHANGELOG.md`: v0.8.1 and v0.8.2 Fixed entries
<!-- GitHub issue #66: Not compiling log (CLOSED) https://github.com/nsmbu-recomp/nsmbu-recomp/issues/66 -->

### What happens

Not compiling in windows 10 last build CPU inter i3 memory 8 gigas shared

### Steps to reproduce

stops in 74% of compilation

### Version

<?xml version="1.0" encoding="utf-8"?> <app type="complex" access="777">   <version type="unsignedInt" length="4">14</version>   <os_version type="hexBinary" length="8">000500101000400A</os_version>   <title_id type="hexBinary" length="8">0005000010101D00</title_id>   <title_version type="hexBinary" length="2">0000</title_version>   <sdk_version type="unsignedInt" length="4">20703</sdk_version>   <app_type type="hexBinary" length="4">80000000</app_type>   <group_id type="hexBinary" length="4">0000101D</group_id> </app>

### Game version

USA 1.3.0

### Platform

Windows

### Hardware

CPU i3 ROM + VRAM 8 gigas shared

### Logs


---- installer window ----
  OK: E:\GAMES\0 - RECOMPS\New Super Mario Bros U\Game (USA_V0, title version 0)
  Getting the compiler (llvm-mingw-20260922-ucrt-x86_64, 181.9 MB download, once).
  Downloading https://github.com/mstorsjo/llvm-mingw/releases/download/20260922/llvm-mingw-20260922-ucrt-x86_64.zip
  Unpacking...
  Using llvm-mingw llvm-mingw-20260922-ucrt-x86_64.
  Using the game files in E:\GAMES\0 - RECOMPS\New Super Mario Bros U\Game (not copied)
warning: hooks.txt:38: the USA_V0 build compiled 029D9560 (there: 02987A10) differently; the hook wraps the whole function, so check that it still means the same thing
warning: hooks_base_hud.txt:4: the USA_V0 build compiled 0252D9C0 (there: 02514488) differently; the hook wraps the whole function, so check that it still means the same thing
recomp: USA_V0, 81492 functions, 31 hooks, 18 sites, 4 workers
recomp: 101 register helpers, 48.1 s
recomp: float analysis, 81492 functions, one process
recomp: float analysis checked 50000 functions, 56205 waiting, 22.8 s
recomp: float analysis checked 100000 functions, 30222 waiting, 50.3 s
recomp: float analysis checked 150000 functions, 8233 waiting, 85.4 s
recomp: float analysis finished, 175865 function analyses, 123.7 s
recomp: translating 81492 functions into 93 files
Process SpawnPoolWorker-8:
Traceback (most recent call last):
  File "multiprocessing\process.py", line 313, in _bootstrap
  File "multiprocessing\process.py", line 108, in run
  File "multiprocessing\pool.py", line 109, in worker
  File "E:\GAMES\0 - RECOMPS\0 - QuiverLauncher\Apps\NewSuperMarioBrosU-NsmbuRecomp\recomp\recomp.py", line 159, in _load_rc
    _RC = pickle.load(f)
MemoryError
Process SpawnPoolWorker-5:
Traceback (most recent call last):
  File "multiprocessing\process.py", line 313, in _bootstrap
  File "multiprocessing\process.py", line 108, in run
  File "multiprocessing\pool.py", line 109, in worker
  File "E:\GAMES\0 - RECOMPS\0 - QuiverLauncher\Apps\NewSuperMarioBrosU-NsmbuRecomp\recomp\recomp.py", line 159, in _load_rc
    _RC = pickle.load(f)
MemoryError
multiprocessing.pool.RemoteTraceback: 
"""
Traceback (most recent call last):
  File "multiprocessing\pool.py", line 125, in worker
  File "E:\GAMES\0 - RECOMPS\0 - QuiverLauncher\Apps\NewSuperMarioBrosU-NsmbuRecomp\recomp\recomp.py", line 235, in _emit_job
    f.write("\n\n".join(parts))
            ~~~~~~~~~~~^^^^^^^
MemoryError
"""
The above exception was the direct cause of the following exception:
Traceback (most recent call last):
  File "E:\GAMES\0 - RECOMPS\0 - QuiverLauncher\Apps\NewSuperMarioBrosU-NsmbuRecomp\recomp\recomp.py", line 913, in <module>
    Recompiler(a.rpx, build).run(a.outdir, a.insns_per_file, on_progress=on_progress, jobs=_job_count(a.jobs))
    ~~~~~~~~~~~~~~~~~~~~~~~~~~~~^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
  File "E:\GAMES\0 - RECOMPS\0 - QuiverLauncher\Apps\NewSuperMarioBrosU-NsmbuRecomp\recomp\recomp.py", line 739, in run
    for st in pool.imap_unordered(_emit_job, jobs_arg, chunksize=1):
              ~~~~~~~~~~~~~~~~~~~^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
  File "multiprocessing\pool.py", line 873, in next
MemoryError


### From the app

_No response_

<!-- GitHub issue #70: hagan soporte para open gl antiguo (OPEN) https://github.com/nsmbu-recomp/nsmbu-recomp/issues/70 -->

### What happens

mi compu tiene intel i5 de gen 3  y en teoriael port lo coreria pero n o hay siporte para direct x 10 o 11 y open gl antiguo


### Steps to reproduce


---- installer window ----
  OK: C:\Nueva carpeta\New SUPER MARIO BROS U [Game] [0005000010101d00] (USA_V0, title version 0)
  Getting the compiler (llvm-mingw-20260922-ucrt-x86_64, 181.9 MB download, once).
  Downloading https://github.com/mstorsjo/llvm-mingw/releases/download/20260922/llvm-mingw-20260922-ucrt-x86_64.zip
  Unpacking...
  Using llvm-mingw llvm-mingw-20260922-ucrt-x86_64.
  Using the game files in C:\Nueva carpeta\New SUPER MARIO BROS U [Game] [0005000010101d00] (not copied)
warning: hooks.txt:65: the USA_V0 build compiled 029D9560 (there: 02987A10) differently; the hook wraps the whole function, so check that it still means the same thing
warning: hooks.txt:84: the USA_V0 build compiled 025A4218 (there: 025834D8) differently; the hook wraps the whole function, so check that it still means the same thing
warning: hooks.txt:85: the USA_V0 build compiled 025A42CC (there: 02583520) differently; the hook wraps the whole function, so check that it still means the same thing
warning: hooks.txt:86: the USA_V0 build compiled 025A4B98 (there: 02583678) differently; the hook wraps the whole function, so check that it still means the same thing
warning: hooks_base_hud.txt:4: the USA_V0 build compiled 0252D9C0 (there: 02514488) differently; the hook wraps the whole function, so check that it still means the same thing
recomp: USA_V0, 81492 functions, 61 hooks, 21 sites, 4 workers
recomp: hooks skipped: hooks_menu_cursor.txt (only for the USA EU NSMBU_NSLU_USA NSMBU_NSLU_EU build)
recomp: hooks skipped: hooks_nslu_notice.txt (only for the USA EU NSMBU_NSLU_USA NSMBU_NSLU_EU build)
recomp: 101 register helpers, 88.8 s
recomp: float analysis, 81492 functions, one process
recomp: float analysis checked 50000 functions, 56204 waiting, 14.1 s
recomp: float analysis checked 100000 functions, 30163 waiting, 30.3 s
recomp: float analysis checked 150000 functions, 8146 waiting, 54.2 s
recomp: float analysis finished, 175727 function analyses, 87.8 s
recomp: translating 81492 functions into 93 files
recomp: translated 4414 of 81492 functions, 330.1 s
recomp: translated 9017 of 81492 functions, 369.0 s
recomp: translated 13561 of 81492 functions, 402.6 s
recomp: translated 18223 of 81492 functions, 415.7 s
recomp: translated 22986 of 81492 functions, 424.6 s
recomp: translated 27322 of 81492 functions, 441.5 s
recomp: translated 32310 of 81492 functions, 447.9 s
recomp: translated 36688 of 81492 functions, 460.2 s
recomp: translated 40996 of 81492 functions, 470.4 s
recomp: translated 46229 of 81492 functions, 479.4 s
recomp: translated 51164 of 81492 functions, 486.0 s
recomp: translated 56257 of 81492 functions, 494.3 s
recomp: translated 60794 of 81492 functions, 500.3 s
recomp: translated 65344 of 81492 functions, 505.4 s
recomp: translated 69865 of 81492 functions, 512.0 s
recomp: translated 74696 of 81492 functions, 526.2 s
recomp: translated 79320 of 81492 functions, 537.3 s
recomp: translated 81492 of 81492 functions, 538.2 s
recomp: writing headers
build: USA_V0 (title 0005000010101d00)
hooks skipped: hooks_menu_cursor.txt (only for the USA EU NSMBU_NSLU_USA NSMBU_NSLU_EU build)
hooks skipped: hooks_nslu_notice.txt (only for the USA EU NSMBU_NSLU_USA NSMBU_NSLU_EU build)
hook on a function this build compiled differently: 029D9560 (here 02987A10, hooks.txt:65)
hook on a function this build compiled differently: 025A4218 (here 025834D8, hooks.txt:84)
hook on a function this build compiled differently: 025A42CC (here 02583520, hooks.txt:85)
hook on a function this build compiled differently: 025A4B98 (here 02583678, hooks.txt:86)
hook on a function this build compiled differently: 0252D9C0 (here 02514488, hooks_base_hud.txt:4)
hook skipped, function not in this build: 0252F900 (hooks.txt:26)
hook skipped, function not in this build: 029D7648 (hooks.txt:74)
functions: 81492
files: 93
fixpoint rounds: 3
functions with a name from nsmbu/headers: 0 of 81492
imports used: 386 of 2821
CR writers: 199675, 157 with no live bit (liveness on)
leaf functions with registers in locals: 51710
functions with calls with registers in locals: 29708
nonleaf call reloads: 601138 written, 2222274 left out, 167754 calls (live-only on)
functions that change callee-saved registers (save/restore helpers): 101
single-precision multiplier operands: 890 rounded (round25), 20060 known single
unhandled instruction kinds:
      37  op=0
  93 source files (1015 s)
  compiled in 1232 s
  Linking...
  Built C:\Users\manol\Downloads\NSMBU-v0.9.1-windows-x86_64\NSMBU-v0.9.1-windows-x86_64\data\bin\nsmbu.exe
  Precompiling shaders.
  OK: C:\Nueva carpeta\New SUPER MARIO BROS U [Game] [0005000010101d00] (USA_V0, title version 0)
  Using llvm-mingw llvm-mingw-20260922-ucrt-x86_64.
  Using the game files in C:\Nueva carpeta\New SUPER MARIO BROS U [Game] [0005000010101d00] (not copied)
warning: hooks.txt:65: the USA_V0 build compiled 029D9560 (there: 02987A10) differently; the hook wraps the whole function, so check that it still means the same thing
warning: hooks.txt:84: the USA_V0 build compiled 025A4218 (there: 025834D8) differently; the hook wraps the whole function, so check that it still means the same thing
warning: hooks.txt:85: the USA_V0 build compiled 025A42CC (there: 02583520) differently; the hook wraps the whole function, so check that it still means the same thing
warning: hooks.txt:86: the USA_V0 build compiled 025A4B98 (there: 02583678) differently; the hook wraps the whole function, so check that it still means the same thing
warning: hooks_base_hud.txt:4: the USA_V0 build compiled 0252D9C0 (there: 02514488) differently; the hook wraps the whole function, so check that it still means the same thing
recomp: USA_V0, 81492 functions, 61 hooks, 21 sites, 4 workers
recomp: hooks skipped: hooks_menu_cursor.txt (only for the USA EU NSMBU_NSLU_USA NSMBU_NSLU_EU build)
recomp: hooks skipped: hooks_nslu_notice.txt (only for the USA EU NSMBU_NSLU_USA NSMBU_NSLU_EU build)
recomp: 101 register helpers, 75.4 s
recomp: float analysis, 81492 functions, one process
recomp: float analysis checked 50000 functions, 56204 waiting, 18.9 s
recomp: float analysis checked 100000 functions, 30163 waiting, 43.9 s
recomp: float analysis checked 150000 functions, 8146 waiting, 66.3 s
recomp: float analysis finished, 175727 function analyses, 92.4 s
recomp: translating 81492 functions into 93 files
recomp: translated 4414 of 81492 functions, 184.6 s
recomp: translated 8845 of 81492 functions, 208.1 s
recomp: translated 13003 of 81492 functions, 221.1 s
recomp: translated 17560 of 81492 functions, 232.9 s
recomp: translated 21650 of 81492 functions, 240.8 s
recomp: translated 26140 of 81492 functions, 250.2 s
recomp: translated 30362 of 81492 functions, 257.9 s
recomp: translated 34668 of 81492 functions, 263.8 s
recomp: translated 39477 of 81492 functions, 272.1 s
recomp: translated 44093 of 81492 functions, 281.3 s
recomp: translated 48932 of 81492 functions, 289.2 s
recomp: translated 53413 of 81492 functions, 297.0 s
recomp: translated 58540 of 81492 functions, 303.3 s
recomp: translated 63804 of 81492 functions, 310.5 s
recomp: translated 68190 of 81492 functions, 317.2 s
recomp: translated 72451 of 81492 functions, 324.0 s
recomp: translated 76569 of 81492 functions, 331.1 s
recomp: translated 81492 of 81492 functions, 342.2 s
recomp: writing headers
build: USA_V0 (title 0005000010101d00)
hooks skipped: hooks_menu_cursor.txt (only for the USA EU NSMBU_NSLU_USA NSMBU_NSLU_EU build)
hooks skipped: hooks_nslu_notice.txt (only for the USA EU NSMBU_NSLU_USA NSMBU_NSLU_EU build)
hook on a function this build compiled differently: 029D9560 (here 02987A10, hooks.txt:65)
hook on a function this build compiled differently: 025A4218 (here 025834D8, hooks.txt:84)
hook on a function this build compiled differently: 025A42CC (here 02583520, hooks.txt:85)
hook on a function this build compiled differently: 025A4B98 (here 02583678, hooks.txt:86)
hook on a function this build compiled differently: 0252D9C0 (here 02514488, hooks_base_hud.txt:4)
hook skipped, function not in this build: 0252F900 (hooks.txt:26)
hook skipped, function not in this build: 029D7648 (hooks.txt:74)
functions: 81492
files: 93
fixpoint rounds: 3
functions with a name from nsmbu/headers: 0 of 81492
imports used: 386 of 2821
CR writers: 199675, 157 with no live bit (liveness on)
leaf functions with registers in locals: 51710
functions with calls with registers in locals: 29708
nonleaf call reloads: 601138 written, 2222274 left out, 167754 calls (live-only on)
functions that change callee-saved registers (save/restore helpers): 101
single-precision multiplier operands: 890 rounded (round25), 20060 known single
unhandled instruction kinds:
      37  op=0
  93 source files (663 s)
  compiled in 637 s
  Linking...
  Built C:\Users\manol\Downloads\NSMBU-v0.9.1-windows-x86_64\NSMBU-v0.9.1-windows-x86_64\data\bin\nsmbu.exe
  Precompiling shaders.


### Version

v0.9.1

### Game version

USA base disc

### Platform

Windows

### Hardware

Intel(R) Core(TM) i5-3210M CPU @ 2.50GHz, 4 GB

### Logs

dale

### From the app

The game stopped before shader precompile finished (exit code 1).
[boot] New Super Mario Bros. U v0.9.1 (f99ee83), Windows 10 22H2 (build 19045.6466)
[boot] game version USA base disc (USA_V0, title 0005000010101d00, title version 0)
[gfx] renderer: Vulkan (default)
[boot] loaded C:\Nueva carpeta\New SUPER MARIO BROS U [Game] [0005000010101d00]/code/red-pro2.rpx (USA base disc / USA_V0, title 0005000010101d00 version 0): entry 02AA2E50 sda 10231120 sda2 10008000 data end 10229104
[boot] save dir C:\Users\manol\Downloads\NSMBU-v0.9.1-windows-x86_64\NSMBU-v0.9.1-windows-x86_64\data\work\shader-warm\save (no rp_savedata.dat yet)
[mem] default heap 1022A000-50000000
FATAL: Vulkan renderer could not start: Vulkan is not installed on this computer: the Vulkan runtime (vulkan-1.dll) could not be loaded (Failed loading vulkan-1.dll: No se puede encontrar el módulo especificado.).
Install the newest graphics driver for this GPU (from the AMD, Intel or NVIDIA website, or through Windows Update) and start the game again. If no newer driver exists, this GPU cannot run the game's Vulkan renderer.
Run Setup again and choose Repair to retry. Shaders also compile the first time a scene needs them.

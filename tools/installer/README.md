# Setup (first start)

The release packages contain no game files and no game code. The first start of **NSMBU**
builds the game on the player's machine from their own dump (a disc image, a Cemu archive or an
extracted game folder): it extracts the game (a disc image or archive only), translates its code to C, compiles it with a pinned compiler
and links it with the prebuilt runtime. Later starts launch the built game directly. Player
instructions are in the main README ("Install").

## Portable releases

A release folder contains `portable.txt`. Then everything stays in `<release>/data`:

| path | what |
|---|---|
| `data/bin/nsmbu` (`.exe`) | the built game; `data/bin/portable.txt` puts the runtime in portable mode |
| `data/game/` | game files extracted from a disc image or Cemu archive (an extracted folder chosen by the player is used where it is) |
| `data/nsmbu/save/` | saves |
| `data/nsmbu/` | settings, controls, graphics options, save states, shader caches (`host::portable_user_dir()`) |
| `data/nsmbu/logs/` | crash logs and run logs (the game runs in `data/`); player paths for bug reports are in [docs/logs.md](../../docs/logs.md) |
| `data/install.json`, `data/setup.log` | what was prepared (paths inside `data/` relative, so the folder can move), and the setup log |
| `data/toolchain/`, `data/python/` | the downloaded compiler (Windows, Linux; kept by default for repair) and Python (Linux without Python 3; the Windows release ships its Python in `tools/python`) |

Runtime side: a `portable.txt` next to the game executable makes `host::config_dir()` return
`<folder>/nsmbu` (`runtime/src/platform/host.h`); `main.cpp` points the macOS-only paths (save states,
display settings, Metal shader cache) there through their existing overrides; `gfx/metal/menu.mm` keeps the
graphics options in `nsmbu/graphics.plist` instead of NSUserDefaults; the Controls window does not
autosave its frame. Without the marker (source builds) nothing changes. Shortcuts (Applications link,
Start menu, applications menu) are only created when the player asks for one.

Without `portable.txt` (a Linux AppImage: its mount is read-only, issue #55) the same tree lives in
the per-user folders of earlier releases instead: `data/` above becomes
`~/Library/Application Support/nsmbu`, `%LOCALAPPDATA%\NSMBU` or `$XDG_DATA_HOME/nsmbu`
(`~/.local/share/nsmbu`), and `data/nsmbu/` becomes that platform's `host::config_dir()`
(`~/.config/nsmbu` on Linux). `setup.py default_data_dir()` and `gui/setup_gui.cpp data_dir_of()`
implement the same rule; `tools/release/appimage.py` drops `portable.txt` from the package for exactly
this reason. `install.json` then keeps `exe` and `game_dir` as absolute paths (`rel_to_data` is a
no-op without the marker).

Source builds via the `nsmbu` CLI set `NSMBU_USER_DIR` to the gitignored `<repo>/nsmbu` folder (settings,
states, mods, shader cache) and pass `--save nsmbu/save`.
Portable releases keep using `data/nsmbu` (settings and `save/`) through `portable.txt` next to the binary
(`portable.txt` wins if `NSMBU_USER_DIR` is also set).
The setup importer also reads `<repo>/nsmbu` when this tree is a source checkout.

## Pieces

| file | what |
|---|---|
| `setup.py` | all of the installation logic (Python 3.8+, standard library only) |
| `toolchains.json` | pinned compilers and Python downloads (URL + SHA-256); CI builds with the same ones |
| `gui/setup_gui.cpp` | **NSMBU**, the program a release starts: first-start setup, then the game launcher (SDL3 + Dear ImGui) |
| `install-macos.command` | setup in Terminal, macOS (shipped as `tools/Setup in Terminal.command`) |
| `install-windows.bat` | setup in a console window, Windows (`tools/Setup in a console window.bat`): runs `NSMBU.exe --console-setup`, which runs `setup.py` in that console with the bundled Python (`tools/python`) |
| `install-linux.sh` | setup in a terminal, Linux (`tools/setup-in-terminal.sh`; falls back to a pinned standalone Python) |
| `test_setup.py` | unit tests of the helpers (`python3 tools/installer/test_setup.py`) |

In a release folder the program is `NSMBU.app` (macOS), `NSMBU.exe` (Windows) or
`nsmbu-launcher` plus `NSMBU.desktop` (Linux); the terminal setup in `tools/` is the fallback.

## NSMBU (the program a release starts)

On start, when `data/install.json` says the game is prepared for this release (same version as
`sdk/manifest.json`, the executable and the game files are there), it starts the game right away and
shows no window of its own (macOS, Linux: `exec`, so the Dock keeps "NSMBU"; Windows: starts
the game without a console window and exits). Otherwise, or with Shift held at start (macOS,
Windows) or `--setup`, it shows the setup.

The setup is only a front end: it runs the release's terminal setup with `--gui-protocol` as a child
process (so Python is found or fetched exactly as in the terminal setup; on Windows it starts
`setup.py` directly with the Python the release ships, see below) and talks to `setup.py`
over its stdin/stdout. Screens: welcome (or, when installed: play / update / repair / reinstall /
import saves or settings / open the folder), choose the disc image, Cemu archive or game folder (native file dialogs), keys
(disc images only: disc key and common key typed or pasted as 32 hex digits, or taken from a key file next to the image
or chosen), installation
(a bar per step, an overall bar, and the setup log), optional save import (an NSMBU
`rp_personal_savedata.dat`
folder, or the saves and settings of an
earlier installation or another release folder, copied), done (Play, Open folder, Quit; in a portable
release also: remove the downloaded compiler, add a shortcut), and error screens with Retry and Copy
log. On macOS it first checks for Apple's Command
Line Tools and offers Apple's installer, as the Terminal launcher does.

Finding the release folder: the program looks for `tools/installer/setup.py` next to itself (macOS: next
to `NSMBU.app`). A downloaded app opened from Finder on macOS runs from a random read-only copy
of the bundle alone (App Translocation, `/private/var/folders/.../AppTranslocation/<id>/d/`), so it
asks Security.framework (`SecTranslocateIsTranslocatedURL`, `SecTranslocateCreateOriginalPathForURL`,
loaded at run time) where the original bundle is and uses the folder around it. The question is about
the bundle itself: those functions fail for paths that do not exist, such as `Contents/Resources`
(issue #48). The terminal launcher then removes the quarantine from the release folder, so later starts
are not translocated. Before the setup starts, the data folder is checked for writing (a disk image, a
read-only drive). Every "Setup could not continue" screen says what failed (path, operation, error
text) and what to do, offers to show the folder involved, and appends the same text to
`data/setup-window.log` (or, when there is no writable release folder, `~/Library/Logs/NSMBU
setup.log` on macOS, `%TEMP%\NSMBU setup.log` on Windows, `$TMPDIR/nsmbu-setup-log.log`
on Linux).

Build: `-DNSMBU_SETUP_GUI=ON` adds the `nsmbu-setup` target (`cmake/SetupGui.cmake`). SDL3 is linked
statically on macOS and Windows (pinned source, release toolchain); Linux uses the shared SDL3 the
release ships in `sdk/runtime`. The ImGui SDL3 and SDL_Renderer backends are the unmodified ones of
the vendored ImGui release.

## Windows: the bundled Python

The Windows release ships the official embeddable Python (`python.windows` in `toolchains.json`)
unmodified in `tools/python`: the release workflow downloads the pinned zip, `tools/release/package.py
--windows-python` checks it against the pin and every file against `tools/release/python-windows-files.json`
and unpacks it, and `tools/release/guard.py` allows exactly those files there. Its programs and DLLs keep
the Python Software Foundation's signature (the release workflow checks it); its license is in
`third-party-licenses/Python.txt`. It lives in the release folder, not in the data folder, because it belongs
to the release (like `tools/` and `sdk/`): the same place works for portable and per-user (`%LOCALAPPDATA%\NSMBU`)
installations and for `--data-dir`/`NSMBU_DATA_DIR`, and a newer release brings its own.

`NSMBU.exe` starts `tools\python\python.exe tools\installer\setup.py --gui-protocol` directly
(`gui/console_setup_win.cpp`); `--console-setup ARGS` (the first argument; used by
`tools\Setup in a console window.bat`) runs `setup.py ARGS` in the console it was started from. If
`tools\python` is missing, both say that the release is incomplete and should be unzipped again. The program
contains no download code and starts no script host; `setup.py` uses the Windows API through ctypes for its
file dialogs and shortcuts. Until 0.2.6 a PowerShell script started with `-ExecutionPolicy Bypass` removed
the "mark of the web" from every file in the release folder and downloaded Python; antivirus heuristics read
that as a dropper (issue #58). Nothing needs the mark removed: SmartScreen asks once for `NSMBU.exe`
and remembers "Run anyway", and programs started with CreateProcess (Python, the extractor, the compiler,
the game) and DLLs are not checked for it.

## Windows on ARM64

There are two Windows releases, `windows-x86_64` and `windows-arm64`.
The release job builds each on its own runner (`windows-latest`, `windows-11-arm`) with the matching pinned llvm-mingw (`llvm-mingw-20260922` or `llvm-mingw-20260922-aarch64`, whose `triple` is `aarch64-w64-mingw32`).
The arm64 release ships the embeddable Python `python.windows-arm64`, checked against `tools/release/python-windows-arm64-files.json`.
`tools/release/guard.py` picks that list from the `windows-arm64` part of the package name.

## Windows: long paths

Windows limits ordinary paths to 259 characters.
Setup unpacks, moves and deletes the compiler with `\\?\` paths (`longpath()` in `setup.py`), because the deepest files of llvm-mingw pass that limit below a deep release folder.
The game and its tools use ordinary paths, so setup stops before any download when the data folder is longer than `MAX_DATA_DIR_CHARS` (179).
The deepest name below the data folder is the shader cache file `nsmbu\shadercache\vulkan-<8 hex>-<8 hex>-<32 hex>.bin` (79 characters, `DEEPEST_DATA_PATH`).
A Python that joins paths with `/` (MSYS2) keeps ordinary paths, so `test_setup.py LongPaths` runs on MSYS2 Python and on python.org Python in `windows.yml`.
The release workflow repeats the check in a 174 character release folder, and in one 175 characters deep that must stop with exit code 1.

## Linux on x86-64 and arm64

There are two Linux releases, `linux-x86_64` and `linux-aarch64`, built by the same release job on
an x86-64 and an arm64 runner. Each records its toolchain in `sdk/manifest.json`: `zig-0.16.0`
(x86-64 host, target `x86_64-linux-gnu.2.35`) or `zig-0.16.0-aarch64` (arm64 host, target
`aarch64-linux-gnu.2.35`), both pinned with their SHA-256 in `toolchains.json`, so the game code a
player compiles always matches the prebuilt runtime. `setup.py` refuses a release for the other
architecture (`uname -m`), and the terminal setup fetches the matching standalone Python
(`python.linux` or `python.linux-aarch64`) when the system has no Python 3.8+.

On Linux arm64 the runtime reserves the guest's 4 GB at 64 GiB instead of 32 TiB (`PPC_MEM_BASE`,
as on Android): many arm64 kernels (Raspberry Pi OS and other 4K-page configurations) give processes
only a 39-bit (512 GiB) address space. Plain `char` is built signed there (`-fsigned-char`, also
for the player-compiled game code), as on x86-64 and Apple arm64.

Options: arguments it does not know are passed to `setup.py` (`--data-dir DIR`, `--app-dir DIR`,
`--jobs N`). For tests: `--self-test` (start setup.py, wait for its hello, render the first screen,
exit 0), `--automate SCRIPT.json` (scripted clicks, see the comment in `setup_gui.cpp`) and
`--screenshots DIR`. With `SDL_VIDEO_DRIVER=offscreen` nothing is shown on screen (software
rendering), which is how CI and local tests run it.

## `setup.py --gui-protocol`

One JSON object per line. Events on stdout all have `"event"`:

| event | fields |
|---|---|
| `hello` | `version`, `platform`, `data_dir`, `app_dir`, `log`, `portable`, `package`, `installed` (state or null), `game_files`, `game_dir`, `save_exists`, `legacy` (an earlier installation to copy from), `free_bytes`, `toolchain` |
| `log` | `text` (also written to `setup.log`) |
| `plan` | `steps`: `[{id, title}]` of the installation that starts |
| `step` | `n`, `total`, `title`, `id` (`keys`, `archive`, `folder`, `compiler`, `extract`, `copy`, `translate`, `compile`, `app`, `shaders`) |
| `progress` | `label`, `done`, `total`, `detail` (including shader warm counts during the `shaders` install step) |
| `shader` | `phase`, `done`, `total`, `detail` while the optional `shaders` command is running |
| `reply` | `id` and `cmd` of the request, `ok`; on failure `problem` and `message` |
| `fatal` | `message` (setup cannot run, e.g. the wrong release for this system) |

Requests on stdin: `{"cmd": ..., "id": n, ...}`

| cmd | fields | reply |
|---|---|---|
| `probe` | `path` | `kind` (`image`/`archive`/`folder`), `disc_key` (found next to the image), `common_key` (where one was found); for a folder `in_place`, `bytes`; for an archive `title`, `folder`, `bytes`, `message` (which title is used and why); problem `wrong_title` when the archive has no usable game |
| `check_keys` | `image`, optional `disc_key_file` or `disc_key_hex`, `common_key_file` or `common_key_hex` | `title_id`, `files`, `bytes`; problems `disc_key_wrong`, `common_key_wrong`, `wrong_title`, ... |
| `install` | `source` (`image`/`archive`/`folder`/`installed`), `path`, optional `jobs` | streams `plan`/`step`/`progress`/`log` (including a required `shaders` step after the game is built, except for placeholder `--gen-dir` builds), then `app`, `exe`, `data_dir`, `game_dir`, `toolchain_bytes` |
| `import_save` | `kind` (`nsmbu`), `path`, optional `replace` | `message`; problem `exists` when a save is installed and `replace` is not set (with `replace`, the old save is moved to `nsmbu/save.backup-<time>` first) |
| `import_existing` | optional `path` (another release folder; none: the earlier per-user installation), `replace` | `message` (saves and settings copied, never moved; problem `exists` as above) |
| `remove_toolchain` | | `freed` bytes |
| `shortcut` | | `path` of the created shortcut |
| `launch` | | starts the installed game |
| `shaders` | | runs the installed game in the background until the shaders it draws have compiled, streaming `shader` events (`phase`, `done`, `total`, `detail`) and then `compiled`; `shaders_cancel` on stdin during that run stops it and keeps shaders already compiled (`not_installed`, `missing_exe`, `missing_game`, `shaders`, `spawn`) |
| `quit` | | |

Keys: an image install uses the keys from the last successful `check_keys`; they stay in the
`setup.py` process's memory and are dropped when the installation starts extracting. Requests are
never logged or echoed, and no key is written to disk or passed on a command line.

## Disc image keys

A disc image needs its disc key and the Wii U common key. On the Keys screen each one is typed or
pasted as 32 hex digits, or taken from a key file. The disc key belongs to that one image; the common
key is the same on every Wii U.

For both keys the pasted text is the first option: `disc_key_hex` and `common_key_hex` in the protocol
above, with the key file next to the image used when nothing is pasted. A key file holds one of:

- 16 raw bytes;
- one line of 32 hex digits (spaces, dashes and a leading `0x` are ignored);
- a Cemu `keys.txt` with a `common = <32 hex>` line and a `disc = <32 hex>` line.

UTF-8 and UTF-16 files work, with or without a byte order mark (Notepad's "UTF-8 with BOM",
"Unicode" and "UTF-8" encodings all do).

A `keys.txt` next to the image or in the release folder is used too: its `common` line is the common key,
its `disc` line is the disc key. There a bare line is not used as the common key (it is usually the disc
key). Title-key lines (`<title id> = <32 hex>`) are not keys for these two, and a file with only a title
key is rejected with a message that names the title id.

## Cemu archives (`.wua`)

A `.wua` is Cemu's Wii U archive: a [ZArchive](https://github.com/Exzap/ZArchive) file (zstd-compressed
64 KiB blocks, SHA-256 of the whole file in its footer) with already decrypted titles, one folder per
title named `<title id>_v<version>`, often the game, its update and DLC together. No keys are needed:
the GUI skips the key screen and the terminal setup asks for none.

`nsmbu-extract --title 0005000010101d00 info GAME.wua` lists every title folder and the one selected;
setup then decides (`archive_choice` in `setup.py`) and logs which title it uses and why:

- a supported game title is used: USA `0005000010101d00_v64` or Europe `0005000010101e00_v80`
  (a Cemu archive that already merged the update into the game folder), or a base disc at version 0
  (`0005000010101d00_v0` or `0005000010101e00_v0`);
- a lone update title (`0005000e…`) without the base game stops with an explanation.
  DLC or other titles are listed as not used.
- an extracted folder that is only the update (update title id, or no base
  `content/Common/course_res_pack/1-1.szs`) is rejected the same way;
- an unsupported region, an archive with only the update, another game, or the wrong title version
  stop with an explanation.

`nsmbu-extract --title FOLDER --progress extract GAME.wua data/game.partial` first checks the archive's
SHA-256 ("phase verify", the step's bar shows "checking the archive"), then writes the title folder's
`code`, `content` and `meta` ("phase extract"); damaged archives (exit 8), truncated or foreign files
(exit 7) and a missing title (exit 10) are reported before anything is written. The space check is
the selected title's size plus 1 GB, as for a disc image. Tests: `extract_wua` (ctest, a synthetic
archive written by the test) and `ArchiveTitles` in `test_setup.py`.

## Game version check

The hooks (`tools/recomp/hooks*.txt`) use canonical USA addresses.
Setup accepts `code/red-pro2.rpx` from New Super Mario Bros. U USA (`00050000-10101d00`, title version 64
or base disc 0) or Europe (`00050000-10101e00`, title version 80 or base disc 0).
The build registry (`tools/recomp/builds.py` and `tools/recomp/builds/*.json`) keeps those SHA-256
values (checksums only: they identify the file and contain nothing of it;
`guard.py` flags 32-digit, key-shaped strings, not 64-digit sums) and `check_game_version` compares
them for every source before anything is translated: an extracted folder at the "folder" step (and
already when the window probes it), a disc image or Cemu archive right after extracting (into
`game.partial`, which is then removed, so an earlier `game/` stays), a repair or update with the
installed files.
Europe uses a USA→EU address map; see [docs/builds.md](../../docs/builds.md).

## Disc image update folder

A disc image is the base game.
The matching 1.3.0 update is optional: an extracted folder (`code/`, `content/`, `meta/`).
Setup looks for `update/` next to the disc image, accepts `--update-dir`, or asks (terminal / Keys screen).
Without it, the USA or Europe base disc (title version 0) still works.
`check_update_folder` checks the update RPX and that build's title version before extract.
After the base extract, `apply_update_to` merges that folder when present, then `check_game_version` runs on the result.
Tests: `UpdateFolder` and `GameVersion` in `test_setup.py`.

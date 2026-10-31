# Test, CI, and release proposals from WWHD

## Source

NSMBU last merged upstream at `defb89f` (2026-10-09).
This plan reads Wind Waker HD `main` at `0a53327a` (v0.4.0, 2026-10-21) in `vendor/ZeldaWWHDRecomp`.
The proposals cover test isolation, scripted compare, CI, and desktop release packages.

The Android setup APK, compiler cache, and device smokes stay in [android-wwhd-port.md](android-wwhd-port.md).
If a release ships `template.bin`, pin the commit and the SHA-256 and fail the release when the pin fails.

## 1. Give each ctest its own user folder

`button_glyphs_test` wrote the player's `controls.json`.
WWHD now sets `WWHD_USER_DIR` on every ctest at the end of `CMakeLists.txt` (`wwhd_isolate_test_user_folders`).
The folder is `${CMAKE_BINARY_DIR}/test-users/<test>`.
A test that already sets `WWHD_USER_DIR` keeps that value.
`runtime/tools/user_folder_isolation_test.cmake` points `HOME`, `APPDATA`, and `XDG_CONFIG_HOME` at an empty tree and fails if that tree gains files.

NSMBU sets `NSMBU_USER_DIR` only in a few tests, such as `runtime/test/crash_log_test.cmake`.
Other tests can write the developer `nsmbu/` folder.

- Add the same deferred CMake pass for `NSMBU_USER_DIR`.
- Add one ctest that saves a settings file and checks the empty home tree.
- Done when `ctest` leaves `nsmbu/` and the home tree unchanged.

## 2. Give each playtest its own userdata

`nsmbu test` runs cases in parallel (`NSMBU_TEST_JOBS`, default 2).
Each case has `.tmp/playtest/<name>/` for the save, the control socket, and the log.
`tools/cli/src/playtest/session.ts` does not set `NSMBU_USER_DIR`.
The launch path still uses the shared `nsmbu/` tree for settings and the shader cache.

WWHD scripted runs set the shader cache and the state directory under the run folder (`tools/gametest/run_scenario.sh`).

- Set `NSMBU_USER_DIR` to `.tmp/playtest/<name>/user` for each case.
- Copy a save into that tree when the case asks for the repo save.
- Done when two cases run together and each tree has its own settings and shader cache.

## 3. Compare two scripted runs

WWHD `tools/gametest/run_scenario.sh` boots a hidden, muted game on a copy of a save.
It retries a boot that never reaches the scenario (`BOOT_TRIES`, default 3) and keeps `bootfail` logs.
`WWHD_PROC_DIGEST` writes a hash of each process after execute.
`tools/gametest/compare.py` aligns two runs at the first player update (`ALIGN=link`) and diffs the traces.
`WWHD_TEST_FIXED_SEED` freezes guest random seeds and rebases `WWHD_PRESS` and frame dumps onto the first gameplay frame (`runtime/src/fixed_seed.h`).
Host speed changes the boot length, so a press on a raw TV frame hits a different moment.

NSMBU playtests wait on scene helpers.
`nsmbu scripted` times `NSMBU_PRESS` and `NSMBU_DUMP_FRAMES` in TV frames.
Two builds are compared by eye from those frame dumps.

- Record one status line per logic step (scene, course, player position) into the run folder.
- Align two runs on the first frame of a named scene (map or course), then diff the lines.
- Rebase scripted presses and dumps from that scene when `NSMBU_TEST_FIXED_SEED=1`.
- Freeze host-visible random state at that scene if two runs of one build diverge before input.
- Retry a boot that misses the scene, and keep the failed log.
- Done when the same build compares clean, and a known bad build fails with the first step number.

Do not copy Wind Waker process addresses or `cM_rnd` seeds.

## 4. Run the bench tests in CI

Linux and Windows CI in WWHD run `python -m unittest discover -s tools/bench -p "test_*.py"` on synthetic fixtures.
They also run `tools/test_shaderprep.py`.
Tag pushes do not start those workflows (`branches: ["**"]`).
A release tag already points at a commit that CI tested.
Push to any branch still runs CI.

NSMBU `tools/bench/test_bench_lib.py` exists.
`.github/workflows/linux.yml` and `windows.yml` do not run it.
Those workflows run on `main` and on pull requests.

- Run the bench unittest discover on Linux and Windows CI.
- Add synthetic tests for `tools/shader/shaderprep.py` and run them in the same step.
- Run Linux, Windows, and Android CI on every branch push, and keep tag pushes out.
- Done when a bench failure fails the workflow, and a `v*` tag push does not start `linux.yml`.

## 5. Test Windows setup in a deep folder

WWHD issue #144: setup unpacks the compiler with `\\?\` paths.
One character past the accepted folder depth, setup stops before any download.
`release.yml` builds in a deep folder, checks that the deepest compiler file is past `MAX_PATH`, and checks the refusal exit code.
Windows CI runs `test_setup.py LongPaths` on MSYS2 Python and on python.org Python 3.14.
The release Python uses backslashes.
MSYS2 Python joins paths with `/` and can miss the limit.

NSMBU `tools/installer/setup.py` unpacks the toolchain with `tar` into `toolchain/unpack.tmp`.
That path uses ordinary path strings.

- Unpack with long-path prefixes on Windows, and stop with a clear message when the folder is too deep.
- Cover both Pythons in `windows.yml`.
- Repeat the deep-folder self-test in `release.yml`.
- Done when the deepest compiler path in the test is longer than 259 characters and the too-deep run exits 1 before download.

## 6. Pin the AppImage runtime

`tools/release/appimage.py` in WWHD pins the type2 runtime by SHA-256 (`RUNTIME_COMMIT` `8f39b89`).
The download URL is their own release asset `appimage-runtime-8f39b89`.
Upstream `continuous` moves and would fail the pin.
License texts ship in `third-party-licenses/`.
`guard.py` checks that they are present.
`appimage.py --extract-runtime` recovers the runtime from a built AppImage.

NSMBU pins appimagetool.
The AppImage runtime is whatever appimagetool fetches.

- Pin the runtime bytes, host that pin as a release asset, and fail the pack on a hash mismatch.
- Ship the runtime license texts and extend `guard.py`.
- Done when a Linux release AppImage reports the pinned runtime version.

## 7. Ship Windows on ARM64

WWHD `release.yml` builds `windows-x86_64` and `windows-arm64`.
The arm64 job uses the `windows-11-arm` runner, `llvm-mingw` `aarch64-w64-mingw32`, and `python.windows-arm64`.
`tools/release/package.py`, `guard.py`, and `notes.py` name that platform.

NSMBU release jobs are macOS arm64, Windows x86-64, and Linux x86-64 and arm64.

- Add the arm64 matrix row, the Python pin, and the package name `windows-arm64`.
- Run ctest on that runner before packaging.
- Done when the release workflow uploads `NSMBU-*-windows-arm64.zip` and `guard.py` accepts it.

## 8. Publish a rolling development build

WWHD `release.yml` has a nightly schedule (`23 2 * * *`).
The schedule starts a build on `devel` only when `devel` moved since the `devel-build` tag.
The published version is the last release plus the commit, for example `v0.2.11-devel.1a2b3c4d`.
A bare `devel-...` string made every catalogue mod look unavailable.
The pre-release name is `devel-build`.
Each run replaces that release and tag.
It is never marked latest.
`notes.py --devel` prints the unreleased changelog block.
Development builds skip the VirusTotal job.

NSMBU publishes on a `v*` tag on `main`.
Players get that tagged release only.
There is no `devel` branch; the rolling build tracks `main`.

- Schedule the same check on `main`.
- Publish `devel-build` from `main` when `main` moved, with version `<last tag>-devel.<sha>`.
- Point `notes.py` at `## [Unreleased]`.
- Done when a second run on the same commit does not replace the pre-release, and a real `v*` release stays the latest release.

# Android on-device setup

## Status

The port follows the WWHD recomp's Android host (setup service, embedded CPython, Android-hosted compiler, touch controls).
Host Python, JVM and C++ tests cover the pieces that run off the device.
Emulator smokes, release CI and real-device checks are pending; see the [device checklist](android-device-checklist.md).
The implementation uses this project's recompiler (`tools/recomp/recomp.py`); no second recompiler is added.
Dual displays, gyro aiming, a 40 fps mode and Android code mods are out of scope.

## Shared setup pipeline

`tools/android/setup_adapter.py` calls the desktop installer's extraction, translation, compilation and linking operations.
`tools/installer/setup.py` and `tools/recomp/recomp.py` keep their desktop behavior.
The adapter supplies Android native process execution and durable checkpoints instead of the desktop install entry point, which chooses desktop toolchains and deletes work.
The game identity is `code/red-pro2.rpx` and the known builds in `tools/recomp/builds.py`.

Android embeds official CPython through a small JNI bridge in the dedicated `:setup` process.
The interpreter runs isolated: no environment or site loading, and no bytecode writes.
`tools/android/prepare_python.py` verifies the pinned official archive and packages the standard library, the shared scripts and the build maps.

## Compiler delivery

`tools/android/build_toolchain.py` builds an Android-hosted clang, lld and ar from a pinned LLVM revision with NDK 30.
The manual workflow in nsmbu-recomp/ci-assets publishes that build as the `android-clang` GitHub Release.
Automatic workflows only download that release.
Apps that target Android 10 or newer cannot run downloaded binaries from writable storage.
So the APK carries the compiler drivers as native libraries (`libnsmbu-*.so`) and unpacks verified data assets privately.
`tools/android/package_toolchain.py`, `package_runtime.py` and `package_extractor.py` write the APK packages with content hashes and versioned metadata.

## Setup flow and recovery

The setup screen chooses a folder, a `.wua` archive or a `.wud`/`.wux` image, and the keys, through Android's document picker.
URI grants are persisted, and document streams are copied without assuming filesystem paths.
Key files are read into memory (at most 4096 bytes each), pass to the worker as private copies, and are never logged or part of a checkpoint identity.
A Cemu `keys.txt` with labelled `common =` and `disc =` lines supplies both keys; a bare key supplies only the role it was chosen for.
An optional extracted update folder is merged for image jobs.

`tools/android/ondevice_setup.py` runs one job: extract, translate, compile, link, activate.
`job.json`, `state.json` and `ready.json` record the job, its progress and the activated build.
A worker lock keeps a second worker out, and `pause.json` or `manual-pause.json` pause the job.
Incomplete stages restart, completed verified outputs are reused, and a failed rebuild keeps the previous build.
The foreground service shows progress and pauses for heat and low battery with hysteresis.

Activation pairs the validated `libgame.so` with its extracted game folder.
`NsmbuActivity` loads that library, passes the folder with `--game`, and keeps a bundled `libmain.so` only for the PC-built route.
Without either, it stops with a message to set the game up first.

## No shader head start

The desktop installer can build a shader head start, but the Vulkan backend reports no head-start support (`kFeatureShaderHeadStart` in `runtime/src/gfx/vulkan/backend_table.cpp`).
Android keeps a pipeline cache per GPU driver under the driver store instead.
So Android setup has no shader stage, the APK never bundles a shader template, and the Android build has no `nsmbu_yaz0` library.

## Phone runtime

- Back opens and closes Settings (`SDL_HINT_ANDROID_TRAP_BACK_BUTTON`, `SDL_SCANCODE_AC_BACK` in `runtime/src/platform/input_sdl.cpp`).
- The activity hides the system bars in full screen and lays out into the display cutout.
- The runtime writes `config/nsmbu/logs/nsmbu.log` on Android too, and `LogShare` shares it and the previous run's log.
  The same share includes the current job's `host.json`, `state.json`, and the newest extract, translation, compile, and link logs under `checkpoints/`.
  Key files and the imported dump are left out.
- `CrashReports` adds Android's recorded exit reasons (`SystemExitReports`) to the crash flow; save-tool stops are marked as expected.
- `GameFilesProvider` is a documents provider for the system Files app; the game and the shader cache are read-only, and key files are hidden.
- `res/xml/game_mode_config.xml` keeps the system game modes from downscaling or overriding the frame rate.

## Heat guard

`runtime/src/platform/heat_guard.h` holds the policy and `runtime/src/platform/perf_hint.cpp` applies it to the thermal headroom forecast.
When the headroom reaches the pause limit, the in-between frames pause; they resume below the resume limit.
`HeatGuardDevice.java` classes fan-cooled handhelds by maker and model and tells native code before the game starts.
The limits are:

- Phones and unknown devices: 0.72 to pause, below 0.60 to resume.
- Fan-cooled handhelds: 1.00 and 0.90.
- Relaxed: 0.90 and 0.80 on phones, 1.10 and 1.00 on handhelds.
- Off: never pauses.

The setting is saved as `heatGuard`.
`NSMBU_HEAT_GUARD` (`normal`, `relaxed`, `off`) overrides it for one start and is not saved.

## Touch controls

`TouchControls.java` draws the controls and drives an SDL virtual gamepad through `runtime/src/platform/touch_pad.cpp`.
The game reads it through the normal controls mapping, and `touch_face.h` presses the pad button that the current mapping binds to each drawn Wii U face button.
NSMBU has no right-stick action, so the touch layout has one stick.
`touch_buttons.h` decides visibility: Automatic hides the buttons while a physical controller is connected, Always show and Hide override that, and a corner touch reveals them for five seconds.
The view button hides with the touch buttons.
`TouchLayout.java` stores the edited layout in the app's preferences.

Phone vibration (`phone_rumble.h`, `PhoneRumble.java`) sends the existing rumble model's player 1 level to the vibrator in 150 ms leases.
By default it runs only while the latest input came from the touch controls.
The setting is saved as `phoneRumble`, and `NSMBU_PHONE_RUMBLE` (`0`, `touch`, `fallback`) overrides it.

## Synthetic fixture isolation

Production setup accepts only known game builds.
Tests register an authored RPX digest through `tools/android/debug_fixture_build.py`; they do not change installer validation or the production build maps.
Release packages omit those fixture sources.
The release Gradle task `verifyReleasePython` and `tools/release/guard.py` reject them in a release APK.
Only debug CI passes `--allow-android-test-fixtures` to the guard.

## Release

`.github/workflows/release.yml` builds `NSMBU-<version>-android-arm64.apk` and `-android-x86_64.apk` against placeholder guest code.
It deletes `libmain.so` before it assembles the APK, guards it, checks the packaged assets and verifies the signature.
A published release needs the `ANDROID_KEYSTORE_B64`, `ANDROID_KEYSTORE_PASSWORD` and `ANDROID_KEY_ALIAS` secrets, and `vars.ANDROID_CERT_SHA256` pins the certificate when set.
The Android compiler and the pinned AppImage runtime come from [nsmbu-recomp/ci-assets](https://github.com/nsmbu-recomp/ci-assets).
Run the Android compiler workflow in that repo when the pinned LLVM revision, NDK, or `tools/android/build_toolchain.py` changes.

## Tests

```sh
python3 -m unittest discover -s tools/android -p 'test_*.py'
python3 -m unittest discover -s tools/installer -p 'test_*.py'
ctest -R 'heat_guard|touch_controls|input_mapping|sdl_input'
```

- `tools/android/test_jvm.py` runs the plain-Java policy tests in `tools/android/*Test.java`, with small Android stubs under `tools/android/stubs`.
- `tools/android/test_app_sources.py` type-checks all app Java against the installed Android platform.
- `tools/android/test_phone_wiring.py` and `test_release_workflow.py` check the JNI names, log paths, manifest entries and release job against each other.

Touch controls and phone vibration are Android-only, so `nsmbu test` playtests do not cover them.

# Findings

## Report

- The user asks whether an Android port is in progress.
- They cite GitHub Actions that appear to build for Android.

## Issue state

- Issue #33 is closed.
- IrishBruse replied on 2026-10-27 and linked issue #23 for Android work.
- The maintainer said bug fixes come before new features.
- The thread has no further discussion or reopen request.

## Changelog

- `CHANGELOG.md` does not mention issue #33.
- Version 0.8.0 on 2026-10-30 added an Android setup app.
- The same release lists Android settings, touch controls, and Android fixes.
- That release date matches the issue’s last update.

## Code and CI

- `.github/workflows/android.yml` builds a placeholder APK on every branch push.
- `runtime/android/` holds the Gradle project, Java entry points, and native build scripts.
- Platform code lives under `runtime/src/platform/` and `runtime/src/gfx/vulkan/` (`android_controller`, `android_driver`).
- `README.md` lists Android among supported platforms.

## Assessment

- The report is an informational question, not a defect report.
- The maintainer’s reply and the repo both confirm active Android port work.
- Issue #33 needs no code change; Android support is already tracked elsewhere.
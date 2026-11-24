# Findings

## Report

- Issue #27 is an enhancement suggestion titled `[SUGGESTION] android`.
- Author luizao-rizz asks for an Android version.
- The body says only: "that would be fenomenal".
- The report gives no platform details, reproduction steps, or feature specifics.

## Status

- State: **CLOSED** (2026-10-27).
- Label: `enhancement`.
- IrishBruse replied: "Yes I am".
- The reply likely confirms active Android work and closes the suggestion.

## Changelog and code

- `CHANGELOG.md` does not cite #27.
- Version **0.8.0** (2026-10-30) added an Android setup app for phones and emulators.
- Later 0.8.x entries list many Android setup, settings, and controls fixes.
- `README.md` lists Android among supported platforms.
- The repo ships Android tooling, runtime, and user docs beyond the suggestion date.

## Likely files and symbols

- `tools/android/` — APK build, on-device setup, packaging tests.
- `runtime/android/` — Gradle app and native integration.
- `docs/user/android/` and `docs/developer/android/` — player and developer guides.
- `runtime/src/overlay/overlay.cpp` — settings **Android** tab, `AndroidTelemetry`, JNI hooks.
- `runtime/src/gfx/vulkan/android_driver.h` — Vulkan on Android.

## Summary

- The issue requested Android support without technical detail.
- It closed with maintainer confirmation before the public Android release.
- The project now documents and ships Android support in 0.8.0 and later.
#!/usr/bin/env bash
# Builds NSMBU.app for iOS (arm64) and packs an unsigned NSMBU.ipa for re-signing (Signulous, AltStore, ...).
#   GEN_DIR   recompiler output (default: build/gen; run `nsmbu game <version> --recomp` first)
#   OUT       build directory (default: runtime/ios/build)
#   IOS_MIN   deployment target (default: 17.0)
#   CPU       -mcpu for the recompiled game (default: apple-a14; apple-a12 runs on every iOS 17 device)
#   SIGN      codesign identity; unset leaves the app unsigned, "-" signs ad hoc with runtime/ios/nsmbu.entitlements
#   SIM=1     build for the iOS Simulator instead (runtime/ios/build-sim, ad hoc signed without entitlements, no .ipa)
#   ICON      square PNG for the home screen icon (default: the first PNG in static/)
#   IPA_NAME  name of the .ipa (default: New Super Mario Bros. U)
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../.." && pwd)"
jobs="${JOBS:-$(sysctl -n hw.ncpu)}"
if [ -n "${SIM:-}" ]; then
    platform=iphonesimulator
    out="${OUT:-$here/build-sim}"
    SIGN="${SIGN:--}"
else
    platform=iphoneos
    out="${OUT:-$here/build}"
fi
sdk="$(xcrun --sdk $platform --show-sdk-path)"

echo "== NSMBU for $platform ${IOS_MIN:-17.0}, SDK $(xcrun --sdk $platform --show-sdk-version), CPU ${CPU:-apple-a14}"
mkdir -p "$out"
cmake -S "$root" -B "$out" -G Ninja \
    -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_ARCHITECTURES=arm64 "-DCMAKE_OSX_SYSROOT=$sdk" \
    "-DCMAKE_OSX_DEPLOYMENT_TARGET=${IOS_MIN:-17.0}" -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER="$(xcrun -f clang)" -DCMAKE_CXX_COMPILER="$(xcrun -f clang++)" \
    "-DNSMBU_IOS_CPU=${CPU:-apple-a14}" "-DGEN_DIR=${GEN_DIR:-$root/build/gen}" > "$out/configure.log" \
    || { tail -40 "$out/configure.log"; exit 1; }
cmake --build "$out" -j "$jobs" --target nsmbu > "$out/build.log" 2>&1 || { grep -E "error|FAILED" "$out/build.log" | head -40; exit 1; }

app="$out/NSMBU.app"
icon="${ICON:-$(ls "$root"/static/*.png 2>/dev/null | head -1)}"
if [ -n "$icon" ]; then
    # no asset catalog: the sizes Info.plist's CFBundleIconFiles names, without alpha (iOS rejects it)
    for spec in AppIcon60x60@2x:120 AppIcon60x60@3x:180 AppIcon76x76@2x~ipad:152 AppIcon83.5x83.5@2x~ipad:167; do
        sips -s format png -z "${spec#*:}" "${spec#*:}" "$icon" --out "$app/${spec%%:*}.png" > /dev/null
    done
else
    echo "no icon (static/*.png or ICON): the home screen shows a blank one"
fi
if [ -n "${SIM:-}" ]; then
    codesign --force --sign "$SIGN" --timestamp=none "$app"  # the Simulator rejects the kernel entitlements
elif [ -n "${SIGN:-}" ]; then
    codesign --force --sign "$SIGN" --entitlements "$here/nsmbu.entitlements" --timestamp=none "$app"
fi
if [ -n "${SIM:-}" ]; then
    echo "$app"
    exit 0
fi
ipa="$out/${IPA_NAME:-New Super Mario Bros. U}.ipa"
rm -rf "$out/ipa" "$ipa"
mkdir -p "$out/ipa/Payload"
cp -R "$app" "$out/ipa/Payload/"
(cd "$out/ipa" && zip -qry "$ipa" Payload)
rm -rf "$out/ipa"
ls -la "$ipa"

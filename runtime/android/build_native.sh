#!/usr/bin/env bash
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../.." && pwd)"
case "$(uname -s)" in
Darwin) host=darwin-x86_64; exe=; default_sdk="$HOME/Library/Android/sdk" ;;
Linux) host=linux-x86_64; exe=; default_sdk="$HOME/Android/Sdk" ;;
MINGW* | MSYS* | CYGWIN*) host=windows-x86_64; exe=.exe; default_sdk="C:/Android/Sdk" ;;
*) echo "unsupported host $(uname -s)"; exit 2 ;;
esac
sdk="${ANDROID_SDK:-${ANDROID_HOME:-${ANDROID_SDK_ROOT:-$default_sdk}}}"
ndk="$sdk/ndk/${NDK_VERSION:-30.0.16248370}"
[ -f "$ndk/build/cmake/android.toolchain.cmake" ] || { echo "no NDK at $ndk (sdkmanager --install \"ndk;${NDK_VERSION:-30.0.16248370}\")"; exit 1; }
jobs="${JOBS:-8}"
abi="${ABI:-arm64-v8a}"
case "$abi" in
arm64-v8a) target_triple=aarch64-linux-android ;;
x86_64) target_triple=x86_64-linux-android ;;
*) echo "unsupported Android ABI: $abi"; exit 2 ;;
esac
api=33

cmake="$(command -v cmake || true)"
[ -n "$cmake" ] || cmake="$(ls -d "$sdk"/cmake/*/bin 2>/dev/null | tail -1)/cmake$exe"
ninja="$(command -v ninja || true)"
[ -n "$ninja" ] || ninja="$(dirname "$cmake")/ninja$exe"
out="${OUT:-$here/build}/game"
cpu="${CPU:-generic}"
flags=()
if [ "$cpu" != generic ]; then flags=("-DCMAKE_C_FLAGS=-mcpu=$cpu" "-DCMAKE_CXX_FLAGS=-mcpu=$cpu"); fi

gen="${GEN_DIR:-$root/build/gen}"
if [ "$gen" = stub ] || ! compgen -G "$gen/code_*.c" > /dev/null; then
    gen="${OUT:-$here/build}/gen-stub"
    echo "== placeholder game code (recomp/stubgen.py) in $gen"
    python3 "$root/recomp/stubgen.py" "$gen"
fi

echo "== game (libmain.so), NDK $(basename "$ndk"), ABI $abi, CPU $cpu"
mkdir -p "$out"
"$cmake" -S "$root" -B "$out" -G Ninja "-DCMAKE_MAKE_PROGRAM=$ninja" \
    "-DCMAKE_TOOLCHAIN_FILE=$ndk/build/cmake/android.toolchain.cmake" "-DANDROID_ABI=$abi" \
    "-DANDROID_PLATFORM=android-$api" -DANDROID_STL=c++_shared -DCMAKE_BUILD_TYPE=Release \
    -DNSMBU_BUNDLED_DEPS=ON "-DGEN_DIR=$gen" ${flags[@]+"${flags[@]}"} > "$out.log"
"$cmake" --build "$out" -j "$jobs" --target nsmbu >> "$out.log" 2>&1 || { tail -40 "$out.log"; exit 1; }
libs="$here/app/libs/$abi"
mkdir -p "$libs"
bin="$ndk/toolchains/llvm/prebuilt/$host"
"$bin/bin/llvm-strip$exe" --strip-unneeded "$out/libmain.so" -o "$libs/libmain.so"
cp "$(find "$out/_deps" -name libSDL3.so -print -quit)" "$libs/"
cp "$bin/sysroot/usr/lib/$target_triple/libc++_shared.so" "$libs/"

if [ "$abi" = arm64-v8a ]; then
    for hook in main_hook hook_impl file_redirect_hook gsl_alloc_hook; do
        hook_file="$(find "$out/_deps" -name "lib${hook}.so" -print -quit)"
        [ -n "$hook_file" ] || { echo "missing AdrenoTools hook: $hook"; exit 1; }
        cp "$hook_file" "$libs/"
    done
fi
ls -la "$libs"

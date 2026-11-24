#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
name=${1:-survey}
if [ -z "${NSMBU_EXIT_AT_FRAME:-}" ]; then
    echo "Set NSMBU_EXIT_AT_FRAME" >&2
    exit 2
fi
user="$root/nsmbu"
if [ ! -d "$user" ] && [ -d "$root/user" ]; then
    user="$root/user"
fi
save_src="$user/save"
if [ ! -d "$save_src" ]; then
    echo "missing $root/nsmbu/save; run: nsmbu launch once, or copy a save there" >&2
    exit 2
fi
run="$root/.tmp/$name"
rm -rf "$run"
mkdir -p "$run/save" "$run/user"
cp -a "$save_src/." "$run/save/"
for file in settings.ini controls.json; do
    if [ -f "$user/$file" ]; then
        cp "$user/$file" "$run/user/$file"
    fi
done
cd "$run"
export NSMBU_USER_DIR="${NSMBU_USER_DIR:-$run/user}"
export NSMBU_HIDDEN_WINDOWS=1
export NSMBU_NO_HOST_INPUT=1
export NSMBU_VK_PRESENT_MODE=immediate
export NSMBU_NO_AUDIO=1
export NSMBU_RES_SCALE="${NSMBU_RES_SCALE:-1}"
nsmbu_bin=""
for base in "$root/build/debug" "$root/build/release" "$root/build/rel" "$root/build"; do
    if [ -f "$base/nsmbu" ]; then
        nsmbu_bin="$base/nsmbu"
        break
    fi
    if [ -f "$base/nsmbu.exe" ]; then
        nsmbu_bin="$base/nsmbu.exe"
        break
    fi
done
if [ -z "$nsmbu_bin" ]; then
    echo "missing $root/build/debug/nsmbu; run: nsmbu build" >&2
    exit 2
fi
exec "$nsmbu_bin" --game "$root/game" --save "$run/save"

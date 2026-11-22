#!/bin/sh
# RecompLauncher's setup step (windowless) on macOS and Linux: the same installer/setup.py
# the graphical installer and the terminal setups run. Needs python3, like those.
# --console-setup is for NSMBU.exe / nsmbu-launcher. setup.py ignores it; drop it here too.
cd "$(dirname "$0")/.." || exit 1
n=0
for arg do
    if [ "$arg" != "--console-setup" ]; then
        set -- "$@" "$arg"
        n=$((n + 1))
    fi
done
if [ "$n" -eq 0 ]; then
    set --
else
    shift $(($# - n))
fi
exec python3 installer/setup.py "$@"

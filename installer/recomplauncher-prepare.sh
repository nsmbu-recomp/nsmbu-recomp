#!/bin/sh
# RecompLauncher's setup step (windowless) on macOS and Linux: the same installer/setup.py
# the graphical installer and the terminal setups run. Needs python3, like those.
# The first argument is --console-setup, which only the Windows NSMBU.exe consumes; drop it.
if [ "$1" = "--console-setup" ]; then
    shift
fi
cd "$(dirname "$0")/.." || exit 1
exec python3 installer/setup.py "$@"

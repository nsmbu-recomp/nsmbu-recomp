# Common developer commands. Live button and screenshot control is --mcp on the debug executable, not a recipe here.

default:
    @just --list

# Debug build. Extra words go to tools/build/build.py.
build *args:
    python3 tools/build/build.py {{ args }}

# Release build.
release *args:
    python3 tools/build/build.py --release {{ args }}

# Debug build with AddressSanitizer and UBSan.
sanitizer *args:
    python3 tools/build/build.py --sanitizer {{ args }}

# Start the debug game. Flags: --no-debug --trace --gdb --lldb. A course is a trailing token such as 1-1.
launch *args:
    python3 tools/run/launch.py {{ args }}

# Play a .replay file. --uncapped sets NSMBU_UNCAPPED=1 (fast forward, no frame cap).
replay *args:
    python3 tools/run/replay.py {{ args }}

# Start the decomp-debug game. Same flags as launch. Example: just decomp -- --mcp
decomp *args:
    python3 tools/run/launch.py --decomp {{ args }}

# Debug build, then launch.
run *args:
    python3 tools/build/build.py
    python3 tools/run/launch.py {{ args }}

# Release build, then launch without the debug environment.
run-release *args:
    python3 tools/build/build.py --release
    python3 tools/run/launch.py --no-debug {{ args }}

# Debug game, muted, with MCP. Example: just mcp --open 1-1
mcp *args:
    python3 tools/run/launch.py -- --mcp {{ args }}

# Launch under gdb.
gdb *args:
    python3 tools/run/launch.py --gdb {{ args }}

# Launch under lldb.
lldb *args:
    python3 tools/run/launch.py --lldb {{ args }}

# Build the Windows executable and start it with Wine.
wine *args:
    python3 tools/build/build_windows.py {{ args }}
    python3 tools/run/launch.py --wine {{ args }}

# Debug launch that crashes on purpose (NSMBU_TEST_HOST_CRASH).
crash:
    NSMBU_TEST_HOST_CRASH=1 python3 tools/run/launch.py

# Dump folders. Examples: just game --list, just game usa-1.3.0, just game --activate usa-1.3.0
game *args:
    python3 tools/disc/game_select.py {{ args }}

# Extract the active dump.
extract *args:
    python3 tools/disc/extract.py {{ args }}

# Recompile the active dump into build/gen.
recomp:
    python3 tools/disc/game_select.py --recomp

# Hidden scripted boot. Set NSMBU_EXIT_AT_FRAME. Example: just scripted survey
scripted name *args:
    bash tools/run/scripted-run.sh {{ name }} {{ args }}

# Fixed-scene profile.
profile *args:
    python3 tools/run/profile.py {{ args }}

# Sample a running process.
perf *args:
    python3 tools/run/perf.py {{ args }}

# CTest for the debug build.
test *args:
    ctest --test-dir build/debug --output-on-failure {{ args }}

# Linux installer package.
installer *args:
    python3 tools/release/host_artifacts.py {{ args }}

# Android setup APK.
apk *args:
    python3 tools/android/apk.py {{ args }}

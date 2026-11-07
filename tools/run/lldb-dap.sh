#!/bin/sh
# lldb-dap launcher for VS Code / Cursor (lldb-dap.executable-path / debugAdapterExecutable).
set -e
for name in lldb-dap lldb-dap-21 lldb-dap-20 lldb-dap-19 lldb-dap-18 lldb-dap-17; do
    path=$(command -v "$name" 2>/dev/null) || true
    if [ -n "$path" ] && [ -x "$path" ]; then
        exec "$path" "$@"
    fi
done
for llvm in 21 20 19 18 17; do
    path="/usr/lib/llvm-${llvm}/bin/lldb-dap"
    if [ -x "$path" ]; then
        exec "$path" "$@"
    fi
done
echo "lldb-dap not found; install lldb (e.g. lldb-18) or set lldb-dap.executable-path" >&2
exit 1

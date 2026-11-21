#!/usr/bin/env python3
"""Write weak stubs for guest symbols the runtime still names.

usage: guest_stubs.py OUT.c

The NSMBU recompiler does not emit the old addresses.
The runtime hook code still refers to them.
A weak stub satisfies the link.
A real function in the generated game code replaces the stub.
Addresses in recomp/hooks/*.jsonc with no hook_ function in the runtime get a weak hook too.
"""
import glob
import os
import re
import sys

here = os.path.dirname(os.path.abspath(__file__))
root = os.path.normpath(os.path.join(here, ".."))
pat = re.compile(r"\bf_([0-9A-F]{8})(_orig)?\b")
hook_def = re.compile(r"\bhook_([0-9A-F]{8})\s*\(")
site_def = re.compile(r"\bsite_([0-9A-F]{8})\s*\(")

def runtime_texts():
    for ext in ("c", "cpp", "h", "mm"):
        for path in glob.glob(os.path.join(glob.escape(root), "runtime", "**", "*." + ext), recursive=True):
            if path.endswith("nsmbu_guest_stubs.c"):
                continue
            with open(path, errors="replace") as f:
                yield f.read()

def symbols():
    found = set()
    for text in runtime_texts():
        for addr, orig in pat.findall(text):
            found.add("f_%s%s" % (addr, orig))
    return sorted(found)

def hook_lists():
    from builds import hook_files, load_hook_file
    hooks, sites = set(), set()
    for path in hook_files():
        _label, entries = load_hook_file(path)
        for site, canon, _same, _where in entries:
            (sites if site else hooks).add(canon)
    return hooks, sites

def defined(pattern):
    found = set()
    for text in runtime_texts():
        found.update(int(addr, 16) for addr in pattern.findall(text))
    return found

def main(out):
    names = symbols()
    hooks, sites = hook_lists()
    missing_hooks = sorted(hooks - defined(hook_def))
    missing_sites = sorted(sites - defined(site_def))
    lines = ['#include "ppc.h"', ""]
    for name in names:
        addr = name[2:10]
        lines.append("__attribute__((weak)) void %s(Cpu* __restrict c) { ppc_unimplemented(c, 0x%su, 0); }" % (name, addr))
    for addr in missing_hooks:
        lines.append("__attribute__((weak)) void hook_%08X(Cpu* c) { ppc_unimplemented(c, 0x%08Xu, 0); }" % (addr, addr))
    for addr in missing_sites:
        lines.append("__attribute__((weak)) void site_%08X(Cpu* c) { ppc_unimplemented(c, 0x%08Xu, 0); }" % (addr, addr))
    lines.append("")
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    with open(out, "w") as f:
        f.write("\n".join(lines))
    print("%d weak guest stubs, %d hooks, %d sites -> %s" % (len(names), len(missing_hooks), len(missing_sites), out))

if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit("usage: guest_stubs.py OUT.c")
    main(sys.argv[1])

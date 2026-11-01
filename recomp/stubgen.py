#!/usr/bin/env python3
"""Write a placeholder build/gen for build checks without the game.

usage: stubgen.py OUTDIR

recomp.py needs your own red-pro2.rpx. This script instead emits the same file layout (funcs.h,
code_000.c, table.c, imports.c) with every guest function the runtime refers to (hooks and direct
calls) defined as a stub that halts via ppc_unimplemented. The result compiles and links the full
runtime and renderer on any host, so CI and porting work can check the build; the executable
cannot run the game (startup refuses it: the entry point matches no RPX).
"""
import glob
import os
import re
import sys

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, here)
import names
root = os.path.normpath(os.path.join(here, ".."))

def hook_lists():
    from builds import _parse_hook, hook_files
    hooks, sites = set(), set()
    for path in hook_files():
        for lineno, raw in enumerate(open(path), 1):
            line = raw.split("#")[0].strip()
            if not line:
                continue
            site, canon, _same = _parse_hook(line, "%s:%d" % (os.path.basename(path), lineno))
            (sites if site else hooks).add(canon)
    return hooks, sites

def runtime_refs():
    funcs = set()
    pat = re.compile(r"\bf_([0-9A-F]{8})(?:_orig)?\b")
    for ext in ("c", "cpp", "h", "mm"):
        for p in glob.glob(os.path.join(glob.escape(root), "runtime", "src", "**", "*." + ext), recursive=True):
            with open(p, errors="replace") as f:
                funcs.update(int(m, 16) for m in pat.findall(f.read()))
    return funcs

def main(outdir):
    os.makedirs(outdir, exist_ok=True)
    hooks, sites = hook_lists()
    funcs = sorted(runtime_refs() | hooks)
    labels = names.load()

    def comment(e):
        label = labels.get(e)
        return " /* %s */" % label if label else ""

    with open(os.path.join(outdir, "funcs.h"), "w") as f:
        f.write('#pragma once\n#include "ppc.h"\n\n')
        for e in funcs:
            f.write("void f_%08X(Cpu* __restrict c);%s\n" % (e, comment(e)))
        for e in sorted(hooks):
            f.write("void f_%08X_orig(Cpu* __restrict c);%s\nvoid hook_%08X(Cpu* c);\n" % (e, comment(e), e))
        for e in sorted(sites):
            f.write("void site_%08X(Cpu* c);\n" % e)
    with open(os.path.join(outdir, "code_000.c"), "w") as f:
        f.write('#include "funcs.h"\n\n/* placeholder: written by recomp/stubgen.py, not game code */\n')
        for e in funcs:
            if e in hooks:
                f.write("void f_%08X(Cpu* __restrict c) { hook_%08X(c); }%s\n" % (e, e, comment(e)))
                f.write("void f_%08X_orig(Cpu* __restrict c) { ppc_unimplemented(c, 0x%08Xu, 0); }%s\n" % (e, e, comment(e)))
            else:
                f.write("void f_%08X(Cpu* __restrict c) { ppc_unimplemented(c, 0x%08Xu, 0); }%s\n" % (e, e, comment(e)))

        f.write("\nvoid stubgen_sites(Cpu* c) {\n")
        for e in sorted(sites):
            f.write("    site_%08X(c);\n" % e)
        f.write("}\n")
    with open(os.path.join(outdir, "table.c"), "w") as f:
        f.write('#include "funcs.h"\n#include "recomp_table.h"\n\n')
        f.write("const RecompEntry g_recomp_funcs[] = {\n")
        for e in funcs:
            f.write("    {0x%08Xu, f_%08X},\n" % (e, e))
        f.write("};\nconst unsigned g_recomp_func_count = %d;\n\n" % len(funcs))
        f.write("const RecompImport g_recomp_imports[] = {{0, 0, \"\", \"\", 0, 0}};\n")
        f.write("const unsigned g_recomp_import_count = 0;\n")
        f.write("const uint32_t g_recomp_entry_point = 0u; /* placeholder: matches no RPX */\n")

    with open(os.path.join(outdir, "imports.c"), "w") as f:
        f.write('#include "funcs.h"\n')
    with open(os.path.join(outdir, "report.txt"), "w") as f:
        f.write("placeholder from stubgen.py: %d stub functions, %d hooks, %d sites\n" % (len(funcs), len(hooks), len(sites)))
    print("wrote %s: %d stub functions, %d hooks, %d sites" % (outdir, len(funcs), len(hooks), len(sites)))

if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    main(sys.argv[1])

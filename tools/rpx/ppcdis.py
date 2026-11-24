#!/usr/bin/env python3
"""Disassemble red-pro2.rpx .text: ppcdis.py ADDR [COUNT] [RPX]
Import calls are annotated with their library and name."""
import sys, os
sys.path.insert(0, os.path.dirname(__file__))
from rpx import Rpx, R_PPC_REL24
import capstone

def default_rpx():
    root = os.path.join(os.path.dirname(__file__), "..", "..")
    active = os.path.join(root, "game", "active")
    if os.path.exists(active):
        name = open(active).read().strip()
        return os.path.join(root, "game", name, "code", "red-pro2.rpx")
    return os.path.join(root, "game", "usa-1.3.0", "code", "red-pro2.rpx")

def load_names():
    root = os.path.join(os.path.dirname(__file__), "..", "..")
    path = os.path.join(root, "recomp", "headers_names.txt")
    names = {}
    if not os.path.exists(path):
        return names
    for line in open(path):
        if line.startswith("#"):
            continue
        parts = line.split(None, 1)
        if len(parts) == 2:
            names[int(parts[0], 16)] = parts[1].strip()
    return names

_rpx = None
imports = None
names = None

def load(path=None):
    global _rpx, imports, names
    _rpx = _rpx or Rpx(path or default_rpx())
    if imports is None:
        imports = {}
        for sec, addr, typ, sym, add in _rpx.relocs:
            if sym.import_lib and typ == R_PPC_REL24:
                imports[addr] = "%s!%s" % (sym.import_lib, sym.name)
    if names is None:
        names = load_names()
    return _rpx

def dis(addr, count=32, path=None):
    r = load(path)
    t = r.by_name[".text"]
    md = capstone.Cs(capstone.CS_ARCH_PPC, capstone.CS_MODE_32 | capstone.CS_MODE_BIG_ENDIAN | capstone.CS_MODE_PS)
    o = addr - t.addr
    out = []
    for k in range(count):
        a = addr + 4*k
        chunk = t.data[o + 4*k:o + 4*k + 4]
        ins = next(md.disasm(chunk, a), None)
        line = "%08x: %s  %s" % (a, chunk.hex(), "%s %s" % (ins.mnemonic, ins.op_str) if ins else "??")
        if a in imports:
            line += "  ; %s" % imports[a]
        elif ins and ins.mnemonic in ("bl", "b") and ins.op_str.startswith("0x"):
            target = int(ins.op_str, 16)
            if target in names:
                line += "  ; %s" % names[target]
        out.append(line)
    return "\n".join(out)

if __name__ == "__main__":
    print(dis(int(sys.argv[1], 16), int(sys.argv[2]) if len(sys.argv) > 2 else 32,
              sys.argv[3] if len(sys.argv) > 3 else None))

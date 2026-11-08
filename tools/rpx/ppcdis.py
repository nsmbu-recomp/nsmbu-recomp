#!/usr/bin/env python3
"""Queries on red-pro2.rpx .text.

    ppcdis.py ADDR [COUNT] [RPX]
    ppcdis.py callers ADDR [RPX]
    ppcdis.py func ADDR [RPX]
    ppcdis.py access OFFSET [START END] [RPX]
    ppcdis.py xref TEXT [RPX]

The first form disassembles COUNT instructions and annotates import calls.
callers prints each direct b or bl to ADDR.
func prints the discovered function that contains ADDR.
access prints each integer load or store of OFFSET.
xref prints each string that contains TEXT, then each site that points at it.
"""
import bisect
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(__file__))
from rpx import Rpx, R_PPC_REL24
import capstone

_LOAD_STORE = {
    32: "lwz", 33: "lwzu", 34: "lbz", 35: "lbzu",
    36: "stw", 37: "stwu", 38: "stb", 39: "stbu",
    40: "lhz", 41: "lhzu", 42: "lha", 43: "lhau",
    44: "sth", 45: "sthu", 46: "lmw", 47: "stmw",
}
_LIS_WINDOW = 8

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

def text_words(path=None):
    r = load(path)
    t = r.by_name[".text"]
    count = len(t.data) // 4
    words = struct.unpack(">%dI" % count, t.data[:count * 4])
    return t.addr, words

def branch_target(addr, word):
    """Destination of a relative b or bl. An absolute branch returns None."""
    if (word >> 26) != 18 or (word & 2):
        return None
    off = word & 0x03FFFFFC
    if off & 0x02000000:
        off -= 0x04000000
    return (addr + off) & 0xFFFFFFFF

def callers_of(words, base, target):
    """Each (site, 'b'|'bl') whose direct branch lands on target."""
    hits = []
    for i, word in enumerate(words):
        dest = branch_target(base + 4 * i, word)
        if dest == target:
            hits.append((base + 4 * i, "bl" if word & 1 else "b"))
    return hits

def format_callers(words, base, target):
    return "\n".join("%08x %s" % hit for hit in callers_of(words, base, target))

def function_span(entries, addr, text_hi):
    """(start, end) of the discovered function that contains addr, or None."""
    if not entries or addr < entries[0] or addr >= text_hi:
        return None
    i = bisect.bisect_right(entries, addr) - 1
    end = entries[i + 1] if i + 1 < len(entries) else text_hi
    if addr >= end:
        return None
    return entries[i], end

def format_func(entries, addr, text_hi):
    span = function_span(entries, addr, text_hi)
    if span is None:
        return "outside"
    start, end = span
    size = end - start
    return "start %08x end %08x size %d insns %d" % (start, end, size, size // 4)

def accesses(words, base, disp, start=None, end=None):
    """Each (addr, mnemonic, rD, rA) load or store whose low 16 bits equal disp."""
    disp &= 0xFFFF
    hits = []
    for i, word in enumerate(words):
        addr = base + 4 * i
        if start is not None and addr < start:
            continue
        if end is not None and addr >= end:
            continue
        op = word >> 26
        if op not in _LOAD_STORE or (word & 0xFFFF) != disp:
            continue
        hits.append((addr, _LOAD_STORE[op], (word >> 21) & 31, (word >> 16) & 31))
    return hits

def format_accesses(words, base, disp, start=None, end=None):
    lines = []
    for addr, mnemonic, rd, ra in accesses(words, base, disp, start, end):
        lines.append("%08x %s r%d, 0x%x(r%d)" % (addr, mnemonic, rd, disp & 0xFFFF, ra))
    return "\n".join(lines)

def find_strings(blob, base, needle):
    """Map each cstring address to its text when the text contains needle."""
    raw = needle.encode("utf-8")
    if not raw:
        return {}
    found = {}
    start = 0
    while True:
        i = blob.find(raw, start)
        if i < 0:
            return found
        begin = blob.rfind(b"\0", 0, i) + 1
        end = blob.find(b"\0", i)
        if end < 0:
            end = len(blob)
        text = blob[begin:end]
        if 0 < len(text) <= 200 and all(32 <= c < 127 for c in text):
            found[base + begin] = text.decode("ascii")
        start = i + max(len(raw), 1)

def strings_in(r, needle):
    found = {}
    for name in (".rodata", ".data", ".sdata"):
        section = r.by_name.get(name)
        if section is not None and section.data:
            found.update(find_strings(section.data, section.addr, needle))
    return found

def lis_sites(words, base, targets):
    """Instruction addresses where lis plus addi or ori forms an address in targets."""
    pending = {}
    hits = []
    for i, word in enumerate(words):
        op = word >> 26
        rd = (word >> 21) & 31
        ra = (word >> 16) & 31
        if op == 15 and ra == 0:
            pending[rd] = (i, word & 0xFFFF)
        elif op == 14 and ra in pending and i - pending[ra][0] <= _LIS_WINDOW:
            high = pending[ra][1]
            simm = word & 0xFFFF
            if simm & 0x8000:
                simm -= 0x10000
            ea = ((high << 16) + simm) & 0xFFFFFFFF
            if ea in targets:
                hits.append(base + 4 * pending[ra][0])
        elif op == 24 and rd in pending and i - pending[rd][0] <= _LIS_WINDOW:
            ea = ((pending[rd][1] << 16) | (word & 0xFFFF)) & 0xFFFFFFFF
            if ea in targets:
                hits.append(base + 4 * pending[rd][0])
        dest = ra if op == 24 else rd
        if op in (14, 15, 24) and dest in pending and pending[dest][0] != i:
            pending.pop(dest, None)
    return hits

def reloc_sites(relocs, targets):
    """Relocation sites whose symbol plus addend is an address in targets."""
    hits = []
    for site, _typ, value, addend in relocs:
        if ((value + addend) & 0xFFFFFFFF) in targets:
            hits.append(site)
    return hits

def format_xref(strings, code_sites, reloc_hits):
    if not strings:
        return "none"
    lines = []
    for addr in sorted(strings):
        lines.append("string %08x %s" % (addr, strings[addr]))
    for site in sorted(set(code_sites)):
        lines.append("site %08x" % site)
    for site in sorted(set(reloc_hits)):
        lines.append("reloc %08x" % site)
    return "\n".join(lines)

def _parse_hex(text):
    return int(text, 16)

def _pop_rpx(args):
    if args and (args[-1].endswith(".rpx") or os.path.exists(args[-1])):
        return args[:-1], args[-1]
    return args, None

def func_line(addr, path=None):
    root = os.path.join(os.path.dirname(__file__), "..", "..")
    sys.path.insert(0, os.path.join(root, "recomp"))
    import analyze
    program = analyze.Program(path or default_rpx())
    return format_func(program.discover(), addr, program.text_hi)

def xref_line(needle, path=None):
    r = load(path)
    found = strings_in(r, needle)
    base, words = text_words(path)
    relocs = [(site, typ, sym.value, add) for _sec, site, typ, sym, add in r.relocs]
    return format_xref(found, lis_sites(words, base, set(found)), reloc_sites(relocs, set(found)))

def main(argv):
    if len(argv) < 2:
        print(__doc__.strip())
        return 2
    cmd = argv[1]
    if cmd == "callers":
        args, path = _pop_rpx(argv[2:])
        base, words = text_words(path)
        print(format_callers(words, base, _parse_hex(args[0])))
    elif cmd == "func":
        args, path = _pop_rpx(argv[2:])
        print(func_line(_parse_hex(args[0]), path))
    elif cmd == "access":
        args, path = _pop_rpx(argv[2:])
        start = _parse_hex(args[1]) if len(args) > 1 else None
        end = _parse_hex(args[2]) if len(args) > 2 else None
        base, words = text_words(path)
        print(format_accesses(words, base, _parse_hex(args[0]), start, end))
    elif cmd == "xref":
        args, path = _pop_rpx(argv[2:])
        print(xref_line(args[0], path))
    else:
        count = int(argv[2]) if len(argv) > 2 and not argv[2].endswith(".rpx") else 32
        path = argv[3] if len(argv) > 3 else (argv[2] if len(argv) > 2 and argv[2].endswith(".rpx") else None)
        print(dis(_parse_hex(argv[1]), count, path))
    return 0

if __name__ == "__main__":
    sys.exit(main(sys.argv))

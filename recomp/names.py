#!/usr/bin/env python3
"""Class and function names for guest addresses.

The names come from the [nsmbu/headers](https://github.com/nsmbu/headers) project (USA v1.3.0):
`python3 recomp/names.py` walks a `references/headers` checkout and writes the extracted
`{address: name}` table to `recomp/headers_names.txt`, which is in git. Local `recomp.py` and
`stubgen.py` read that file when present; if it is missing (release packages omit it), they use an
empty table.

usage:
    names.py path/to/headers...     extract into headers_names.txt
    names.py                        show the committed count

The headers tie a name to an address in three shapes:

    // Address: 0xADDR          before a declaration: members and methods
    // symbol   Address: 0xADDR  on one line: statics and the disposer block
    * @par Address: 0xADDR      in a doxygen block: virtuals

`Address: Deleted` and `Address: //!!!!!!!!` are not addresses. The class comes from the
enclosing `class`; a nested `class` adds to the scope. A symbol read out of a declaration is
the identifier before the first `(`, so `sead::TTaskFactory<TitleScene>` keeps its own
namespace and is not prefixed with the class.
"""
import os
import re
import subprocess
import sys

here = os.path.dirname(os.path.abspath(__file__))
headers_dir = os.path.normpath(os.path.join(here, "..", "references", "headers"))
DATA_FILE = os.path.join(here, "headers_names.txt")

ADDRESS = re.compile(r"Address:\s*(0x[0-9A-Fa-f]{8})\b")
CLASS = re.compile(r"^\s*(?:class|struct)\s+(\w+)")
FIELD = re.compile(r"([A-Za-z_][A-Za-z0-9_]*)\s*(?:=[^;]*)?;\s*$")
SKIP_DECL_PREFIX = ("#", "SEAD_", "enum", "typedef", "static_assert", "using")


def strip_code(line):
    return re.sub(r'"(?:[^"\\]|\\.)*"', '""', line).split("//")[0]


def symbol_of(text):
    """The symbol a declaration or comment fragment names: the identifier before `(`."""
    paren = text.find("(")
    if paren >= 0:
        head = text[:paren].split()
        return head[-1].strip() if head else None
    m = FIELD.search(text.strip())
    return m.group(1) if m else None


def qualify(scope, symbol, from_decl):
    if not symbol:
        return None
    if from_decl and ("::" in symbol or "<" in symbol):
        return symbol
    return "::".join(scope + [symbol]) if scope else symbol


class Parser:
    def __init__(self):
        self.names = {}
        self.conflicts = 0

    def put(self, addr, name):
        if not name:
            return
        old = self.names.setdefault(addr, name)
        if old != name:
            self.conflicts += 1

    def parse(self, text):
        lines = text.splitlines()
        depth = 0
        pending = None
        stack = []
        for i, line in enumerate(lines):
            m = ADDRESS.search(line)
            if m:
                self.add(int(m.group(1), 16), line, stack, pending, lines, i)
            code = strip_code(line)
            opens, closes = code.count("{"), code.count("}")
            new_depth = depth + opens - closes
            if pending is not None:
                if opens:
                    stack.append((pending, new_depth))
                    pending = None
                elif ";" in code or closes:
                    pending = None
            depth = new_depth
            while stack and depth < stack[-1][1]:
                stack.pop()
            if pending is None:
                c = CLASS.match(code)
                if c and not code.rstrip().endswith(";"):
                    pending = c.group(1)

    def scope(self, stack, pending):
        if pending and not stack:
            return [pending]
        return [name for name, _ in stack]

    def add(self, addr, line, stack, pending, lines, i):
        before = line.split("Address:")[0]
        text = before.replace("//", " ").replace("*", " ").replace("@par", " ").strip()
        if text:
            self.put(addr, qualify(self.scope(stack, pending), text.split()[0], from_decl=False))
            return
        for k in range(i + 1, min(i + 16, len(lines))):
            decl = strip_code(lines[k]).strip()
            if not decl or decl == "*/" or decl.endswith(":"):
                continue
            if decl.startswith(SKIP_DECL_PREFIX) and "(" not in decl:
                continue
            if decl.startswith("extern"):
                continue
            symbol = symbol_of(decl)
            if symbol:
                self.put(addr, qualify(self.scope(stack, pending), symbol, from_decl=True))
            return

    def load_tree(self, root_dir):
        for root, dirs, files in os.walk(root_dir):
            dirs[:] = [d for d in sorted(dirs) if d != ".git"]
            for fn in sorted(files):
                if fn.endswith(".h"):
                    with open(os.path.join(root, fn), errors="replace") as f:
                        self.parse(f.read())


def source_commit(root_dir):
    try:
        out = subprocess.run(["git", "-C", root_dir, "rev-parse", "--short", "HEAD"],
                             capture_output=True, text=True, timeout=10)
        return out.stdout.strip() if out.returncode == 0 else None
    except OSError:
        return None


def extract(argv_paths):
    """Walk one or more references/headers checkouts and write headers_names.txt."""
    if not argv_paths:
        argv_paths = [headers_dir]
    parser = Parser()
    for d in argv_paths:
        if not os.path.isdir(d):
            sys.exit("no headers checkout: %s" % d)
        parser.load_tree(d)
    lines = ["# Function and global addresses with their class and symbol names.",
             "# Source: https://github.com/nsmbu/headers (USA v1.3.0), extracted by recomp/names.py."]
    commit = source_commit(argv_paths[0])
    if commit:
        lines[1] += " Checkout: %s." % commit
    lines.append("#")
    for addr, name in sorted(parser.names.items()):
        lines.append("%08X %s" % (addr, name))
    with open(DATA_FILE, "w") as f:
        f.write("\n".join(lines) + "\n")
    if parser.conflicts:
        print("names.py: %d addresses with two names" % parser.conflicts, file=sys.stderr)
    print("wrote %s: %d names from %s" % (DATA_FILE, len(parser.names), " ".join(argv_paths)))


def load():
    """{address int: name} from headers_names.txt when present, else {}."""
    names = {}
    try:
        with open(DATA_FILE) as f:
            for line in f:
                if line.startswith("#"):
                    continue
                addr, _, name = line.rstrip("\n").partition(" ")
                if addr and name:
                    names[int(addr, 16)] = name
    except OSError:
        pass
    return names


if __name__ == "__main__":
    if any(a.startswith("-") for a in sys.argv[1:]):
        print(__doc__.strip())
        sys.exit(0)
    if sys.argv[1:]:
        extract(sys.argv[1:])
    else:
        print("%d committed names" % len(load()))

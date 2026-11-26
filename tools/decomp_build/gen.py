#!/usr/bin/env python3
"""Write the decomp build function list and the hook wrappers.

usage: gen.py --out DIR [--root vendor/decomp] [--hooks recomp]

A candidate is a function body whose previous line is `// Address: 0xXXXXXXXX`.
A declaration with no body stays recompiled.
An address already in recomp/hooks.txt or recomp/hooks_*.txt stays on the port hook.
The list is one address per line in DIR/decomp_hooks.txt.
"""
import argparse
import glob
import os
import re

ADDR_RE = re.compile(r"^\s*// Address:\s*0x([0-9A-Fa-f]{8})\b")
HOOK_RE = re.compile(r"^(?:@)?([0-9A-Fa-f]+)\b")
FLOAT_TYPES = {"float", "double", "f32", "f64"}
INT64_TYPES = {"u64", "s64", "uint64_t", "int64_t", "unsigned long long", "long long"}
LEAD_WORDS = ("static", "virtual", "inline", "explicit", "constexpr", "friend", "extern")


def repo_root():
    return os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))


def hook_addresses(hooks_dir):
    found = set()
    paths = [os.path.join(hooks_dir, "hooks.txt")]
    paths += sorted(glob.glob(os.path.join(glob.escape(hooks_dir), "hooks_*.txt")))
    for path in paths:
        if not os.path.isfile(path):
            continue
        for raw in open(path, encoding="utf-8", errors="replace"):
            text = raw.split("#")[0].strip()
            if not text or text.startswith("@") or text.startswith("#"):
                continue
            parts = text.split()
            try:
                found.add(int(parts[0], 16))
            except (ValueError, IndexError):
                continue
    return found


def strip_comments(text):
    out = []
    i = 0
    n = len(text)
    while i < n:
        if text.startswith("//", i):
            i = text.find("\n", i)
            if i < 0:
                break
            continue
        if text.startswith("/*", i):
            j = text.find("*/", i + 2)
            i = n if j < 0 else j + 2
            continue
        if text[i] in "\"'":
            q = text[i]
            out.append(text[i])
            i += 1
            while i < n:
                out.append(text[i])
                if text[i] == "\\":
                    if i + 1 < n:
                        out.append(text[i + 1])
                    i += 2
                    continue
                if text[i] == q:
                    i += 1
                    break
                i += 1
            continue
        out.append(text[i])
        i += 1
    return "".join(out)


def line_classes(text):
    """Class qualifier in force at the start of each line, from enclosing class scopes."""
    lines = text.splitlines(keepends=True)
    scopes = []
    stack = []
    depth = 0
    i = 0
    pending = None
    raw = strip_comments(text)
    # Walk the stripped text in parallel with newlines of the original.
    # The comment strip keeps newlines, so indexes match when comments are replaced by nothing
    # only if we preserve length. Re-scan the original with a small state machine instead.
    scopes = [""] * len(lines)
    depth = 0
    stack = []
    pending = None
    line = 0
    i = 0
    n = len(text)
    mode = "code"

    def current():
        names = [name for kind, name, _d in stack if kind == "class" and name]
        return "::".join(names)

    while i < n:
        if text[i] == "\n":
            line += 1
            if line < len(scopes):
                scopes[line] = current()
            i += 1
            continue
        if mode == "line":
            if text[i] == "\n":
                mode = "code"
            else:
                i += 1
            continue
        if mode == "block":
            if text.startswith("*/", i):
                mode = "code"
                i += 2
            else:
                i += 1
            continue
        if text.startswith("//", i):
            mode = "line"
            i += 2
            continue
        if text.startswith("/*", i):
            mode = "block"
            i += 2
            continue
        if text[i] in "\"'":
            q = text[i]
            i += 1
            while i < n and text[i] != q:
                if text[i] == "\\":
                    i += 2
                    continue
                if text[i] == "\n":
                    line += 1
                    if line < len(scopes):
                        scopes[line] = current()
                i += 1
            i += 1
            continue
        if text[i] == "{":
            if pending:
                stack.append((pending[0], pending[1], depth))
                pending = None
            depth += 1
            i += 1
            continue
        if text[i] == "}":
            depth = max(0, depth - 1)
            if stack and stack[-1][2] == depth:
                stack.pop()
            i += 1
            continue
        if pending is None and depth == (stack[-1][2] + 1 if stack else 0):
            m = re.match(r"(class|struct|namespace)\s+([A-Za-z_]\w*)", text[i:])
            if m and (i == 0 or not (text[i - 1].isalnum() or text[i - 1] == "_")):
                kind = "class" if m.group(1) != "namespace" else "ns"
                pending = (kind, m.group(2) if kind == "class" else "")
                i += m.end()
                continue
        i += 1
    if scopes:
        scopes[0] = ""
    return scopes


def _gather(lines, start):
    """Return (kind, signature, next_index). kind is def, decl, or skip."""
    buf = []
    j = start + 1
    paren = brace = angle = bracket = 0
    started = False
    while j < len(lines):
        raw = lines[j]
        if not started and not raw.strip():
            j += 1
            continue
        if not started and raw.strip().startswith("//"):
            j += 1
            continue
        if raw.strip().startswith("// Address:"):
            break
        started = True
        buf.append(raw)
        text = strip_comments("\n".join(buf))
        paren = brace = angle = bracket = 0
        body = False
        semi = False
        for ch in text:
            if ch == "(":
                paren += 1
            elif ch == ")":
                paren = max(0, paren - 1)
            elif ch == "{":
                if paren == 0 and angle == 0 and bracket == 0 and brace == 0:
                    body = True
                    break
                brace += 1
            elif ch == "}":
                brace = max(0, brace - 1)
            elif ch == "<":
                angle += 1
            elif ch == ">":
                angle = max(0, angle - 1)
            elif ch == "[":
                bracket += 1
            elif ch == "]":
                bracket = max(0, bracket - 1)
            elif ch == ";" and paren == 0 and brace == 0 and angle == 0 and bracket == 0:
                semi = True
                break
        if body:
            sig = text.split("{", 1)[0].strip()
            return "def", _cut_initializer(sig), j + 1
        if semi:
            return "decl", text.split(";", 1)[0].strip(), j + 1
        j += 1
    joined = " ".join(x.strip() for x in buf).strip()
    if not joined:
        return "skip", "", j
    if "(" not in joined:
        return "skip", joined, j
    return "skip", joined, j


def _cut_initializer(sig):
    paren = 0
    seen = False
    for i, ch in enumerate(sig):
        if ch == "(":
            paren += 1
            seen = True
        elif ch == ")":
            paren = max(0, paren - 1)
        elif ch == ":" and seen and paren == 0:
            return sig[:i].strip()
    return sig.strip()


def _split_params(param_text):
    param_text = param_text.strip()
    if not param_text or param_text == "void":
        return []
    parts = []
    buf = []
    paren = angle = bracket = 0
    for ch in param_text:
        if ch == "," and paren == 0 and angle == 0 and bracket == 0:
            parts.append("".join(buf).strip())
            buf = []
            continue
        buf.append(ch)
        if ch == "(":
            paren += 1
        elif ch == ")":
            paren = max(0, paren - 1)
        elif ch == "<":
            angle += 1
        elif ch == ">":
            angle = max(0, angle - 1)
        elif ch == "[":
            bracket += 1
        elif ch == "]":
            bracket = max(0, bracket - 1)
    tail = "".join(buf).strip()
    if tail:
        parts.append(tail)
    out = []
    for part in parts:
        out.append(_drop_default(part))
    return out


def _drop_default(part):
    paren = angle = bracket = 0
    for i, ch in enumerate(part):
        if ch == "=" and paren == 0 and angle == 0 and bracket == 0:
            return part[:i].strip()
        if ch == "(":
            paren += 1
        elif ch == ")":
            paren = max(0, paren - 1)
        elif ch == "<":
            angle += 1
        elif ch == ">":
            angle = max(0, angle - 1)
        elif ch == "[":
            bracket += 1
        elif ch == "]":
            bracket = max(0, bracket - 1)
    return part.strip()


def _split_name(param):
    param = param.strip()
    if not param:
        return param, ""
    if param.endswith("]") or param.endswith("&") or param.endswith("*") or param.endswith(")"):
        return param, ""
    bits = param.rsplit(None, 1)
    if len(bits) == 2 and re.match(r"^[A-Za-z_]\w*$", bits[1]) and not bits[0].endswith(":"):
        return bits[0].strip(), bits[1]
    return param, ""


def _parse_head(sig):
    sig = " ".join(sig.split())
    static = False
    op_params = False
    if "operator()" in sig:
        head, rest = sig.split("operator()", 1)
        name = "operator()"
        if not rest.strip().startswith("("):
            raise ValueError("operator() has no parameter list")
        params_s, trailing = _take_params(rest.strip())
        qual = head.strip()
        for word in LEAD_WORDS:
            if qual.startswith(word + " "):
                if word == "static":
                    static = True
                qual = qual[len(word) + 1:].strip()
        scope = qual[:-2].strip() if qual.endswith("::") else ""
        ret = ""
        # qual before operator() is "bool Class::" or "Class::"
        if scope.endswith("::"):
            scope = scope[:-2]
        # ret may still be in scope: "bool ActorProfileFindFunc"
        if " " in scope:
            ret, scope = scope.rsplit(None, 1)
        elif scope and not _looks_like_scope(scope) and ret == "":
            # "bool" only, free operator. Not expected.
            ret, scope = scope, ""
        return static, ret, scope, name, params_s
    head, params_s = _split_sig(sig)
    words = head.split()
    while words and words[0] in LEAD_WORDS:
        if words[0] == "static":
            static = True
        words = words[1:]
    head = " ".join(words).strip()
    m = re.search(r"([~A-Za-z_][\w:]*)$", head)
    if not m:
        raise ValueError("no function name in %r" % sig)
    name = m.group(1)
    ret = head[:m.start()].strip()
    if "::" in name:
        scope, name = name.rsplit("::", 1)
    else:
        scope = ""
    return static, ret, scope, name, params_s


def _looks_like_scope(text):
    return "::" in text or (text[:1].isupper() and " " not in text)


def _take_params(text):
    if not text.startswith("("):
        raise ValueError("missing parameter list")
    depth = 0
    for i, ch in enumerate(text):
        if ch == "(":
            depth += 1
        elif ch == ")":
            depth -= 1
            if depth == 0:
                return text[1:i], text[i + 1:]
    raise ValueError("unbalanced parameters")


def _split_sig(sig):
    # The parameter list is the last top-level parenthesized group.
    depth = 0
    start = None
    for i, ch in enumerate(sig):
        if ch == "(":
            if depth == 0:
                start = i
            depth += 1
        elif ch == ")":
            depth -= 1
            if depth == 0 and start is not None:
                end = i
    if start is None:
        raise ValueError("missing parameter list")
    return sig[:start].strip(), sig[start + 1:end].strip()


def _norm_type(type_name):
    return " ".join(type_name.replace(" &", "&").replace(" *", "*").split())


def _load_gpr(n):
    if n <= 10:
        return "c->r[%d]" % n
    return "ld32(c->r[1] + %d)" % (8 + (n - 3) * 4)


def _arg_expr(type_name, gpr, fpr):
    """Return (expr, gpr, fpr)."""
    ty = _norm_type(type_name)
    base = ty.replace("const ", "").replace("volatile ", "").strip()
    if base in FLOAT_TYPES or base.replace("const ", "") in FLOAT_TYPES:
        if fpr > 8:
            raise ValueError("too many float arguments")
        expr = "static_cast<%s>(c->f[%d].ps0)" % (ty, fpr)
        return expr, gpr, fpr + 1
    if base in INT64_TYPES:
        if (gpr - 3) % 2:
            gpr += 1
        hi = _load_gpr(gpr)
        lo = _load_gpr(gpr + 1)
        expr = "static_cast<%s>(((uint64_t)%s << 32) | %s)" % (ty, hi, lo)
        return expr, gpr + 2, fpr
    if "&" in ty or "*" in ty or "[" in ty:
        if ty.endswith("&") and not ty.endswith("&&"):
            pointee = ty[:-1].strip()
            expr = "(*reinterpret_cast<%s*>((void*)(uintptr_t)%s))" % (pointee, _load_gpr(gpr))
        else:
            expr = "reinterpret_cast<%s>((void*)(uintptr_t)%s)" % (ty.replace("&&", "*"), _load_gpr(gpr))
        return expr, gpr + 1, fpr
    expr = "static_cast<%s>(%s)" % (ty, _load_gpr(gpr))
    return expr, gpr + 1, fpr


def build_call(sig, class_scope):
    """Return (statements_without_indent, kind) or raise ValueError."""
    static, ret, scope, name, params_s = _parse_head(sig)
    if not scope:
        scope = class_scope
    params = _split_params(params_s)
    types = []
    for param in params:
        ty, _nm = _split_name(param)
        if not ty:
            raise ValueError("empty parameter")
        types.append(ty)
    is_dtor = name.startswith("~")
    leaf = name[1:] if is_dtor else name
    is_ctor = (not ret) and (not is_dtor) and scope and leaf == scope.split("::")[-1]
    is_method = bool(scope) and not static and not is_ctor
    gpr = 4 if (is_method or is_dtor or is_ctor) else 3
    fpr = 1
    args = []
    for ty in types:
        expr, gpr, fpr = _arg_expr(ty, gpr, fpr)
        args.append(expr)
    arglist = ", ".join(args)
    qual = (scope + "::") if scope else ""
    if is_ctor:
        expr = "new ((void*)(uintptr_t)c->r[3]) %s(%s)" % (scope, arglist)
        return ["%s;" % expr], "ctor"
    if is_dtor:
        cls = scope or leaf
        return [
            "auto* self = reinterpret_cast<%s*>((void*)(uintptr_t)c->r[3]);" % cls,
            "self->~%s();" % cls.split("::")[-1],
        ], "dtor"
    if is_method or (scope and not static and name == "operator()"):
        call = "self->%s(%s)" % (name, arglist)
        pre = ["auto* self = reinterpret_cast<%s*>((void*)(uintptr_t)c->r[3]);" % scope]
    elif scope and static:
        call = "%s%s(%s)" % (qual, name, arglist)
        pre = []
    else:
        call = "%s%s(%s)" % (qual, name, arglist)
        pre = []
    ret_kind = _return_kind(ret)
    if ret_kind == "void":
        return pre + ["%s;" % call], "void"
    if ret_kind == "float":
        return pre + ["c->f[1].ps0 = (double)(%s);" % call], "float"
    if ret_kind == "u64":
        return pre + [
            "auto __ret = (%s);" % call,
            "c->r[3] = (uint32_t)((uint64_t)__ret >> 32);",
            "c->r[4] = (uint32_t)__ret;",
        ], "u64"
    if ret_kind == "ref":
        return pre + [
            "auto& __ret = (%s);" % call,
            "c->r[3] = (uint32_t)(uintptr_t)&__ret;",
        ], "ref"
    if ret_kind == "ptr":
        return pre + ["c->r[3] = (uint32_t)(uintptr_t)(%s);" % call], "ptr"
    return pre + ["c->r[3] = (uint32_t)(%s);" % call], "int"


def _return_kind(ret):
    ret = _norm_type(ret)
    if not ret or ret == "void":
        return "void"
    if ret.endswith("&") and not ret.endswith("&&"):
        return "ref"
    if "*" in ret or ret.endswith("&&"):
        return "ptr"
    base = ret.replace("const ", "").replace("volatile ", "").strip()
    if base in FLOAT_TYPES:
        return "float"
    if base in INT64_TYPES:
        return "u64"
    return "int"


class Func(object):
    def __init__(self, addr, path, sig, call, kind):
        self.addr = addr
        self.path = path
        self.sig = sig
        self.call = call
        self.kind = kind


def collect(root, hooks_dir):
    hooked = hook_addresses(hooks_dir)
    funcs = []
    skipped = []
    for dirpath, _, files in os.walk(root):
        for name in files:
            if not name.endswith((".h", ".hpp", ".cpp")):
                continue
            path = os.path.join(dirpath, name)
            text = open(path, encoding="utf-8", errors="replace").read()
            lines = text.splitlines()
            classes = line_classes(text)
            i = 0
            while i < len(lines):
                m = ADDR_RE.match(lines[i])
                if not m:
                    i += 1
                    continue
                addr = int(m.group(1), 16)
                kind, sig, nxt = _gather(lines, i)
                where = "%s:%d" % (os.path.relpath(path, root), i + 1)
                if kind != "def":
                    if kind == "decl":
                        pass
                    else:
                        skipped.append((addr, where, "no function body"))
                    i = max(nxt, i + 1)
                    continue
                if addr in hooked:
                    skipped.append((addr, where, "port hook"))
                    i = max(nxt, i + 1)
                    continue
                scope = classes[i] if i < len(classes) else ""
                try:
                    call, ckind = build_call(sig, scope)
                except ValueError as exc:
                    skipped.append((addr, where, "unparsed: %s" % exc))
                    i = max(nxt, i + 1)
                    continue
                funcs.append(Func(addr, path, sig, call, ckind))
                i = max(nxt, i + 1)
    funcs.sort(key=lambda f: f.addr)
    seen = set()
    unique = []
    for func in funcs:
        if func.addr in seen:
            skipped.append((func.addr, func.path, "duplicate address"))
            continue
        seen.add(func.addr)
        unique.append(func)
    return unique, skipped


def _wrap_text(path, funcs):
    lines = [
        "// Generated by tools/decomp_build/gen.py. Do not edit.",
        "#define private public",
        "#define protected public",
        '#include "%s"' % path.replace("\\", "/"),
        "#undef private",
        "#undef protected",
        "#undef static_assert",
        '#include "ppc.h"',
        '#include "decomp/decomp.h"',
        "",
    ]
    for func in funcs:
        lines.append('extern "C" void f_%08X_orig(Cpu*);' % func.addr)
        lines.append('extern "C" void hook_%08X(Cpu* c) {' % func.addr)
        lines.append("    if (!decomp::on(%d)) { f_%08X_orig(c); return; }" % (func.index, func.addr))
        for stmt in func.call:
            lines.append("    %s" % stmt)
        lines.append("}")
        lines.append("")
    return "\n".join(lines) + "\n"


def write(out, funcs, skipped, src_root):
    os.makedirs(out, exist_ok=True)
    by_path = {}
    for i, func in enumerate(funcs):
        func.index = i
        by_path.setdefault(func.path, []).append(func)
    # Compile every decomp source so a hooked body can call a helper in another file.
    sources = []
    for dirpath, _, files in os.walk(os.path.join(src_root, "src")):
        for name in files:
            if name.endswith(".cpp"):
                sources.append(os.path.join(dirpath, name))
    for path in by_path:
        if path not in sources:
            sources.append(path)
    sources = sorted(set(sources))
    written = []
    for n, path in enumerate(sources):
        wrap = os.path.join(out, "wrap_%03d.cpp" % n)
        text = _wrap_text(path, by_path.get(path, []))
        _write_if_changed(wrap, text)
        written.append(wrap)
    slots = ["// Generated by tools/decomp_build/gen.py. Do not edit.",
             '#include "decomp/decomp.h"',
             "namespace decomp {",
             "Slot g_slots[] = {"]
    if funcs:
        for func in funcs:
            slots.append("    {0x%08Xu, 1}," % func.addr)
    else:
        slots.append("    {0u, 0},")
    slots.append("};")
    slots.append("const unsigned g_slot_count = %d;" % len(funcs))
    slots.append("}")
    slots.append("")
    _write_if_changed(os.path.join(out, "slots.cpp"), "\n".join(slots))
    list_text = "".join("%08X\n" % func.addr for func in funcs)
    _write_if_changed(os.path.join(out, "decomp_hooks.txt"), list_text)
    skip_lines = ["%08X  %s  %s" % (addr, why, where) for addr, where, why in skipped]
    _write_if_changed(os.path.join(out, "skipped.txt"), ("\n".join(skip_lines) + "\n") if skip_lines else "")
    return written


def _write_if_changed(path, text):
    try:
        with open(path, encoding="utf-8") as handle:
            if handle.read() == text:
                return
    except OSError:
        pass
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(text)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    ap.add_argument("--root", default=os.path.join(repo_root(), "vendor", "decomp"))
    ap.add_argument("--hooks", default=os.path.join(repo_root(), "recomp"))
    args = ap.parse_args()
    funcs, skipped = collect(args.root, args.hooks)
    write(args.out, funcs, skipped, args.root)
    print("decomp: %d functions, %d skipped -> %s" % (len(funcs), len(skipped), args.out))


if __name__ == "__main__":
    main()

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
import hashlib
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
        with open(path, encoding="utf-8", errors="replace") as handle:
            raw_lines = handle.readlines()
        for raw in raw_lines:
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
        call = "self->%s%s(%s)" % (qual, name, arglist)
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
            with open(path, encoding="utf-8", errors="replace") as handle:
                text = handle.read()
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


_SCALAR = {
    "u8", "u16", "u32", "u64", "s8", "s16", "s32", "s64",
    "f32", "f64", "float", "double", "bool", "int", "char", "short",
    "unsigned", "unsigned int", "long", "unsigned long",
    "size_t", "uintptr_t", "intptr_t", "uint32_t", "int32_t",
}


def _param_is_guest(part):
    typ, _nm = _split_name(part)
    typ = " ".join(typ.split())
    if typ.endswith("&"):
        return False
    if "*" in typ:
        return "<" not in typ.split("*", 1)[1]
    base = typ
    for word in ("const", "volatile"):
        base = base.replace(word, "")
    base = " ".join(base.split())
    return base in _SCALAR


def _return_ok(ret):
    kind = _return_kind(ret)
    if kind in ("ref", "u64"):
        return False
    if kind in ("void", "float", "ptr"):
        return True
    if "<" in ret or "::" in ret:
        return False
    return True


def _named_signature(sig, params):
    named = []
    for n, part in enumerate(params):
        typ, nm = _split_name(part)
        if not nm:
            nm = "a%d" % n
            part = ("%s %s" % (typ, nm)).strip()
        named.append(part)
    depth = 0
    start = end = None
    for i, ch in enumerate(sig):
        if ch == "(":
            if depth == 0:
                start = i
            depth += 1
        elif ch == ")":
            depth -= 1
            if depth == 0:
                end = i
    if start is None or end is None:
        raise ValueError("no parameter list")
    return sig[:start + 1] + ", ".join(named) + sig[end:]


def _trampoline(addr, sig):
    _static, ret, _scope, _name, params_s = _parse_head(sig)
    params = _split_params(params_s)
    if not _return_ok(ret) or any(not _param_is_guest(part) for part in params):
        raise ValueError("parameter is not a guest word")
    sig = _named_signature(sig, params)
    names = []
    for n, part in enumerate(params):
        _typ, nm = _split_name(part)
        if not nm:
            nm = "a%d" % n
        names.append(nm)
    kind = _return_kind(ret)
    call_args = "0x%08Xu" % addr
    if names:
        call_args += ", " + ", ".join(names)
    if kind == "void":
        stmt = "decomp::call<void>(%s);" % call_args
    elif kind == "float":
        stmt = "return decomp::call<float>(%s);" % call_args
    else:
        stmt = "return decomp::call<%s>(%s);" % (ret, call_args)
    if not sig.lstrip().startswith("inline "):
        sig = "inline " + sig.lstrip()
    return "%s {\n    %s\n}" % (sig, stmt)


def _add_trampolines(text, defined):
    """A declaration with an address and no body calls the recompiled function."""
    lines = text.splitlines(keepends=True)
    out = []
    i = 0
    seen = set()
    while i < len(lines):
        match = ADDR_RE.match(lines[i])
        if not match:
            out.append(lines[i])
            i += 1
            continue
        addr = int(match.group(1), 16)
        kind, sig, nxt = _gather(lines, i)
        nxt = max(nxt, i + 1)
        if kind != "decl" or addr in defined or addr in seen:
            out.extend(lines[i:nxt])
            i = nxt
            continue
        try:
            body = _trampoline(addr, sig)
        except ValueError:
            out.extend(lines[i:nxt])
            i = nxt
            continue
        seen.add(addr)
        out.append(lines[i] if lines[i].endswith("\n") else lines[i] + "\n")
        out.append(body + "\n")
        i = nxt
    return "".join(out)


def _type_only_args(args):
    if not args or args == "void":
        return ""
    parts = []
    for part in _split_params(args):
        typ, _nm = _split_name(part)
        parts.append(typ if typ else part)
    return ", ".join(parts)


def _rewrite_fn_ptrs(text):
    """A guest function pointer is an address. Call it through the recompiled function."""
    aliases = []
    for match in re.finditer(r"using\s+(\w+)\s*=\s*([^\n;]+);", text):
        typ = " ".join(match.group(2).split())
        found = re.match(r"^(.*)\(\s*\*\s*\)\s*\((.*)\)\s*$", typ)
        if found:
            aliases.append((match.end(), match.group(1), found.group(1).strip(), _type_only_args(found.group(2).strip())))
    if not aliases:
        return text
    pattern = re.compile(r"(?:auto\s+(\w+)|(\w+)\s+(\w+))\s*=\s*reinterpret_cast<(\w+)>\((.*)\)\s*;")
    out = []
    last = 0
    for match in pattern.finditer(text):
        if match.group(1):
            var = match.group(1)
            alias_name = match.group(4)
        elif match.group(2) == match.group(4):
            var = match.group(3)
            alias_name = match.group(4)
        else:
            continue
        chosen = None
        for end, alias, ret, args in aliases:
            if alias == alias_name and end <= match.start():
                chosen = (ret, args)
        if chosen is None:
            continue
        ret, args = chosen
        targ = ret if not args else ret + ", " + args
        expr = match.group(5).strip()
        inner = _deref_inner(expr)
        if inner is None:
            addr = "decomp::as_fn(%s)" % expr
        else:
            addr = "decomp::fn_addr(%s)" % inner
        out.append(text[last:match.start()])
        out.append("auto %s = decomp::make_fn<%s>(%s);" % (var, targ, addr))
        last = match.end()
    out.append(text[last:])
    return "".join(out)


def _deref_inner(expr):
    prefix = "*reinterpret_cast<"
    if not expr.startswith(prefix):
        return None
    depth = 1
    i = len(prefix)
    while i < len(expr) and depth:
        if expr[i] == "<":
            depth += 1
        elif expr[i] == ">":
            depth -= 1
        i += 1
    if depth or i >= len(expr) or expr[i] != "(":
        return None
    depth = 0
    start = i
    while i < len(expr):
        if expr[i] == "(":
            depth += 1
        elif expr[i] == ")":
            depth -= 1
            if depth == 0:
                if expr[i + 1:].strip():
                    return None
                return expr[start + 1:i]
        i += 1
    return None


_WORD = {"u32", "s32", "uint32_t", "unsigned", "unsignedint", "uintptr_t"}
_HALF = {"u16", "s16", "uint16_t"}
_FLOAT = {"f32", "float"}


def _load_repl(typ, expr):
    if "(*)" in typ or not typ.endswith("*"):
        return None
    pointee = typ[:-1].strip()
    bare = "".join(pointee.replace("const", "").replace("volatile", "").split())
    if bare in _WORD:
        return "decomp::be32(%s)" % expr
    if bare in _HALF:
        return "decomp::be16(%s)" % expr
    if bare in _FLOAT:
        return "decomp::bef32(%s)" % expr
    if "*" in pointee:
        return "decomp::beptr<%s>(%s)" % (pointee, expr)
    return None


def _rewrite_guest_loads(text):
    """Guest words are 4 bytes and big-endian. A host load of those words is the wrong value."""
    key = "*reinterpret_cast<"
    changed = True
    while changed:
        changed = False
        i = 0
        while True:
            j = text.find(key, i)
            if j < 0:
                break
            depth = 1
            t = j + len(key)
            while t < len(text) and depth:
                if text[t] == "<":
                    depth += 1
                elif text[t] == ">":
                    depth -= 1
                t += 1
            if depth or t >= len(text) or text[t] != "(":
                i = j + len(key)
                continue
            typ = " ".join(text[j + len(key):t - 1].split())
            depth = 0
            p = t
            while p < len(text):
                if text[p] == "(":
                    depth += 1
                elif text[p] == ")":
                    depth -= 1
                    if depth == 0:
                        break
                p += 1
            if depth:
                break
            expr = text[t + 1:p]
            if key in expr:
                i = j + len(key)
                continue
            repl = _load_repl(typ, expr)
            if repl is None:
                i = p + 1
                continue
            text = text[:j] + repl + text[p + 1:]
            changed = True
            i = j + len(repl)
    return text


def _prepare_source(text, defined):
    text = _add_trampolines(text, defined)
    text = _rewrite_fn_ptrs(text)
    text = _rewrite_guest_loads(text)
    return _widen_narrow_casts(text)


_NARROW = {"u32", "s32", "u16", "s16", "u8", "s8"}


def _widen_narrow_casts(text):
    """A 32-bit pointer cast is a truncating cast on this host. The low bits are the guest address."""
    key = "reinterpret_cast<"
    out = []
    i = 0
    while True:
        j = text.find(key, i)
        if j < 0:
            out.append(text[i:])
            break
        k = text.find(">", j)
        if k < 0 or k + 1 >= len(text) or text[k + 1] != "(":
            out.append(text[i:j + len(key)])
            i = j + len(key)
            continue
        typ = "".join(text[j + len(key):k].split())
        if typ not in _NARROW:
            out.append(text[i:k + 1])
            i = k + 1
            continue
        depth = 0
        p = k + 1
        while p < len(text):
            if text[p] == "(":
                depth += 1
            elif text[p] == ")":
                depth -= 1
                if depth == 0:
                    break
            p += 1
        expr = text[k + 2:p]
        out.append(text[i:j])
        out.append("static_cast<%s>(reinterpret_cast<uintptr_t>(%s))" % (text[j + len(key):k], expr))
        i = p + 1
    return "".join(out)


def _include_path(out, path, defined):
    with open(path, encoding="utf-8", errors="replace") as handle:
        text = handle.read()
    widened = _prepare_source(text, defined)
    if widened == text:
        return path.replace("\\", "/")
    filt = os.path.join(out, "filt")
    os.makedirs(filt, exist_ok=True)
    dest = os.path.join(filt, hashlib.md5(path.encode()).hexdigest()[:12] + ".cpp")
    _write_if_changed(dest, widened)
    return os.path.abspath(dest).replace("\\", "/")


def _wrap_text(out, path, funcs, defined):
    lines = [
        "// Generated by tools/decomp_build/gen.py. Do not edit.",
        "#define private public",
        "#define protected public",
        '#include "%s"' % _include_path(out, path, defined),
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
        lines.append("    decomp::bind(c);")
        for stmt in func.call:
            lines.append("    %s" % stmt)
        lines.append("}")
        lines.append("")
    return "\n".join(lines) + "\n"


def write(out, funcs, skipped, src_root, drop):
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
    sources = sorted(set(sources) - set(drop))
    defined = set(func.addr for func in funcs)
    written = []
    for n, path in enumerate(sources):
        wrap = os.path.join(out, "wrap_%03d.cpp" % n)
        text = _wrap_text(out, path, by_path.get(path, []), defined)
        _write_if_changed(wrap, text)
        written.append(wrap)
    for name in os.listdir(out) if os.path.isdir(out) else []:
        if name.startswith("wrap_") and name.endswith(".cpp") and os.path.join(out, name) not in written:
            os.remove(os.path.join(out, name))
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
    ap.add_argument("--drop", default="")
    args = ap.parse_args()
    funcs, skipped = collect(args.root, args.hooks)
    drop = set()
    if args.drop and os.path.isfile(args.drop):
        with open(args.drop, encoding="utf-8") as handle:
            for line in handle:
                line = line.split("#", 1)[0].strip()
                if not line:
                    continue
                path = line if os.path.isabs(line) else os.path.join(args.root, line)
                drop.add(os.path.normpath(path))
    kept = []
    for func in funcs:
        if os.path.normpath(func.path) in drop:
            skipped.append((func.addr, func.path, "does not compile on the host"))
        else:
            kept.append(func)
    funcs = kept
    write(args.out, funcs, skipped, args.root, drop)
    print("decomp: %d functions, %d skipped -> %s" % (len(funcs), len(skipped), args.out))


if __name__ == "__main__":
    main()

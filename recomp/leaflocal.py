"""Keep a leaf function's guest registers in C locals.

ppc2c turns every register access into a Cpu struct access (c->r[N], c->f[N].psK). With
`Cpu* __restrict c` the C compiler keeps values in host registers inside a function, but it must
store every register the function changed back into the struct at each return: it cannot know that
callers ignore most of them. A leaf function (one that calls nothing) can instead use locals and,
at a return (blr), write back only what callers can see under the PowerPC EABI: the return values
(r3, r4, f1, f2), the stack pointer and small-data bases (r1, r2, r13) and the callee-saved
registers (r14-r31, f14-f31). Any other exit (a tail jump or dispatch to other code) writes back
every register the function changed.

The condition register gets the same treatment: each CR bit the function uses becomes a local, and a
return writes back only the callee-saved fields cr2-cr4 (bits 8-19), the ones crlive.py also treats
as live at a return. Compares write the locals through ppc.h's crl_* helpers. A function that reads
or writes the CR as a whole (mfcr, mtcrf, stwcx.) keeps its CR in the struct.

The statements are the same C expressions with the struct accesses replaced, so the results are
the same; only the dead stores of scratch registers go away. Not applied to functions with calls
(transform_nonleaf below handles those), hooks, instruction hooks, traps or unimplemented
instructions.
"""
import os
import re

import ppc2c

# NSMBU_RECOMP_LEAF_POISON=1: a checking build; at each return the scratch registers the function
# used get garbage, so a caller that relied on one would break visibly
POISON = os.environ.get("NSMBU_RECOMP_LEAF_POISON", "0") == "1"

_R = re.compile(r"c->r\[(\d+)\]")
_F = re.compile(r"c->f\[(\d+)\]\.ps([01])")
_PSQ_LOAD = re.compile(r"\bpsq_load\(c, (\d+), ")
_PSQ_STORE = re.compile(r"\bpsq_store\(c, (\d+), ")
_CALL = re.compile(r"(?<!MUSTTAIL return )\b(f_[0-9A-F]{8}(?:_orig)?|imp_\w+|ppc_dispatch)\(c\b")
_BLOCKERS = ("site_", "ppc_unimplemented", "ppc_trap", "hook_")
_RET = re.compile(r"(?<![\w])return;")
_TAIL = re.compile(r"MUSTTAIL return [^;]*;")

_CR = re.compile(r"(?<!&)\bc->cr\[(\d+)\]")
_CR_ADDR = re.compile(r"&c->cr\[(\d+)\]")
_MCRF = re.compile(r"memmove\(&c->cr\[(\d+)\], &c->cr\[(\d+)\], 4\);")
_CR_SET = re.compile(r"\bcr_set_([suf])(_m)?\(c, (\d+), ")
_CR0_RC = re.compile(r"\bcr0_rc(_m)?\(c, ")
_CR_READ = re.compile(r"\bppc_cr_read\(c, (\d+), ")
_CR_LEFT = re.compile(r"c->cr\b|\bppc_mfcr\b|\bppc_mtcrf\b|\bppc_stwcx\b|\bcr_set_|\bcr0_rc|\bppc_cr_read\(")
_CR_LOCAL = re.compile(r"\bcr(\d+)\b")
RET_CR = set(range(8, 20))  # cr2-cr4


def _field_ptrs(f):
    return "&cr%d, &cr%d, &cr%d, &cr%d, c, " % (4 * f, 4 * f + 1, 4 * f + 2, 4 * f + 3)


def localize_cr(stmts):
    """The statements with CR bits as locals cr0..cr31, and the set of bits used; or None if the
    function uses the CR in a way this does not cover."""
    out = []
    for s in stmts:
        s = _MCRF.sub(lambda m: "{ %s %s }" % (
            " ".join("uint8_t t%d = c->cr[%d];" % (i, int(m.group(2)) + i) for i in range(4)),
            " ".join("c->cr[%d] = t%d;" % (int(m.group(1)) + i, i) for i in range(4))), s)
        s = _CR_SET.sub(lambda m: "crl_set_%s%s(%s" % (m.group(1), m.group(2) or "", _field_ptrs(int(m.group(3)))), s)
        s = _CR0_RC.sub(lambda m: "crl0_rc%s(%s" % (m.group(1) or "", _field_ptrs(0)), s)
        s = _CR_READ.sub(lambda m: "ppc_crl_read(c, cr%s, %s, " % (m.group(1), m.group(1)), s)
        s = _CR.sub(lambda m: "cr%s" % m.group(1), s)
        if _CR_LEFT.search(s) or _CR_ADDR.search(s):
            return None
        out.append(s)
    used = set()
    for s in out:
        used |= {int(n) for n in _CR_LOCAL.findall(s) if int(n) < 32}
    return out, used


RET_GPR = {1, 2, 3, 4, 13} | set(range(14, 32))
RET_FPR = {1, 2} | set(range(14, 32))


def eligible(stmts):
    for s in stmts:
        if _CALL.search(s) or any(b in s for b in _BLOCKERS):
            return False
    return True


def transform(stmts):
    """stmts: the function's statements (after PPC_ENTER). Returns (prologue, new statements,
    write-back for falling off the end into the next function)."""
    gprs, fprs = set(), set()
    out = []
    for s in stmts:
        s = _PSQ_LOAD.sub(lambda m: (fprs.add(int(m.group(1))), "psq_load_l(c, &f%s_0, &f%s_1, " % (m.group(1), m.group(1)))[1], s)
        s = _PSQ_STORE.sub(lambda m: (fprs.add(int(m.group(1))), "psq_store_l(c, f%s_0, f%s_1, " % (m.group(1), m.group(1)))[1], s)
        s = _R.sub(lambda m: (gprs.add(int(m.group(1))), "r%s" % m.group(1))[1], s)
        s = _F.sub(lambda m: (fprs.add(int(m.group(1))), "f%s_%s" % (m.group(1), m.group(2)))[1], s)
        out.append(s)
    crs = set()
    localized = localize_cr(out)
    if localized:
        out, crs = localized
    # every register the function uses is written back where callers may see it (written or not:
    # no reliance on spotting each assignment form in the C)
    gw, fw = gprs, fprs
    prologue = []
    if crs:
        prologue.append("uint8_t %s;" % ", ".join("cr%d = c->cr[%d]" % (n, n) for n in sorted(crs)))
    if gprs:
        prologue.append("uint32_t %s;" % ", ".join("r%d = c->r[%d]" % (n, n) for n in sorted(gprs)))
    if fprs:
        prologue.append("double %s;" % ", ".join("f%d_0 = c->f[%d].ps0, f%d_1 = c->f[%d].ps1" % (n, n, n, n) for n in sorted(fprs)))

    def wb(gset, fset, cset):
        parts = ["c->r[%d] = r%d;" % (n, n) for n in sorted(gset)]
        parts += ["c->f[%d].ps0 = f%d_0; c->f[%d].ps1 = f%d_1;" % (n, n, n, n) for n in sorted(fset)]
        parts += ["c->cr[%d] = cr%d;" % (n, n) for n in sorted(cset)]
        return " ".join(parts)

    ret_wb = wb(gw & RET_GPR, fw & RET_FPR, crs & RET_CR)
    if POISON:
        # only registers the function changes: a register it merely reads keeps its value in the
        # struct, as on the hardware
        text = "\n".join(out)
        g_set = {n for n in gprs if re.search(r"\br%d\s*(=[^=]|\|=|&=|\^=|\+=|-=|<<=|>>=)" % n, text)}
        f_set = {n for n in fprs if re.search(r"(\bf%d_[01]\s*=[^=]|&f%d_[01])" % (n, n), text)}
        ret_wb += " " + " ".join(["c->r[%d] = 0xDEADBEEFu;" % n for n in sorted(g_set - RET_GPR)] +
                                 ["c->f[%d].ps0 = c->f[%d].ps1 = __builtin_nan(\"\");" % (n, n) for n in sorted(f_set - RET_FPR)])
    all_wb = wb(gw, fw, crs)
    out = [_TAIL.sub(lambda m: "{ %s %s }" % (all_wb, m.group(0)) if all_wb else m.group(0),
                     _RET.sub(lambda m: "{ %s return; }" % ret_wb if ret_wb else "return;", s)) for s in out]
    return prologue, out, all_wb


# ---------------------------------------------------------------- functions with calls
# The same locals in a function that calls others. Before each call the registers the function
# assigned since the last sync go back to the Cpu struct. A later return, tail call or loop-head
# sync also stores them when they are dirty. After the call, a register is read again only when it
# is live (NSMBU_RECOMP_NONLEAF_LIVE=0 reads every used register again). r0, r11 and r12 are still
# tracked as dirty locals between calls like any other register, but they are always written back
# before a call (a callee loads them from the struct in its prologue) and always reloaded when live
# after a call (a callee with calls writes them back at its own calls). One float half does not
# stand in for the other: a store writes only the half that changed. The callee-saved registers
# (r1, r14-r31, f14-f31) are read again when they are live, although the PowerPC EABI has callees
# preserve them: a full save state may replace a thread's registers while it waits inside a call,
# and the compiler's save/restore helpers change them on purpose. Between calls the values stay in
# locals, so the C compiler can keep them in host registers and drop the stores of values that are
# overwritten before the next call or exit. Functions whose code passes the Cpu to anything else
# keep the struct form.
_NL_CALL = re.compile(r"(?<!MUSTTAIL return )\b(f_[0-9A-F]{8}(?:_orig)?|imp_\w+|ppc_dispatch|site_[0-9A-F]{8})\(c\);"
                      r"|\b(?:ppc_trap|ppc_unimplemented)\(c[^;]*\);")
_C_USE = re.compile(r"\b(\w+)\(c\b")
_NL_ALLOWED = {"cr_set_s", "cr_set_u", "cr_set_f", "cr_set_s_m", "cr_set_u_m", "cr_set_f_m", "cr0_rc", "cr0_rc_m",
               "psq_load_l", "psq_store_l", "ppc_mfcr", "ppc_mtcrf", "ppc_fctiw", "ppc_lwarx", "ppc_stwcx",
               "ppc_mfxer", "ppc_mtxer", "ppc_cr_read", "ppc_dispatch", "ppc_trap", "ppc_unimplemented"}


_ASSIGN_R = re.compile(r"\br(\d+)\s*(?:=(?!=)|\|=)|&r(\d+)\b")
_ASSIGN_F = re.compile(r"\bf(\d+)_[01]\s*=(?!=)|&f(\d+)_[01]\b")
_GOTO = re.compile(r"goto L_([0-9A-F]{8});")


def _assigned(text):
    regs = set()
    for m in _ASSIGN_R.finditer(text):
        regs.add(int(m.group(1) or m.group(2)))
    for m in _ASSIGN_F.finditer(text):
        regs.add(32 + int(m.group(1) or m.group(2)))
    return regs


_LHS_R = re.compile(r"\br\d+\s*=(?!=)")
_LHS_F = re.compile(r"\bf\d+_[01]\s*=(?!=)")
_ADDR_REG = re.compile(r"&(?:r\d+|f\d+_[01])\b")
_READ_R = re.compile(r"\br(\d+)\b")
_READ_F = re.compile(r"\bf(\d+)_[01]\b")

reloads_written = 0
reloads_left_out = 0
reload_calls = 0


def reset_stats():
    global reloads_written, reloads_left_out, reload_calls
    reloads_written = 0
    reloads_left_out = 0
    reload_calls = 0


_READ_FH = re.compile(r"\bf(\d+)_([01])\b")
_DEF_FH = re.compile(r"\bf(\d+)_([01])\s*=(?!=)|&f(\d+)_([01])\b")


def _half_id(n, half):
    return 32 + 2 * int(n) + int(half)


def _mask_halves(halves, fn_used):
    out = set()
    for h in halves:
        if h < 32:
            if h in fn_used:
                out.add(h)
        elif 32 + (h - 32) // 2 in fn_used:
            out.add(h)
    return out


def _half_defs(text):
    regs = set()
    for m in _ASSIGN_R.finditer(text):
        regs.add(int(m.group(1) or m.group(2)))
    for m in _DEF_FH.finditer(text):
        regs.add(_half_id(m.group(1) or m.group(3), m.group(2) or m.group(4)))
    return regs


def _half_reads(text):
    text = _LHS_R.sub(" ", text)
    text = _LHS_F.sub(" ", text)
    text = _ADDR_REG.sub(" ", text)
    regs = {int(n) for n in _READ_R.findall(text)}
    regs |= {_half_id(n, h) for n, h in _READ_FH.findall(text)}
    return regs


def _wholes(halves):
    regs = {h for h in halves if h < 32}
    for h in halves:
        if h >= 32:
            regs.add(32 + (h - 32) // 2)
    return regs


def _all_dead(fn_used, halves):
    dead = set()
    for r in fn_used:
        if r < 32:
            if r not in halves:
                dead.add(r)
        else:
            n = r - 32
            if _half_id(n, 0) not in halves and _half_id(n, 1) not in halves:
                dead.add(r)
    return dead


def _kept_across_call(live):
    reload = _wholes(live)
    kill = set()
    for w in reload:
        if w < 32:
            kill.add(w)
        else:
            n = w - 32
            kill.add(_half_id(n, 0))
            kill.add(_half_id(n, 1))
    return live - kill


def used(text):
    text = _LHS_R.sub(" ", text)
    text = _LHS_F.sub(" ", text)
    text = _ADDR_REG.sub(" ", text)
    regs = {int(n) for n in _READ_R.findall(text)}
    regs |= {32 + int(n) for n in _READ_F.findall(text)}
    return regs


def _poison(regs):
    parts = ["r%d = 0xDEADBEEFu;" % n for n in sorted(n for n in regs if n < 32)]
    parts += ["f%d_0 = f%d_1 = __builtin_nan(\"\");" % (n - 32, n - 32)
              for n in sorted(n for n in regs if n >= 32)]
    return " ".join(parts)


def _live_after_calls(kills, reads, succ, extra):
    n = len(kills)
    live_in = [set() for _ in range(n)]
    after = [[set() for _ in range(len(kills[i]) - 1)] for i in range(n)]
    preds = [[] for _ in range(n)]
    for i, nx in enumerate(succ):
        for j in nx:
            preds[j].append(i)
    work = list(range(n))
    queued = [True] * n
    while work:
        i = work.pop()
        queued[i] = False
        live = set()
        for j in succ[i]:
            live |= live_in[j]
        last = len(kills[i]) - 1
        for k in range(last, -1, -1):
            if k == last:
                live = extra[i] | live
            live = reads[i][k] | (live - kills[i][k])
            if k > 0:
                if not live <= after[i][k - 1]:
                    after[i][k - 1] |= live
                live = _kept_across_call(live)
        if not live <= live_in[i]:
            live_in[i] |= live
            for p in preds[i]:
                if not queued[p]:
                    queued[p] = True
                    work.append(p)
    return after


def transform_nonleaf(stmts, addrs):
    """stmts: the statements (site calls already prefixed to theirs), addrs: their guest addresses.
    Returns (prologue, statements, write-back for falling off the end), or None to keep the struct
    form. A forward analysis over the function's jumps tracks which registers may differ from the
    struct ("dirty"); a call writes back only those, an exit only those its caller can see."""
    if any("hook_" in s for s in stmts):
        return None
    gprs, fprs = set(), set()
    out = []
    for s in stmts:
        s = _PSQ_LOAD.sub(lambda m: (fprs.add(int(m.group(1))), "psq_load_l(c, &f%s_0, &f%s_1, " % (m.group(1), m.group(1)))[1], s)
        s = _PSQ_STORE.sub(lambda m: (fprs.add(int(m.group(1))), "psq_store_l(c, f%s_0, f%s_1, " % (m.group(1), m.group(1)))[1], s)
        s = _R.sub(lambda m: (gprs.add(int(m.group(1))), "r%s" % m.group(1))[1], s)
        s = _F.sub(lambda m: (fprs.add(int(m.group(1))), "f%s_%s" % (m.group(1), m.group(2)))[1], s)
        out.append(s)
    for s in out:
        for m in _C_USE.finditer(s):
            name = m.group(1)
            if name not in _NL_ALLOWED and not re.match(r"(f_[0-9A-F]{8}(_orig)?|imp_\w+|site_[0-9A-F]{8})$", name):
                return None
    if not gprs and not fprs:
        return None
    fn_used = gprs | {32 + n for n in fprs}
    n = len(out)
    index = {a: i for i, a in enumerate(addrs)}
    live_on = ppc2c.pass_on("NSMBU_RECOMP_NONLEAF_LIVE")
    poison_on = os.environ.get("NSMBU_RECOMP_LEAF_POISON", "0") == "1"
    global reloads_written, reloads_left_out, reload_calls

    def every_half():
        out_regs = {r for r in fn_used if r < 32}
        for r in fn_used:
            if r >= 32:
                nreg = r - 32
                out_regs.add(_half_id(nreg, 0))
                out_regs.add(_half_id(nreg, 1))
        return out_regs

    # per statement: segments between calls (assignments), the calls, successors
    reads, kills, calls, succ = [], [], [], []
    for i, s in enumerate(out):
        rparts, kparts, pos, cs = [], [], 0, []
        for m in _NL_CALL.finditer(s):
            chunk = s[pos:m.start()]
            rparts.append(_mask_halves(_half_reads(chunk), fn_used))
            kparts.append(_mask_halves(_half_defs(chunk), fn_used))
            cs.append(m)
            pos = m.end()
        chunk = s[pos:]
        rparts.append(_mask_halves(_half_reads(chunk), fn_used))
        kparts.append(_mask_halves(_half_defs(chunk), fn_used))
        reads.append(rparts)
        kills.append(kparts)
        calls.append(cs)
        nx = [i + 1] if i + 1 < n else []  # falling through (a superset after an unconditional jump: safe)
        nx += [index[int(t, 16)] for t in _GOTO.findall(s) if int(t, 16) in index]
        succ.append(nx)
    # loop heads (targets of backward jumps): the edges entering them from before write back what is
    # dirty, so registers set up before a loop are not written back again at each call inside it
    heads = {j for i in range(n) for j in succ[i] if j <= i}
    d_in = [None] * n
    d_in[0] = set()
    work = [0]
    while work:
        i = work.pop()
        d = set(d_in[i])
        for k, part in enumerate(kills[i]):
            d |= part
            if k < len(calls[i]):
                d = set()
        for j in succ[i]:
            arrive = set() if (j in heads and j > i) else d  # a forward edge into a loop head syncs
            if d_in[j] is None or not arrive <= d_in[j]:
                d_in[j] = set(arrive) if d_in[j] is None else d_in[j] | arrive
                work.append(j)
    for i in range(n):
        if d_in[i] is None:
            d_in[i] = every_half()  # not reached by the analysis (cannot happen): write back everything

    def for_return(regs):
        out_regs = {r for r in regs if r < 32 and r in RET_GPR}
        out_regs |= {h for h in regs if h >= 32 and (h - 32) // 2 in RET_FPR}
        return out_regs

    exit_at, end_at = [], []
    for i in range(n):
        ed, end = set(d_in[i]), set(d_in[i])
        for k, part in enumerate(kills[i]):
            ed |= part
            end |= part
            if k < len(calls[i]):
                end = set()
        exit_at.append(ed)
        end_at.append(end)
    extra = [set() for _ in range(n)]
    for i, s in enumerate(out):
        ed = exit_at[i]
        if _RET.search(s):
            extra[i] |= for_return(ed)
        if _TAIL.search(s):
            extra[i] |= ed
        for t in _GOTO.findall(s):
            tgt = int(t, 16)
            if tgt in index and index[tgt] in heads and index[tgt] > i:
                extra[i] |= ed
                break
        if i + 1 < n and i + 1 in heads:
            extra[i] |= end_at[i]
    extra[-1] |= end_at[-1]
    live_after = _live_after_calls(kills, reads, succ, extra)

    def wb(regs):
        parts = ["c->r[%d] = r%d;" % (r, r) for r in sorted(r for r in regs if r < 32)]
        for h in sorted(h for h in regs if h >= 32):
            nreg, half = divmod(h - 32, 2)
            parts.append("c->f[%d].ps%d = f%d_%d;" % (nreg, half, nreg, half))
        return " ".join(parts)

    def rl(gset, fset):
        parts = ["r%d = c->r[%d];" % (r, r) for r in sorted(gset)]
        parts += ["f%d_0 = c->f[%d].ps0; f%d_1 = c->f[%d].ps1;" % (r, r, r, r) for r in sorted(fset)]
        return " ".join(parts)

    final = []
    for i, s in enumerate(out):
        d = set(d_in[i])
        exit_dirty = exit_at[i]
        pieces, pos = [], 0
        for k, m in enumerate(calls[i]):
            d |= kills[i][k]
            pieces.append(s[pos:m.start()])
            wb_regs = set(d)
            live = live_after[i][k]
            reload_regs = set(fn_used if not live_on else _wholes(live))
            if live_on:
                reloads_written += len(reload_regs)
                reloads_left_out += len(_all_dead(fn_used, live))
            else:
                reloads_written += len(fn_used)
            reload_calls += 1
            poison_txt = ""
            if poison_on and live_on:
                keep = set(reload_regs)
                poison_txt = _poison(fn_used - keep)
            inner = " ".join(p for p in (
                wb(wb_regs), m.group(0),
                rl({r for r in reload_regs if r < 32}, {r - 32 for r in reload_regs if r >= 32}),
                poison_txt) if p)
            pieces.append("{ %s }" % inner)
            pos = m.end()
            d = set()
        pieces.append(s[pos:])
        s = "".join(pieces)
        # forward jumps into a loop head write back first (see heads above)
        sync = wb(exit_dirty)
        if sync:
            s = _GOTO.sub(lambda m: "{ %s %s }" % (sync, m.group(0))
                          if int(m.group(1), 16) in index and index[int(m.group(1), 16)] in heads and index[int(m.group(1), 16)] > i
                          else m.group(0), s)
        ret_wb, all_wb = wb(for_return(exit_dirty)), wb(exit_dirty)
        if POISON:
            # checking build: the volatile registers this function sets get garbage at its returns
            g_set = {r for r in exit_dirty if r < 32 and r not in RET_GPR}
            f_set = {(h - 32) // 2 for h in exit_dirty if h >= 32 and (h - 32) // 2 not in RET_FPR}
            ret_wb = (ret_wb + " " + " ".join(["c->r[%d] = 0xDEADBEEFu;" % r for r in sorted(g_set)] +
                      ["c->f[%d].ps0 = c->f[%d].ps1 = __builtin_nan(\"\");" % (r, r) for r in sorted(f_set)])).strip()
        s = _TAIL.sub(lambda m: "{ %s %s }" % (all_wb, m.group(0)) if all_wb else m.group(0),
                      _RET.sub(lambda m: "{ %s return; }" % ret_wb if ret_wb else "return;", s))
        if i + 1 < n and i + 1 in heads and end_at[i]:
            s += " " + wb(end_at[i])
        final.append(s)
    prologue = []
    if gprs:
        prologue.append("uint32_t %s;" % ", ".join("r%d = c->r[%d]" % (r, r) for r in sorted(gprs)))
    if fprs:
        prologue.append("double %s;" % ", ".join("f%d_0 = c->f[%d].ps0, f%d_1 = c->f[%d].ps1" % (r, r, r, r) for r in sorted(fprs)))
    return prologue, final, wb(end_at[-1])

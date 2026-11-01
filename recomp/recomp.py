#!/usr/bin/env python3
"""Statically recompile a Wii U RPX into C.

usage: recomp.py game/code/red-pro2.rpx OUTDIR [--insns-per-file N] [--build NAME] [--jobs N]

--jobs N (default: the CPU count) translates in that many processes. 0 and NSMBU_RECOMP_JOBS select
the same count. 1 stays on one process. Windows, and NSMBU_RECOMP_SPAWN=1, start fresh workers;
other systems share the loaded game.

The rpx is identified by its SHA-256 (recomp/builds.py). Functions are named by their
*canonical* (USA) address, so the runtime refers to the same f_XXXXXXXX whichever build of the game
it was translated from; the dispatch table maps this build's real addresses to them.

Code-quality passes (each on by default; the variable set to 0 when generating turns it off):
  NSMBU_RECOMP_CRLIVE       condition-register liveness: compares store only the CR bits read later (crlive.py)
  NSMBU_RECOMP_LEAF         leaf functions keep guest registers and CR bits in C locals (leaflocal.py)
  NSMBU_RECOMP_NONLEAF      functions with calls keep guest registers in C locals between calls (leaflocal.py)
  NSMBU_RECOMP_NONLEAF_LIVE after each call, reload only registers a later read still needs (leaflocal.py)
  NSMBU_RECOMP_SINGLE       round25 left out for multiplier operands known to be single precision (ppc2c.py)
  NSMBU_RECOMP_SINGLE_IP    ... with entry states and return summaries across functions (singleflow.py)
  NSMBU_RECOMP_GQR          paired-single loads/stores through GQRs the game never writes skip the GQR check
NSMBU_RECOMP_PLAIN=1 turns them all off: the output is then the same as without these passes (the
plain c->r[N] form).
Checking builds (set to 1): NSMBU_RECOMP_CR_CHECK (a dropped CR bit that is read aborts),
NSMBU_RECOMP_LEAF_POISON (scratch registers get garbage at returns, and locals that were not
reloaded after a call), NSMBU_RECOMP_SINGLE_CHECK (an operand left unrounded that round25 would
change is logged).
Hooked functions and functions with instruction-level sites keep the plain c->r[N] form: hook_X /
site_X read and change the Cpu mid-function.

Output:
  OUTDIR/funcs.h         prototypes of every recompiled function and import
  OUTDIR/code_NNN.c      recompiled functions
  OUTDIR/table.c         guest address -> host function table
  OUTDIR/imports.c       weak default implementations of imported functions
  OUTDIR/imports.json    import slot addresses (for the runtime loader)
  OUTDIR/report.txt      statistics and unhandled instructions
"""
import argparse
import bisect
import collections
import io
import json
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(__file__))
import builds as game_builds
from analyze import Program, sext
from ppc2c import translate, Unhandled
from rpx import R_PPC_ADDR16_HA, R_PPC_ADDR16_LO, R_PPC_ADDR16_HI
import ppc2c
import singleflow
import crlive
import leaflocal
import names

class _Output(io.StringIO):
    """An output file that is written only when its text changed: a run on the same game and tools leaves
    every file (and its time) as it was, so the build does not compile the game code again."""

    def __init__(self, path):
        super().__init__()
        self.path = path

    def __exit__(self, *exc):
        if exc[0] is None:
            text = self.getvalue()
            try:
                with open(self.path, encoding="utf-8") as old:
                    same = old.read() == text
            except (OSError, UnicodeDecodeError):
                same = False
            if not same:
                with open(self.path, "w", encoding="utf-8") as f:
                    f.write(text)
        return super().__exit__(*exc)

CRLIVE = ppc2c.pass_on("NSMBU_RECOMP_CRLIVE")
CR_CHECK = os.environ.get("NSMBU_RECOMP_CR_CHECK", "0") == "1"
LEAF = ppc2c.pass_on("NSMBU_RECOMP_LEAF")
NONLEAF = ppc2c.pass_on("NSMBU_RECOMP_NONLEAF")
GQR_STATIC = ppc2c.pass_on("NSMBU_RECOMP_GQR")
_SAVED_SET_RE = re.compile(r"c->r\[(1[4-9]|2\d|3[01])\]\s*(=(?!=)|\|=)|c->f\[(1[4-9]|2\d|3[01])\]\.ps[01]\s*=(?!=)"
                           r"|psq_load\(c, (1[4-9]|2\d|3[01]),")
_TAIL_RE = re.compile(r"MUSTTAIL return f_([0-9A-F]{8})\(c\)")
_RC = None


def _job_count(requested):
    if requested is None:
        raw = os.environ.get("NSMBU_RECOMP_JOBS")
        if raw is None or raw == "":
            return os.cpu_count() or 1
        try:
            requested = int(raw)
        except ValueError:
            raise SystemExit("NSMBU_RECOMP_JOBS must be an integer, not %r" % raw)
    if requested == 0:
        return os.cpu_count() or 1
    if requested < 0:
        raise SystemExit("--jobs must be >= 0")
    return requested


def _import_mp():
    try:
        import _multiprocessing as extension
        import multiprocessing as mp
    except ImportError:
        return None
    return mp if extension is not None else None


def _fork_ok():
    if os.environ.get("NSMBU_RECOMP_SPAWN") == "1" or sys.platform == "win32":
        return False
    try:
        mp = _import_mp()
        if mp is None:
            return False
        mp.get_context("fork")
    except (ImportError, ValueError):
        return False
    return True


class _SeqPool:
    def imap_unordered(self, fn, items, chunksize=1):
        for item in items:
            yield fn(item)

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        return False


def _dump_rc(rc):
    import pickle
    import tempfile
    fd, path = tempfile.mkstemp(prefix="recomp-", suffix=".pkl")
    try:
        with os.fdopen(fd, "wb") as f:
            pickle.dump(rc, f, protocol=pickle.HIGHEST_PROTOCOL)
    except Exception:
        os.remove(path)
        raise
    return path


def _load_rc(path):
    import pickle
    global _RC
    with open(path, "rb") as f:
        _RC = pickle.load(f)


def _open_pool(jobs, rc):
    global _RC
    _RC = rc
    if jobs <= 1:
        return _SeqPool(), None
    try:
        mp = _import_mp()
    except ImportError:
        return _SeqPool(), None
    if mp is None:
        return _SeqPool(), None
    try:
        if _fork_ok():
            try:
                return mp.get_context("fork").Pool(jobs), None
            except OSError:
                pass
        path = _dump_rc(rc)
        try:
            ctx = mp.get_context("spawn")
            return ctx.Pool(jobs, initializer=_load_rc, initargs=(path,)), path
        except Exception:
            os.remove(path)
            raise
    except ImportError:
        _log("recomp: one process; the _multiprocessing module is unavailable")
        return _SeqPool(), None


def _shutdown_pool(pool, pickle_path, failed):
    if not isinstance(pool, _SeqPool):
        if failed:
            pool.terminate()
        else:
            pool.close()
        pool.join()
    if pickle_path:
        try:
            os.remove(pickle_path)
        except OSError:
            pass


def _slices(n, jobs):
    jobs = max(1, min(jobs, n)) if n else 1
    if jobs <= 1 or n == 0:
        return [(0, n)]
    size = (n + jobs - 1) // jobs
    return [(i, min(n, i + size)) for i in range(0, n, size)]


def _clobber_job(bounds):
    rc = _RC
    lo, hi = bounds
    return [rc._clobber_facts(s) for s in rc.sorted_entries[lo:hi]]


def _emit_job(job):
    index, starts, path = job
    rc = _RC
    ppc2c.single_stats[:] = [0, 0]
    leaflocal.reset_stats()
    rc.unhandled = collections.Counter()
    rc.cr_stats = [0, 0]
    rc.leaf_count = 0
    rc.nonleaf_count = 0
    rc.used_imports = set()
    parts = []
    for start in starts:
        src, _count = rc.emit_function(start)
        parts.append(src)
    with _Output(path) as f:
        f.write('#include "funcs.h"\n\n')
        f.write("\n\n".join(parts))
        f.write("\n")
    return {
        "index": index,
        "funcs": len(starts),
        "unhandled": rc.unhandled,
        "cr": (rc.cr_stats[0], rc.cr_stats[1]),
        "leaf": rc.leaf_count,
        "nonleaf": rc.nonleaf_count,
        "used": rc.used_imports,
        "single": (ppc2c.single_stats[0], ppc2c.single_stats[1]),
        "reloads": (leaflocal.reloads_written, leaflocal.reloads_left_out, leaflocal.reload_calls),
    }

def unknown_build_message(path):
    return "%s is not a build of the game this port knows (SHA-256 %s...); known: %s" % (
        path, game_builds.file_sha256(path)[:16],
        ", ".join("%s %s" % (b.name, b.title_id) for b in game_builds.all_builds()))

def c_ident(s):
    return re.sub(r"[^A-Za-z0-9_]", "_", s)

def _log(msg):
    sys.stderr.write(msg + "\n")
    sys.stderr.flush()

def branch_target(addr, w):
    """Static target of a non-linking b/bc, or None."""
    op = w >> 26
    if op == 18 and not (w & 1):
        return (sext(w & 0x03FFFFFC, 26) + (0 if w & 2 else addr)) & 0xFFFFFFFF
    if op == 16 and not (w & 1):
        return (sext(w & 0xFFFC, 16) + (0 if w & 2 else addr)) & 0xFFFFFFFF
    return None

DATA_IMPORT_BASE = 0xC1000000
DATA_IMPORT_STRIDE = 0x1000

class Recompiler:
    def __init__(self, path, build=None):
        """`build` is which build of the game `path` is (recomp/builds.py); by default it is
        identified by its SHA-256, and an rpx that is none of them is refused."""
        self.build = build or game_builds.identify(path)
        if self.build is None:
            raise SystemExit(unknown_build_message(path))
        self.p = Program(path)
        self.p.discover()
        self.entries = set(self.p.entries)
        self.imports = {}
        self.data_import_addr = {}
        for sym in self.p.rpx.symbols:
            if sym.import_lib and sym.type != 3:
                self.imports[sym.value] = (sym.import_lib, sym.name, sym.import_kind)
        for i, slot in enumerate(sorted(s for s, v in self.imports.items() if v[2] == "d")):
            self.data_import_addr[slot] = DATA_IMPORT_BASE + i * DATA_IMPORT_STRIDE
        self._imm_overrides()
        self._fixpoint()
        self._build_interior_dispatch()

        hook_entries, self.skipped_hooks = game_builds.read_hooks(game_builds.hook_files(), self.build)
        hook_entries = self._check_hooks(hook_entries)
        self.canon_of = {}
        self.hooks, self.sites = set(), set()
        for site, canon, addr, _ in hook_entries:
            self.canon_of[addr] = canon
            (self.sites if site else self.hooks).add(addr)

    def _check_hooks(self, hook_entries):
        """A hook is written against the canonical code, so check that it can mean the same thing in
        this build (see recomp/mkbuildmap.py). An instruction-level site inside a function the
        build compiled differently patches different code: that is an error. A hook on the entry of
        such a function wraps it whole, which usually still holds, but it is reported. A line marked
        `same` was already checked, so that wrap is quiet. A function this build does not have is
        skipped. Its mapped address sits inside a different function."""
        self.hooks_in_changed = []
        self.hooks_missing = []
        kept = []
        for site, canon, addr, where, same in hook_entries:
            if self.build.function_missing(canon) or self.build.body_differs(canon):
                fate = game_builds.hook_on_differing(
                    self.build, site, canon, same, addr in self.entries)
                if fate == "skip" and self.build.function_missing(canon):
                    self.hooks_missing.append((where, canon))
                if fate == "skip":
                    continue
                if fate == "refuse":
                    raise SystemExit('%s: %08X is inside a function the %s build compiled differently; the '
                                     'hook is written for the canonical (USA) code. Mark its file with '
                                     '"# builds: USA" or write the hook for this build.' % (
                                         where, canon, self.build.name))
                if fate == "warn":
                    self.hooks_in_changed.append((where, canon, addr))
                    print("warning: %s: the %s build compiled %08X (there: %08X) differently; the hook wraps "
                          "the whole function, so check that it still means the same thing" % (
                              where, self.build.name, canon, addr), file=sys.stderr)
            if not self.p.in_text(addr):
                raise SystemExit("%s: %08X (%s build: %08X) is outside the game's code" % (
                    where, canon, self.build.name, addr))
            if not site and addr not in self.entries:
                raise SystemExit("%s: %08X (%s build: %08X) is not the start of a function there" % (
                    where, canon, self.build.name, addr))
            kept.append((site, canon, addr, where))
        return kept

    def _imm_overrides(self):
        """Resolve the immediates of instructions referencing imported symbols."""
        self.imm_override = {}
        for sec, addr, typ, sym, add in self.p.rpx.relocs:
            if not sym.import_lib or sec.name != ".text":
                continue
            s = (self.data_import_addr.get(sym.value, sym.value) + add) & 0xFFFFFFFF
            v = {R_PPC_ADDR16_HA: ((s + 0x8000) >> 16) & 0xFFFF,
                 R_PPC_ADDR16_LO: s & 0xFFFF,
                 R_PPC_ADDR16_HI: s >> 16}.get(typ)
            if v is not None:
                self.imm_override[addr & ~3] = v

    def _bounds(self):
        self.sorted_entries = sorted(self.entries)

    def func_of(self, a):
        i = bisect.bisect_right(self.sorted_entries, a) - 1
        return self.sorted_entries[i] if i >= 0 else None

    def func_end(self, start):
        i = bisect.bisect_right(self.sorted_entries, start)
        return self.sorted_entries[i] if i < len(self.sorted_entries) else self.p.text_hi

    def _build_interior_dispatch(self):
        self.interior_dispatch = {}
        self.extra_dispatch_addrs = {}
        for _bctr, (base, count) in self.p.jump_tables.items():
            for i in range(count):
                slot = base + 4 * i
                fn = self.func_of(slot)
                if fn is None or slot == fn:
                    continue
                label = branch_target(slot, self.p.word(slot))
                if label is None:
                    continue
                self.interior_dispatch.setdefault(fn, []).append((slot, label))
                self.extra_dispatch_addrs[slot] = fn

    def _fixpoint(self):
        """Branch targets that land inside another function become entries."""
        rounds = 0
        while True:
            self._bounds()
            new = set()
            for i, w in enumerate(self.p.words):
                a = self.p.text_lo + 4 * i
                t = branch_target(a, w)
                if t is None or not self.p.in_text(t) or t in self.entries:
                    continue
                if self.func_of(t) != self.func_of(a):
                    new.add(t)
            rounds += 1
            if not new:
                break
            self.entries |= new
        self.fixpoint_rounds = rounds
        self._bounds()

    def branch(self, addr, tgt):
        if addr in self.p.import_calls:
            lib, name, slot = self.p.import_calls[addr]
            self.used_imports.add(slot)
            return "MUSTTAIL return %s(c);" % self.imp_name(slot)
        if self.cur_start <= tgt < self.cur_end:
            self.labels.add(tgt)
            if tgt <= addr:
                return "PPC_LOOP(); goto L_%08X;" % tgt
            return "goto L_%08X;" % tgt
        if tgt in self.entries:
            return "MUSTTAIL return f_%08X(c);%s" % (self.sym(tgt), self.callee(tgt))
        return "c->pc = 0x%08Xu; MUSTTAIL return ppc_dispatch(c);" % tgt

    def call(self, addr, tgt):
        if addr in self.p.import_calls:
            lib, name, slot = self.p.import_calls[addr]
            self.used_imports.add(slot)
            return "%s(c);" % self.imp_name(slot)
        if addr in self.p.undef_calls:
            return "ppc_unimplemented(c, 0x%08Xu, 0); /* call to undefined symbol */" % addr
        if tgt in self.entries:
            return "f_%08X(c);%s" % (self.sym(tgt), self.callee(tgt))
        return "c->pc = 0x%08Xu; ppc_dispatch(c);" % tgt

    def ret(self):
        return "return;"

    def indirect_jump(self, addr):
        jt = self.p.jump_tables.get(addr)
        if jt:
            base, count = jt
            cases = []
            for i in range(count):
                slot = base + 4 * i
                fn = self.func_of(slot)
                if self.cur_start <= slot < self.cur_end:
                    self.labels.add(slot)
                    cases.append("case 0x%08Xu: goto L_%08X;" % (slot, slot))
                elif fn is not None:
                    cases.append("case 0x%08Xu: c->pc = 0x%08Xu; MUSTTAIL return f_%08X(c);%s" % (
                        slot, slot, self.sym(fn), self.callee(fn)))
            back = any(self.cur_start <= base + 4 * i <= addr for i in range(count))
            return "%sswitch (c->ctr) { %s } c->pc = c->ctr; MUSTTAIL return ppc_dispatch(c);" % (
                "PPC_LOOP(); " if back else "", " ".join(cases))
        return "c->pc = c->ctr; MUSTTAIL return ppc_dispatch(c);"

    def sym(self, addr):
        """Symbol id for a function at `addr` in this build.

        Hooks and the runtime use canonical (USA) addresses. For a regional or pack build, a
        function that comes from the USA code is named by its USA address. A function the other
        build inserted (no USA counterpart) is named with bit 31 set on its own address, so the
        id cannot collide with a USA canonical f_XXXXXXXX.
        """
        if addr in self.canon_of:
            return self.canon_of[addr]
        if self.build.canonical:
            return addr
        canon = self.build.canon_code(addr)
        try:
            if self.build.code(canon) == addr:
                return canon
        except ValueError:
            pass
        return addr | 0x80000000

    def callee(self, addr):
        """The comment for a call site: the callee's name from headers_names.txt."""
        label = self.names.get(self.sym(addr))
        return " /* %s */" % label if label else ""

    def imp_name(self, slot):
        lib, name, kind = self.imports[slot]
        ident = c_ident(name)
        lib_ident = c_ident(lib.replace(".rpl", ""))
        if ident:
            return "imp_%s_%s" % (lib_ident, ident)
        return "imp_%s_%08X" % (lib_ident, slot)

    def find_saved_clobbers(self):
        """Functions that leave a callee-saved register (r14-r31, f14-f31) changed when they return:
        the compiler's register save/restore helpers (stores and loads through r11, the save helper
        also puts the return address in r31, and the frame helpers that push or pop the caller's
        stack frame for it). A function that does not both push and pop a stack frame and that sets
        such a register, itself or in the code it falls or jumps into, counts as one: after a call
        to it, f14-f31 are not assumed to keep their single-precision state (single_dataflow,
        singleflow.py)."""
        frame, sets, nexts = {}, {}, {}
        jobs = getattr(self, "jobs", 1)
        slices = _slices(len(self.sorted_entries), jobs)
        pool, pickle_path = _open_pool(jobs if len(slices) > 1 else 1, self)
        failed = True
        try:
            for part in pool.imap_unordered(_clobber_job, slices, chunksize=1):
                for start, has_frame, assigns, targets in part:
                    frame[start], sets[start], nexts[start] = has_frame, assigns, targets
            failed = False
        finally:
            _shutdown_pool(pool, pickle_path, failed)
        if len(frame) != len(self.sorted_entries):
            raise RuntimeError("register-helper scan did not cover every function")
        clobbers = {f for f in self.sorted_entries if not frame[f] and sets[f]}
        changed = True
        while changed:
            changed = False
            for f in self.sorted_entries:
                if f not in clobbers and not frame[f] and any(t in clobbers for t in nexts[f]):
                    clobbers.add(f)
                    changed = True
        return clobbers

    def _clobber_facts(self, start):
        end = self.func_end(start)
        self.cur_start, self.cur_end = start, end
        self.labels = set()
        push = pop = assigns = False
        targets = set()
        ppc2c.fp_state = 0
        for a in range(start, end, 4):
            w = self.p.word(a)
            op, rd, ra = w >> 26, (w >> 21) & 31, (w >> 16) & 31
            if op == 37 and rd == 1 and ra == 1:  # stwu r1, d(r1): a frame is pushed
                push = True
            if (op == 14 and rd == 1 and ra == 1 and not w & 0x8000) or (op == 32 and rd == 1 and ra == 1):
                pop = True  # addi r1, r1, +d / lwz r1, d(r1): and popped again
            if op == 31 and rd == 1 and ra == 1 and ((w >> 1) & 0x3FF) == 183:  # stwux r1, r1, rB
                push = True
            try:
                st = translate(a, w, self)
            except Unhandled:
                continue
            if _SAVED_SET_RE.search(st):
                assigns = True
            targets.update(int(t, 16) for t in _TAIL_RE.findall(st))
        if end < self.p.text_hi:
            targets.add(end)
        # the frame helpers that push or pop the caller's frame have only one of the two
        return start, push and pop, assigns, targets

    def single_dataflow(self, start, end):
        """For each instruction of the function: the FPR halves known to hold single-precision values
        before it (ppc2c.fp_transfer), as a bit mask. Forward dataflow over the function's branches:
        where paths meet, a half counts only if it does on all of them. Nothing is known at the
        entry, at instruction hooks (they may change registers) or in code no path reaches."""
        n = (end - start) // 4
        words = [self.p.word(start + 4 * i) for i in range(n)]
        succ = []
        for i, w in enumerate(words):
            a = start + 4 * i
            op, lk = w >> 26, w & 1
            nxt = [i + 1] if i + 1 < n else []
            if op == 18 and not lk:
                t = branch_target(a, w)
                out = [(t - start) // 4] if start <= t < end else []
            elif op == 16 and not lk:
                t = branch_target(a, w)
                always = ((w >> 21) & 0x14) == 0x14
                out = ([] if always else nxt) + ([(t - start) // 4] if start <= t < end else [])
            elif op == 19 and ((w >> 1) & 0x3FF) in (16, 528) and not lk:
                always = ((w >> 21) & 0x14) == 0x14
                out = [] if always else list(nxt)
                jt = self.p.jump_tables.get(a)
                if jt:
                    out += [(jt[0] + 4 * k - start) // 4 for k in range(jt[1]) if start <= jt[0] + 4 * k < end]
            else:
                out = nxt
            succ.append(out)

        def keeps_saved(i, w):
            """a call whose callee restores f14-f31: not a register save/restore helper or a hook"""
            a = start + 4 * i
            if (w >> 26) == 19:
                return True  # indirect: the calling convention
            if a in self.p.import_calls or a in self.p.undef_calls:
                return True
            t = (sext(w & 0x03FFFFFC, 26) + (0 if w & 2 else a)) & 0xFFFFFFFF if (w >> 26) == 18 else \
                (sext(w & 0xFFFC, 16) + (0 if w & 2 else a)) & 0xFFFFFFFF
            return t not in self.saved_clobbers and t not in self.hooks

        TOP = (1 << 64) - 1
        state = [TOP] * n
        state[0] = 0
        sites = {(s - start) // 4 for s in self.sites if start <= s < end}
        for i in sites:
            state[i] = 0
        reached = [False] * n
        reached[0] = True
        work = [0]
        while work:
            i = work.pop()
            w = words[i]
            out = ppc2c.fp_transfer(state[i], w, keeps_saved(i, w))
            for j in succ[i]:
                new = 0 if j in sites else state[j] & out
                if not reached[j] or new != state[j]:
                    reached[j] = True
                    state[j] = new
                    work.append(j)
        return [state[i] if reached[i] else 0 for i in range(n)]

    def emit_function(self, start):
        self.cur_start, self.cur_end = start, self.func_end(start)
        self.labels = set()
        body = []
        if not ppc2c.SINGLE:
            single_in = None
        elif self.singleflow:
            single_in = self.singleflow.states(start)
        else:
            single_in = self.single_dataflow(start, self.cur_end)
        for i, a in enumerate(range(start, self.cur_end, 4)):
            w = self.p.word(a)
            ppc2c.fp_state = single_in[i] if single_in else 0
            ppc2c.cur_addr = a
            try:
                s = translate(a, w, self)
            except Unhandled as e:
                self.unhandled[str(e)] += 1
                s = "ppc_unimplemented(c, 0x%08Xu, 0x%08Xu);" % (a, w)
            body.append((a, w, s))
        hooked = start in self.hooks
        plain = hooked or any(a in self.sites for a, _, _ in body)
        if CRLIVE and not plain:
            live_after, fields = crlive.analyze(body, self.cur_start, self.cur_end, self.p.jump_tables, branch_target,
                                                self.sites)
            body = [(a, w, crlive.rewrite(s, fields[i], live_after[i], CR_CHECK, a)) for i, (a, w, s) in enumerate(body)]
            self.cr_stats[0] += sum(1 for f in fields if f is not None)
            self.cr_stats[1] += sum(1 for i, f in enumerate(fields) if f is not None and (live_after[i] >> (4 * f)) & 0xF == 0)
        elif CRLIVE and CR_CHECK:
            body = [(a, w, crlive.rewrite(s, None, 0, True, a)) for a, w, s in body]
        canon = self.sym(start)
        fname = "f_%08X_orig" % canon if hooked else "f_%08X" % canon
        out = []
        label = self.names.get(canon)
        if label:
            out.append("/* %s */" % label)
        if hooked:
            out.append("void f_%08X(Cpu* __restrict c) { hook_%08X(c); }\n" % (canon, canon))
        out += ["void %s(Cpu* __restrict c) {" % fname, "    PPC_ENTER(0x%08Xu);" % start]
        interior = self.interior_dispatch.get(start, [])
        if interior:
            icases = []
            for slot, label in sorted(interior):
                if self.cur_start <= label < self.cur_end:
                    icases.append("case 0x%08Xu: goto L_%08X;" % (slot, label))
            if icases:
                out.append("    if (c->pc != 0x%08Xu) { switch (c->pc) { %s default: break; } }" % (
                    start, " ".join(icases)))
        tail_wb = ""
        stmts = [s for _, _, s in body]
        if LEAF and not plain and leaflocal.eligible(stmts):
            prologue, stmts, tail_wb = leaflocal.transform(stmts)
            body = [(a, w, s) for (a, w, _), s in zip(body, stmts)]
            out += ["    %s" % p for p in prologue]
            self.leaf_count += 1
        elif NONLEAF and not plain:
            done = leaflocal.transform_nonleaf(stmts, [a for a, _, _ in body])
            if done:
                prologue, stmts, tail_wb = done
                body = [(a, w, s) for (a, w, _), s in zip(body, stmts)]
                out += ["    %s" % p for p in prologue]
                self.nonleaf_count += 1
        for a, w, s in body:
            if a in self.labels:
                out.append("L_%08X: ;" % a)
            if a in self.sites:
                out.append("    site_%08X(c);" % self.sym(a))
            out.append("    %s /* %08X: %08X */" % (s, a, w))
        if self.cur_end < self.p.text_hi:
            nxt = self.sym(self.cur_end)
            fname_nxt = "f_%08X_orig" if self.cur_end in self.hooks else "f_%08X"
            out.append("    %sMUSTTAIL return %s(c);%s" % (
                tail_wb + " " if tail_wb else "", fname_nxt % nxt, self.callee(self.cur_end)))
        else:
            out.append("    ppc_unimplemented(c, 0x%08Xu, 0); /* fell off end of text */" % self.cur_end)
        out.append("}")
        return "\n".join(out), len(body)

    def _source_groups(self, per_file):
        files, cur, n = [], [], 0
        for start in self.sorted_entries:
            cur.append(start)
            n += (self.func_end(start) - start) // 4
            if n >= per_file:
                files.append(cur)
                cur, n = [], 0
        if cur:
            files.append(cur)
        return files

    def run(self, outdir, per_file, on_progress=None, jobs=1):
        def progress(done, total, detail=""):
            if on_progress:
                on_progress(done, total, detail)

        self.jobs = max(1, jobs)
        os.makedirs(outdir, exist_ok=True)
        _log("recomp: %s, %d functions, %d hooks, %d sites, %d workers" % (
            self.build.name, len(self.sorted_entries), len(self.hooks), len(self.sites), self.jobs))
        for name, only in self.skipped_hooks:
            _log("recomp: hooks skipped: %s (only for the %s build)" % (name, only))
        progress(0, 1, "loading names")
        self.names = names.load()
        self.unhandled = collections.Counter()
        self.cr_stats = [0, 0]
        self.leaf_count = 0
        self.nonleaf_count = 0
        leaflocal.reset_stats()
        self.used_imports = set()
        self.imm_override = self.imm_override
        if ppc2c.SINGLE:
            progress(0, 1, "finding register helpers")
            t0 = time.time()
            self.saved_clobbers = self.find_saved_clobbers()
            _log("recomp: %d register helpers, %.1f s" % (len(self.saved_clobbers), time.time() - t0))
        else:
            self.saved_clobbers = set()
        self.singleflow = None
        if ppc2c.SINGLE and singleflow.INTERPROC:
            progress(0, 1, "analyzing floating-point use")
            t0 = time.time()
            self.singleflow = singleflow.Solver(self).solve(self.jobs, _log)
            _log("recomp: float analysis finished, %d function analyses, %.1f s" %
                 (self.singleflow.rounds, time.time() - t0))
        self.used_imports = set()
        ppc2c.single_stats[:] = [0, 0]
        leaflocal.reset_stats()
        files = self._source_groups(per_file)
        total_fn = len(self.sorted_entries)
        jobs_arg = [(i, group, os.path.join(outdir, "code_%03d.c" % i)) for i, group in enumerate(files)]
        stats = [None] * len(files)
        done_fns = 0
        logged_fns = 0
        step = max(1, total_fn // 20)
        t_emit = time.time()
        if jobs_arg:
            _log("recomp: translating %d functions into %d files" % (total_fn, len(files)))
            pool, pickle_path = _open_pool(self.jobs, self)
            failed = True
            try:
                for st in pool.imap_unordered(_emit_job, jobs_arg, chunksize=1):
                    stats[st["index"]] = st
                    done_fns += st["funcs"]
                    progress(done_fns, total_fn, "translating functions (%d of %d)" % (done_fns, total_fn))
                    if done_fns - logged_fns >= step or done_fns == total_fn:
                        _log("recomp: translated %d of %d functions, %.1f s" % (
                            done_fns, total_fn, time.time() - t_emit))
                        logged_fns = done_fns
                failed = False
            finally:
                _shutdown_pool(pool, pickle_path, failed)
        self.unhandled = collections.Counter()
        self.cr_stats = [0, 0]
        self.leaf_count = 0
        self.nonleaf_count = 0
        self.used_imports = set()
        single = [0, 0]
        reloads = [0, 0, 0]
        for st in stats:
            self.unhandled += st["unhandled"]
            self.cr_stats[0] += st["cr"][0]
            self.cr_stats[1] += st["cr"][1]
            self.leaf_count += st["leaf"]
            self.nonleaf_count += st["nonleaf"]
            self.used_imports |= st["used"]
            single[0] += st["single"][0]
            single[1] += st["single"][1]
            reloads[0] += st["reloads"][0]
            reloads[1] += st["reloads"][1]
            reloads[2] += st["reloads"][2]
        ppc2c.single_stats[:] = single
        leaflocal.reloads_written, leaflocal.reloads_left_out, leaflocal.reload_calls = reloads
        written = set("code_%03d.c" % i for i in range(len(files)))
        for name in os.listdir(outdir):
            if re.fullmatch(r"code_\d+\.c", name) and name not in written:
                os.remove(os.path.join(outdir, name))
        progress(len(files), len(files), "writing headers")
        _log("recomp: writing headers")
        self.write_headers(outdir)
        self.write_report(outdir, len(files))
        with _Output(os.path.join(outdir, "hook-inputs.txt")) as f:
            f.write(game_builds.hook_inputs_stamp())
        progress(1, 1, "%d source files" % len(files))

    def static_float_gqrs(self):
        written = set()
        for a in range(self.p.text_lo, self.p.text_hi, 4):
            w = self.p.word(a)
            if w >> 26 == 31 and (w >> 1) & 0x3FF == 467:
                spr = ((w >> 16) & 31) | (((w >> 11) & 31) << 5)
                if 912 <= spr <= 919 or 896 <= spr <= 903:
                    written.add((spr - 912) if spr >= 912 else (spr - 896))
        return sum(1 << n for n in (0, 1) if n not in written)

    def write_headers(self, outdir):
        func_slots = sorted(s for s, (lib, name, kind) in self.imports.items() if kind == "f")

        def comment(canon):
            label = self.names.get(canon)
            return " /* %s */" % label if label else ""

        with _Output(os.path.join(outdir, "funcs.h")) as f:
            gqr = "#define PPC_GQR_STATIC_FLOAT 0x%02X\n" % self.static_float_gqrs() if GQR_STATIC else ""
            f.write('#pragma once\n%s%s#include "ppc.h"\n\n' % ("#define PPC_CR_CHECK 1\n" if CR_CHECK else "", gqr))
            for e in self.sorted_entries:
                canon = self.sym(e)
                f.write("void f_%08X(Cpu* __restrict c);%s\n" % (canon, comment(canon)))
            f.write("\n/* hooked functions: hook_X is implemented in the runtime, f_X_orig is the game's code */\n")
            for e in sorted(self.sym(a) for a in self.hooks):
                f.write("void f_%08X_orig(Cpu* __restrict c);%s\nvoid hook_%08X(Cpu* c);\n" % (e, comment(e), e))
            f.write("\n/* instruction-level hooks (\"@ADDR\" in hooks.txt), run before the instruction at ADDR */\n")
            for e in sorted(self.sym(a) for a in self.sites):
                f.write("void site_%08X(Cpu* c);\n" % e)
            f.write("\n/* imported functions */\n")
            for s in func_slots:
                f.write("void %s(Cpu* c);\n" % self.imp_name(s))
        with _Output(os.path.join(outdir, "table.c")) as f:
            f.write('#include "funcs.h"\n#include "recomp_table.h"\n#include "guest_addr.h"\n\n')
            f.write("const RecompEntry g_recomp_funcs[] = {\n")
            for e in self.sorted_entries:
                f.write("    {0x%08Xu, f_%08X},\n" % (e, self.sym(e)))
            for slot in sorted(self.extra_dispatch_addrs):
                f.write("    {0x%08Xu, f_%08X},\n" % (slot, self.sym(self.extra_dispatch_addrs[slot])))
            n_funcs = len(self.sorted_entries) + len(self.extra_dispatch_addrs)
            f.write("};\nconst unsigned g_recomp_func_count = %d;\n\n" % n_funcs)
            f.write("const RecompImport g_recomp_imports[] = {\n")
            for s, (lib, name, kind) in sorted(self.imports.items()):
                fn = self.imp_name(s) if kind == "f" else "0"
                addr = self.data_import_addr.get(s, s)
                f.write('    {0x%08Xu, 0x%08Xu, "%s", "%s", %d, %s},\n' % (s, addr, lib, name, kind == "f", fn))
            f.write("};\nconst unsigned g_recomp_import_count = %d;\n" % len(self.imports))
            f.write("const uint32_t g_recomp_entry_point = 0x%08Xu;\n" % self.p.entry)
            self.write_build_map(f)
        with _Output(os.path.join(outdir, "imports.c")) as f:
            f.write('#include "funcs.h"\n\nvoid hle_unimplemented(Cpu* c, const char* lib, const char* name);\n\n')
            for s in func_slots:
                lib, name, _ = self.imports[s]
                f.write('__attribute__((weak)) void %s(Cpu* c) { hle_unimplemented(c, "%s", "%s"); }\n' % (
                    self.imp_name(s), lib, name))
        with _Output(os.path.join(outdir, "imports.json")) as f:
            json.dump([{"slot": s, "lib": l, "name": n, "kind": k} for s, (l, n, k) in sorted(self.imports.items())], f, indent=1)

    def write_build_map(self, f):
        """The address map of this build, for the runtime (runtime/include/guest_addr.h)."""
        b = self.build
        f.write('\n/* %s build: canonical (USA) address -> this build\'s (recomp/builds.py) */\n' % b.name)
        f.write('const char g_guest_build_name[] = "%s";\n' % b.name)
        f.write('const char g_guest_build_label[] = "%s";\n' % b.issue_label())
        f.write('const char g_guest_build_title_id[] = "%s";\n' % b.title_id)
        f.write('const int g_guest_build_title_version = %d;\n' % b.title_version)
        for kind, steps in (("code", b.code_steps()), ("data", b.data_steps())):
            f.write("const GuestStep g_guest_%s_steps[] = {%s};\n" % (
                kind, ", ".join("{0x%08Xu, %d}" % (a, d) for a, d in steps)))
            f.write("const unsigned g_guest_%s_step_count = %d;\n" % (kind, len(steps)))

        for kind in ("code", "data"):
            lo, hi = b.bounds.get(kind, (0, 0))
            f.write("const uint32_t g_guest_%s_lo = 0x%08Xu, g_guest_%s_hi = 0x%08Xu;\n" % (kind, lo, kind, hi))
        changed = ", ".join("{0x%08Xu, 0x%08Xu}" % (a, a + size) for a, size, _, _ in b.differing)
        f.write("const GuestChanged g_guest_changed_code[] = {%s};\n" % (changed or "{0u, 0u}"))
        f.write("const unsigned g_guest_changed_code_count = %d;\n" % len(b.differing))

    def write_report(self, outdir, nfiles):
        with _Output(os.path.join(outdir, "report.txt")) as f:
            f.write("build: %s (title %s)\n" % (self.build.name, self.build.title_id))
            for name, only in self.skipped_hooks:
                f.write("hooks skipped: %s (only for the %s build)\n" % (name, only))
            for where, canon, addr in self.hooks_in_changed:
                f.write("hook on a function this build compiled differently: %08X (here %08X, %s)\n" % (
                    canon, addr, where))
            for where, canon in self.hooks_missing:
                f.write("hook skipped, function not in this build: %08X (%s)\n" % (canon, where))
            f.write("functions: %d\nfiles: %d\nfixpoint rounds: %d\n" % (len(self.sorted_entries), nfiles, self.fixpoint_rounds))
            f.write("functions with a name from nsmbu/headers: %d of %d\n" % (
                sum(1 for e in self.sorted_entries if self.names.get(self.sym(e))), len(self.sorted_entries)))
            f.write("imports used: %d of %d\n" % (len(self.used_imports), len(self.imports)))
            f.write("CR writers: %d, %d with no live bit (liveness %s%s)\n" % (
                self.cr_stats[0], self.cr_stats[1], "on" if CRLIVE else "off", ", check build" if CR_CHECK else ""))
            f.write("leaf functions with registers in locals: %d\n" % self.leaf_count)
            f.write("functions with calls with registers in locals: %d\n" % self.nonleaf_count)
            f.write("nonleaf call reloads: %d written, %d left out, %d calls (live-only %s)\n" % (
                leaflocal.reloads_written, leaflocal.reloads_left_out, leaflocal.reload_calls,
                "on" if ppc2c.pass_on("NSMBU_RECOMP_NONLEAF_LIVE") else "off"))
            f.write("functions that change callee-saved registers (save/restore helpers): %d\n" % len(self.saved_clobbers))
            f.write("single-precision multiplier operands: %d rounded (round25), %d known single\n" % tuple(ppc2c.single_stats))
            f.write("unhandled instruction kinds:\n")
            for k, v in self.unhandled.most_common():
                f.write("  %6d  %s\n" % (v, k))
        print(open(os.path.join(outdir, "report.txt")).read())

if __name__ == "__main__":
    ap = argparse.ArgumentParser(usage=__doc__.strip().splitlines()[2].replace("usage: ", ""))
    ap.add_argument("rpx")
    ap.add_argument("outdir")
    ap.add_argument("--insns-per-file", type=int, default=30000)
    ap.add_argument("--jobs", type=int, default=None,
                    help="worker processes (default: CPU count; 0 = CPU count; 1 = one process)")
    ap.add_argument("--progress", action="store_true",
                    help="print 'progress DONE TOTAL detail' lines for the installer")
    ap.add_argument("--build", help="which build the rpx is, when it should not be identified by "
                                    "its SHA-256 (%s)" % ", ".join(b.name for b in game_builds.all_builds()))
    a = ap.parse_args()
    build = None
    if a.build:
        build = game_builds.by_name(a.build)
        if build is None:
            sys.exit("unknown build %r; known: %s" % (a.build, ", ".join(b.name for b in game_builds.all_builds())))

    def on_progress(done, total, detail=""):
        if a.progress:
            print("progress %d %d %s" % (done, total, detail), flush=True)

    if a.progress:
        print("progress 0 1 reading game code", flush=True)
    Recompiler(a.rpx, build).run(a.outdir, a.insns_per_file, on_progress=on_progress, jobs=_job_count(a.jobs))

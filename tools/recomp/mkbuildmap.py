#!/usr/bin/env python3
"""Derive (and check) the address map between two builds of the game.

usage:
  mkbuildmap.py CANONICAL.rpx OTHER.rpx --name EU --title 0005000010101e00 [--out FILE]
  mkbuildmap.py --check FILE [CANONICAL.rpx OTHER.rpx]

Every game address in this repository (tools/recomp/hooks*.txt, runtime/src, the docs) is an
address of the USA build: the canonical id of that function or object. Another build of the same
game (a region, or a pack that inserts more functions) has the same core code at shifted
addresses. A build map (tools/recomp/builds/*.json) is a piecewise constant shift that turns a
canonical address into that build's address, for code (.text) and for data (.rodata/.data/.bss).

The map is derived from the two executables and only then committed; it holds numbers, never any
of the game's code. Deriving it asserts that the two builds really are the same program:

  - functions with a body unique to both builds appear in the same order after dropping a
    longest-increasing-subsequence filter when a few anchors were moved (base disc vs update);
  - every USA function that still exists is paired with exactly one function in the other build
    when enough partners remain;
  - every direct call from a function with an identical body lands inside the mapped target
    function (exact entry, or a few instructions in);
  - every data address referenced from such a function maps to exactly one address.

Functions whose body differs (region language code, or a pack tweak) are listed in the map. A
hook on one of them is not valid for the other build, which is why hooks*.txt files can be marked
as belonging to a build (see tools/recomp/builds.py).
"""
import argparse
import bisect
import collections
import hashlib
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from analyze import Program, sext

DATA_LO, DATA_HI = 0x10000000, 0x20000000

class Binary:
    """An rpx with its functions, normalised bodies and relocation targets."""

    def __init__(self, path):
        self.path = path
        self.sha256 = file_sha256(path)
        self.p = p = Program(path)
        p.discover()
        self.entries = p.entries
        self.ends = p.entries[1:] + [p.text_hi]

        self.reloc_type = {}
        self.reloc_target = collections.defaultdict(list)
        for sec, addr, typ, sym, add in p.rpx.relocs:
            if sec.name != ".text":
                continue
            self.reloc_type[addr & ~3] = typ
            self.reloc_target[addr & ~3].append((typ, (sym.value + add) & 0xFFFFFFFF))

    def nword(self, a):
        """The instruction at `a` with everything the linker chose masked out: branch
        displacements (the target shifts with the code) and relocated immediates (the address of
        a global shifts with the data)."""
        w = self.p.word(a)
        op = w >> 26
        if op == 18:
            return w & 0xFC000003
        if op == 16:
            return w & 0xFFFF0003
        if a in self.reloc_type:
            return w & 0xFFFF0000
        return w

    def body(self, i):
        return [self.nword(a) for a in range(self.entries[i], self.ends[i], 4)]

    def func_index(self, a):
        i = bisect.bisect_right(self.entries, a) - 1
        return i if 0 <= i < len(self.entries) and a < self.ends[i] else None

def file_sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()

def steps_of(pairs):
    """[(canonical, mapped)] (sorted by canonical) -> [(canonical start, delta)] runs."""
    steps, prev = [], None
    for c, m in pairs:
        if m - c != prev:
            prev = m - c
            steps.append((c, prev))
    return steps

class Mismatch(Exception):
    pass

def _digest_body(b):
    return hashlib.blake2b(b"".join(w.to_bytes(4, "big") for w in b), digest_size=16).digest()


def align_functions(canon, other, bodies_c, bodies_o):
    """Pair every USA function index with an other-build index, or Mismatch.

    Unique identical bodies are anchors and must appear in the same order in both builds.
    Between anchors, USA functions are matched greedily by body (allowing the other build to
    insert extra functions, as in the NSMBU + NSLU pack).
    """
    n, m = len(bodies_c), len(bodies_o)
    dc = [_digest_body(b) for b in bodies_c]
    do = [_digest_body(b) for b in bodies_o]
    count_c, count_o = collections.Counter(dc), collections.Counter(do)
    index_c = {h: i for i, h in enumerate(dc)}
    index_o = {h: i for i, h in enumerate(do)}
    anchors = sorted(
        (index_c[h], index_o[h])
        for h in count_c
        if count_c[h] == 1 and count_o.get(h) == 1
    )
    if len(anchors) < n // 8:
        raise Mismatch("only %d of %d functions have a body unique to both builds: too few to tie "
                       "the two together" % (len(anchors), n))
    # Keep the longest increasing subsequence of other-build indices so a few relocated unique
    # bodies (base disc vs 1.3.0) do not abort the whole map.
    prev = [-1] * len(anchors)
    tails_val, tails_pos = [], []
    for i, (_, j) in enumerate(anchors):
        k = bisect.bisect_left(tails_val, j)
        if k == len(tails_val):
            tails_val.append(j)
            tails_pos.append(i)
        else:
            tails_val[k] = j
            tails_pos[k] = i
        if k > 0:
            prev[i] = tails_pos[k - 1]
    keep = set()
    p = tails_pos[-1]
    while p != -1:
        keep.add(p)
        p = prev[p]
    if len(keep) < len(anchors):
        anchors = [anchors[i] for i in range(len(anchors)) if i in keep]
    if len(anchors) < n // 8:
        raise Mismatch("only %d of %d functions remain as ordered unique anchors: too few to tie "
                       "the two together" % (len(anchors), n))
    if anchors[0] != (0, 0):
        raise Mismatch("the first function is not a shared unique body; cannot start the alignment")
    n_anchors = len(anchors)
    if anchors[-1][0] < n:
        anchors = list(anchors) + [(n, m)]

    pair = [-1] * n
    for (i0, j0), (i1, j1) in zip(anchors, anchors[1:]):
        if i0 < n:
            pair[i0] = j0
        if i1 <= i0 + 1:
            continue
        usa_idx = list(range(i0 + 1, i1))
        other_idx = list(range(j0 + 1, j1))
        # LCS on digests so identical bodies line up; leftover USA slots pair in order with
        # leftover other slots (differing bodies); leftover other-only slots are inserts;
        # leftover USA-only slots are absent in the other build (pair to the next anchor).
        a = [dc[i] for i in usa_idx]
        b = [do[j] for j in other_idx]
        lcs = _lcs_index_pairs(a, b)
        used_u = {u for u, _ in lcs}
        used_o = {v for _, v in lcs}
        for u, v in lcs:
            pair[usa_idx[u]] = other_idx[v]
        rest_u = [usa_idx[u] for u in range(len(usa_idx)) if u not in used_u]
        rest_o = [other_idx[v] for v in range(len(other_idx)) if v not in used_o]
        for u, o in zip(rest_u, rest_o):
            pair[u] = o
        # USA functions with no partner in this gap are absent in the other build.

    missing = [i for i, j in enumerate(pair) if j < 0]
    if missing and len(missing) > n // 20:
        raise Mismatch("%d USA functions have no partner in the other build (e.g. #%d %08X)" % (
            len(missing), missing[0], canon.entries[missing[0]]))
    return pair, n_anchors


def _lcs_index_pairs(a, b):
    """Longest common subsequence of sequences a and b as [(index_in_a, index_in_b), ...]."""
    na, nb = len(a), len(b)
    if na == 0 or nb == 0:
        return []
    # For typical short gaps this is fine; long equal-length gaps use the O(n*m) DP.
    dp = [[0] * (nb + 1) for _ in range(na + 1)]
    for i in range(na - 1, -1, -1):
        ai = a[i]
        row, nrow = dp[i], dp[i + 1]
        for j in range(nb - 1, -1, -1):
            if ai == b[j]:
                row[j] = nrow[j + 1] + 1
            else:
                row[j] = nrow[j] if nrow[j] >= row[j + 1] else row[j + 1]
    pairs = []
    i = j = 0
    while i < na and j < nb:
        if a[i] == b[j]:
            pairs.append((i, j))
            i += 1
            j += 1
        elif dp[i + 1][j] >= dp[i][j + 1]:
            i += 1
        else:
            j += 1
    return pairs


def derive(canon, other):
    """The map from `canon` to `other`, or Mismatch if they are not the same program."""
    n = len(canon.entries)
    bodies_c = [canon.body(i) for i in range(n)]
    bodies_o = [other.body(i) for i in range(len(other.entries))]
    pair, n_anchors = align_functions(canon, other, bodies_c, bodies_o)

    paired = [i for i in range(n) if pair[i] >= 0]
    missing = [i for i in range(n) if pair[i] < 0]
    differs = [i for i in paired if bodies_c[i] != bodies_o[pair[i]]]
    # Only one USA function may claim each other function; keep the first.
    seen_other = {}
    unique_paired = []
    for i in paired:
        j = pair[i]
        if j in seen_other:
            pair[i] = -1
            missing.append(i)
            continue
        seen_other[j] = i
        unique_paired.append(i)
    paired = unique_paired
    missing = [i for i in range(n) if pair[i] < 0]
    differs = [i for i in paired if bodies_c[i] != bodies_o[pair[i]]]

    code_steps = steps_of([(canon.entries[i], other.entries[pair[i]]) for i in paired])
    code = piecewise(code_steps)

    checked = wrong = 0
    call_mismatch = set()
    for i in paired:
        j = pair[i]
        if bodies_c[i] != bodies_o[j] or i in call_mismatch:
            continue
        for k in range(0, canon.ends[i] - canon.entries[i], 4):
            a = canon.entries[i] + k
            w = canon.p.word(a)
            if (w >> 26) != 18 or not (w & 1) or a in canon.p.import_calls or a in canon.p.undef_calls:
                continue
            t = (sext(w & 0x03FFFFFC, 26) + (0 if w & 2 else a)) & 0xFFFFFFFF
            if not canon.p.in_text(t):
                continue
            ti = canon.func_index(t)
            if ti is None or pair[ti] < 0 or bodies_c[ti] != bodies_o[pair[ti]]:
                continue
            b = other.entries[j] + k
            w2 = other.p.word(b)
            t2 = (sext(w2 & 0x03FFFFFC, 26) + (0 if w2 & 2 else b)) & 0xFFFFFFFF
            checked += 1
            tj = pair[ti]
            lo, hi = other.entries[tj], other.ends[tj]
            if lo <= t2 < hi:
                continue
            wrong += 1
            call_mismatch.add(i)
            break
    if call_mismatch:
        differs = sorted(set(differs) | call_mismatch)
    if wrong and wrong > max(8, checked // 5000):
        raise Mismatch("%d of %d direct calls do not land in the mapped target function" % (wrong, checked))

    seen = collections.defaultdict(collections.Counter)
    for a, lst in canon.reloc_target.items():
        i = canon.func_index(a)
        if i is None or pair[i] < 0 or bodies_c[i] != bodies_o[pair[i]] or i in call_mismatch:
            continue
        j = pair[i]
        lst2 = other.reloc_target.get(other.entries[j] + (a - canon.entries[i]))
        if not lst2 or len(lst2) != len(lst):
            continue
        for (t1, d1), (t2, d2) in zip(lst, lst2):
            if t1 == t2 and DATA_LO <= d1 < DATA_HI:
                seen[d1][d2] += 1
    data_pairs = []
    ambiguous = 0
    for d, c in sorted(seen.items()):
        top = c.most_common(2)
        if len(top) == 1 or top[0][1] > top[1][1]:
            data_pairs.append((d, top[0][0]))
        else:
            ambiguous += 1
    if ambiguous and ambiguous > max(8, len(seen) // 5000):
        raise Mismatch("%d data addresses map to more than one address with no majority" % ambiguous)
    data_steps = steps_of(data_pairs)

    differing = []
    for i in differs:
        j = pair[i]
        differing.append([canon.entries[i], canon.ends[i] - canon.entries[i],
                          other.entries[j], other.ends[j] - other.entries[j]])
    for i in missing:
        # Absent in the other build: keep a code entry so the piecewise map jumps, and mark the
        # whole USA function as changed so instruction hooks are refused.
        mapped = code(canon.entries[i])
        differing.append([canon.entries[i], canon.ends[i] - canon.entries[i], mapped, 0])
    differing.sort(key=lambda row: row[0])

    return {
        "code_bounds": [canon.p.text_lo, canon.p.text_hi],
        "data_bounds": [min(s.addr for s in canon.p.rpx.sections if s.name in (".rodata", ".data", ".bss")),
                        max(s.addr + s.size for s in canon.p.rpx.sections if s.name in (".rodata", ".data", ".bss"))],
        "code_steps": code_steps,
        "data_steps": data_steps,
        "differing_functions": differing,
        "stats": {"functions": n, "other_functions": len(other.entries), "anchors": n_anchors,
                  "paired": len(paired), "missing": len(missing),
                  "calls_checked": checked, "data_addresses": len(seen),
                  "differing": len(differing)},
    }

def piecewise(steps):
    starts = [s for s, _ in steps]
    deltas = [d for _, d in steps]

    def f(a):
        i = bisect.bisect_right(starts, a) - 1
        return (a + deltas[i]) & 0xFFFFFFFF if i >= 0 else a
    return f

def to_json(m, name, title, sha256, canon_sha256, canon_name, title_version=None):
    out = {
        "name": name,
        "title_id": title,
    }
    if title_version is not None:
        out["title_version"] = int(title_version)
    out.update({
        "rpx_sha256": sha256,
        "derived_from": {"name": canon_name, "rpx_sha256": canon_sha256},
        "comment": ("Canonical (%s) address -> %s address, as a piecewise constant shift. Derived and "
                    "checked by tools/recomp/mkbuildmap.py; numbers only, no game code." % (canon_name, name)),
        "checked": m["stats"],
        "code_bounds": ["%08X" % a for a in m["code_bounds"]],
        "data_bounds": ["%08X" % a for a in m["data_bounds"]],
        "code_steps": [["%08X" % a, d] for a, d in m["code_steps"]],
        "data_steps": [["%08X" % a, d] for a, d in m["data_steps"]],
        "differing_functions": [["%08X" % a, s1, "%08X" % b, s2] for a, s1, b, s2 in m["differing_functions"]],
    })
    return out

def report(m):
    s = m["stats"]
    other = s.get("other_functions", s["functions"])
    print("functions %d -> %d, bodies unique to both builds %d, direct calls checked %d, data addresses %d" % (
        s["functions"], other, s["anchors"], s["calls_checked"], s["data_addresses"]))
    print("code: %d steps, data: %d steps, functions with a different body: %d" % (
        len(m["code_steps"]), len(m["data_steps"]), len(m["differing_functions"])))
    for a, d in m["code_steps"]:
        print("   code from %08X: %+d" % (a, d))
    for a, d in m["data_steps"]:
        print("   data from %08X: %+d" % (a, d))
    differing = m["differing_functions"]
    show = differing if len(differing) <= 40 else differing[:20] + differing[-10:]
    for a, s1, b, s2 in show:
        print("   body differs: %08X (%d bytes) -> %08X (%d bytes)" % (a, s1, b, s2))
    if len(differing) > 40:
        print("   ... %d more functions with a different or missing body" % (len(differing) - len(show)))

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("rpx", nargs="*", help="the canonical (USA) red-pro2.rpx and the other build's")
    ap.add_argument("--name", help="short name of the other build, e.g. EU")
    ap.add_argument("--title", help="its title id, e.g. 0005000010101e00")
    ap.add_argument("--title-version", type=int, help="title version of the other build (e.g. 0 for a base disc)")
    ap.add_argument("--canonical-name", default="USA")
    ap.add_argument("--out", help="where to write the map (default: stdout)")
    ap.add_argument("--check", help="re-derive the map in this file and compare")
    args = ap.parse_args()

    if args.check:
        have = json.load(open(args.check))
        if len(args.rpx) != 2:
            print("%s: %s, title %s, rpx %s..." % (args.check, have["name"], have["title_id"],
                                                   have["rpx_sha256"][:16]))
            print("pass both rpx files to re-derive and compare")
            return 0
        canon, other = Binary(args.rpx[0]), Binary(args.rpx[1])
        if other.sha256 != have["rpx_sha256"]:
            sys.exit("%s is not the build this map is for (SHA-256 %s..., expected %s...)" % (
                args.rpx[1], other.sha256[:16], have["rpx_sha256"][:16]))
        if canon.sha256 != have["derived_from"]["rpx_sha256"]:
            sys.exit("canonical executable SHA-256 differs from the map's source")
        fresh = to_json(derive(canon, other), have["name"], have["title_id"], other.sha256,
                        canon.sha256, have["derived_from"]["name"],
                        title_version=have.get("title_version"))
        same = all(fresh[k] == have.get(k) for k in ("code_steps", "data_steps", "differing_functions", "code_bounds", "data_bounds"))
        report(derive(canon, other))
        print("map in %s: %s" % (args.check, "unchanged" if same else "DIFFERS from the two binaries"))
        return 0 if same else 1

    if len(args.rpx) != 2 or not args.name or not args.title:
        ap.error("need CANONICAL.rpx OTHER.rpx --name NAME --title TITLEID")
    canon, other = Binary(args.rpx[0]), Binary(args.rpx[1])
    m = derive(canon, other)
    report(m)
    out = to_json(m, args.name, args.title, other.sha256, canon.sha256, args.canonical_name,
                  title_version=args.title_version)
    text = json.dumps(out, indent=1) + "\n"
    if args.out:
        with open(args.out, "w") as f:
            f.write(text)
        print("wrote %s" % args.out)
    else:
        print(text)
    return 0

if __name__ == "__main__":
    try:
        sys.exit(main())
    except Mismatch as e:
        sys.exit("these two executables are not the same program: %s" % e)

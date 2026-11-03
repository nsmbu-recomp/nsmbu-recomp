"""The builds of the game this port knows, and how their addresses relate to the canonical ones.

Every game address written in this repository is an address of the **USA** build: the canonical id
of that function or that global. `recomp/builds/*.json` holds, for every other build, the
piecewise constant shift from a canonical address to that build's address, derived and checked from
the two executables by `recomp/mkbuildmap.py` (numbers only, never any of the game's code).

A hooks file (`hooks*.txt`) may start with

    # builds: USA

to say that its hooks belong to one build only. That is for hooks that patch code which another
build already has, like the European language code the USA build lacks (`hooks_language.txt`).
Everything else is checked automatically: a hook or site inside a function whose body differs
between the builds is refused, because the code it patches is not the same code there. A wrap of
the whole function is allowed and reported, unless the line says `same`: the replacement was
checked and applies to that function in every build. A hook on a function this build does not
have is skipped. Its mapped address sits inside a different function.
"""
import bisect
import glob
import hashlib
import json
import os
import re

HERE = os.path.dirname(os.path.abspath(__file__))
CANONICAL = "USA"

class Build:
    def __init__(self, d):
        self.name = d["name"]
        self.title_id = d["title_id"]
        self.sha256 = d["rpx_sha256"]
        self.title_version = int(d.get("title_version", 64))
        self.bounds = {kind: tuple(int(a, 16) for a in d[kind + "_bounds"])
                       for kind in ("code", "data") if kind + "_bounds" in d}
        if self.name != CANONICAL and len(self.bounds) != 2:
            raise ValueError("noncanonical maps need verified code/data bounds")
        for lo, hi in self.bounds.values():
            if not 0 <= lo < hi <= 0xFFFFFFFF:
                raise ValueError("invalid address bounds")
        self._code = _Shift(d.get("code_steps", []))
        self._data = _Shift(d.get("data_steps", []))
        self.differing = sorted(
            ((int(a, 16), int(s1), int(b, 16), int(s2))
             for a, s1, b, s2 in d.get("differing_functions", [])),
            key=lambda row: row[0],
        )
        self._differ_starts = [a for a, _, _, _ in self.differing]
        self._differ_ends = [a + s for a, s, _, _ in self.differing]

    @property
    def canonical(self):
        return self.name == CANONICAL

    def code(self, canon):
        """Canonical (USA) code address -> this build's address."""
        self.require_address("code", canon)
        if self.body_differs(canon) and canon not in self._differ_starts:
            raise ValueError("%08X is inside changed code; no instruction mapping" % canon)
        return self._code.apply(canon)

    def data(self, canon):
        """Canonical (USA) data address -> this build's address."""
        self.require_address("data", canon)
        return self._data.apply(canon)

    def require_address(self, kind, address):
        if self.canonical:
            return
        lo, hi = self.bounds[kind]
        if not lo <= address < hi:
            raise ValueError("%08X is outside verified %s bounds" % (address, kind))

    def canon_code(self, addr):
        """This build's code address -> the canonical one (the inverse of code()).

        Exact for every address the build really has. The other direction is not exact inside a
        function this build compiled shorter: the canonical bytes past its end have no address here,
        and code() would put them where the next function already is. body_differs() tells those
        apart; a function's entry always maps both ways."""
        return self._code.unapply(addr)

    def code_steps(self):
        """[(canonical start, shift)] of the code map, sorted; [(0, 0)] for the canonical build."""
        return list(zip(self._code.starts, self._code.deltas)) or [(0, 0)]

    def data_steps(self):
        """[(canonical start, shift)] of the data map, sorted; [(0, 0)] for the canonical build."""
        return list(zip(self._data.starts, self._data.deltas)) or [(0, 0)]

    def body_differs(self, canon):
        """Is `canon` inside a function this build compiled differently? Then code written against
        the canonical body (an instruction-level hook, an offset into it) does not apply here."""
        i = bisect.bisect_right(self._differ_starts, canon) - 1
        return i >= 0 and canon < self._differ_ends[i]

    def function_missing(self, canon):
        """True when `canon` is the entry of a function this build does not have.

        The map still names an address, but that address is inside a different function.
        """
        i = bisect.bisect_left(self._differ_starts, canon)
        if i >= len(self.differing) or self.differing[i][0] != canon:
            return False
        return self.differing[i][3] == 0

    def issue_label(self):
        """Label used in bug reports and boot logs (matches the GitHub issue form)."""
        return {
            "USA": "USA 1.3.0",
            "USA_V0": "USA base disc",
            "EU": "Europe 1.3.0",
            "EU_V0": "Europe base disc",
            "NSMBU_NSLU_USA": "NSMBU + NSLU (USA)",
            "NSMBU_NSLU_EU": "NSMBU + NSLU (Europe)",
        }.get(self.name, self.name)

    def __repr__(self):
        return "<Build %s title %s>" % (self.name, self.title_id)

class _Shift:
    """A piecewise constant shift: [(start, delta), ...] sorted by start."""

    def __init__(self, steps):
        pairs = [(int(a, 16) if isinstance(a, str) else a, int(d)) for a, d in steps]
        if pairs != sorted(pairs) or len({a for a, _ in pairs}) != len(pairs):
            raise ValueError("address steps must be sorted and unique")
        if any(not 0 <= a <= 0xFFFFFFFF or not -0x80000000 <= d <= 0x7FFFFFFF
               or not 0 <= a + d <= 0xFFFFFFFF for a, d in pairs):
            raise ValueError("invalid address step")
        self.starts = [a for a, _ in pairs]
        self.deltas = [d for _, d in pairs]
        self.i_starts = sorted(a + d for a, d in pairs)
        self.i_deltas = [d for _, d in sorted(((a + d, d) for a, d in pairs))]

    def apply(self, a):
        i = bisect.bisect_right(self.starts, a) - 1
        return (a + self.deltas[i]) & 0xFFFFFFFF if i >= 0 else a

    def unapply(self, a):
        i = bisect.bisect_right(self.i_starts, a) - 1
        return (a - self.i_deltas[i]) & 0xFFFFFFFF if i >= 0 else a

def all_builds():
    builds = [canonical_build()]
    for path in sorted(glob.glob(os.path.join(glob.escape(HERE), "builds", "*.json"))):
        with open(path, encoding="utf-8") as source:
            builds.append(Build(json.load(source)))
    return builds

_CANONICAL = {
    "name": CANONICAL,
    "title_id": "0005000010101d00",
    "rpx_sha256": "ebd147ce4cdedb563db3ac6278186ca5dea282ad315271925b8bb5ac16c7dc8e",
    "title_version": 64,
}

def canonical_build():
    return Build(_CANONICAL)

def by_name(name):
    for b in all_builds():
        if b.name.lower() == name.lower():
            return b
    return None

def by_sha256(digest):
    for b in all_builds():
        if b.sha256 == digest:
            return b
    return None

def by_title(title_id):
    """Preferred build for this title id (highest title version, canonical first on ties)."""
    matches = [b for b in all_builds() if b.title_id == (title_id or "").lower()]
    if not matches:
        return None
    return max(matches, key=lambda b: (b.title_version, b.canonical))

def builds_for_title(title_id):
    return [b for b in all_builds() if b.title_id == (title_id or "").lower()]

def by_title_version(title_id, version):
    for b in builds_for_title(title_id):
        if b.title_version == version:
            return b
    return None

def file_sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()

def identify(rpx_path):
    """Which build `rpx_path` is, or None if it is not one the port knows."""
    return by_sha256(file_sha256(rpx_path))

_BUILDS_RE = re.compile(r"^#\s*builds:\s*(.+)$", re.I)

def hook_on_differing(build, site, canon, same, is_entry):
    """Fate of a hook in code this build compiled differently. See differing_hook.

    "skip" also covers a function this build does not have. Wrapping the address the map
    gives would run a different function.
    """
    if build.function_missing(canon):
        return "skip"
    return differing_hook(site, same, is_entry)

def differing_hook(site, same, is_entry):
    """What to do with a hook in a function this build compiled differently.

    "keep" applies it quietly. "warn" applies a whole-function wrap and reports it. "refuse" drops
    an instruction site in changed code or rejects a wrap that is not marked `same`.
    `same` is a wrap whose replacement was checked against the other bodies.
    """
    if same and not site and is_entry:
        return "keep"
    if site or not is_entry:
        return "refuse"
    return "warn"

def read_hooks(paths, build):
    """Read the hooks files that apply to `build`.

    Returns (entries, skipped): entries is a list of (is_site, canonical address, this build's
    address, "file:line", same), skipped names the files that belong to another build. `same` is
    true when the line says the whole-function replacement applies to every build. The caller
    checks that every entry applies to the build (the recompiler does, once it knows every function).
    """
    entries, skipped = [], []
    for path in paths:
        if not os.path.exists(path):
            continue
        with open(path, encoding="utf-8") as source:
            lines = source.readlines()
        for lineno, line in enumerate(lines, 1):
            m = _BUILDS_RE.match(line.strip())
            if m:
                only = [s.strip().lower() for s in re.split(r"[,\s]+", m.group(1)) if s.strip()]
                if build.name.lower() not in only:
                    skipped.append((os.path.basename(path), m.group(1).strip()))
                    break
                continue
            text = line.split("#")[0].strip()
            if not text:
                continue
            where = "%s:%d" % (os.path.basename(path), lineno)
            site, canon, same = _parse_hook(text, where)
            try:
                mapped = build.code(canon)
            except ValueError:
                # Interior of a differently compiled function: keep the piecewise address so the
                # recompiler can skip or error with a useful location.
                mapped = build._code.apply(canon)
            entries.append((site, canon, mapped, where, same))
    return entries, skipped

def _parse_hook(text, where):
    site = text.startswith("@")
    body = text[1:].strip() if site else text
    parts = body.split()
    if len(parts) != 1 and not (len(parts) == 2 and parts[1].lower() == "same"):
        raise ValueError("%s: a hook is an address, optionally followed by 'same'" % where)
    if site and len(parts) == 2:
        raise ValueError("%s: 'same' is for a whole-function hook" % where)
    try:
        canon = int(parts[0], 16)
    except ValueError:
        raise ValueError("%s: %s is not an address" % (where, parts[0]))
    return site, canon, len(parts) == 2

def hook_files(directory=None):
    d = directory or HERE
    return [os.path.join(d, "hooks.txt")] + sorted(glob.glob(os.path.join(glob.escape(d), "hooks_*.txt")))

def hook_inputs_stamp(directory=None):
    lines = []
    for path in hook_files(directory):
        try:
            with open(path, "rb") as handle:
                digest = hashlib.sha256(handle.read()).hexdigest()
        except OSError:
            continue
        lines.append("%s %s" % (digest, os.path.basename(path)))
    return "".join(line + "\n" for line in lines)

"""Recompiler switches.

Flags set these.
The process environment does not.
"""


class Switches:
    def __init__(self):
        self.plain = False
        self.spawn = False
        self.force_on = set()
        self.force_off = set()
        self.checks = set()

    def reset(self):
        self.__init__()

    def pass_enabled(self, name):
        if name in self.force_on:
            return True
        if name in self.force_off:
            return False
        return not self.plain

    def check_enabled(self, name):
        return name in self.checks

    def state(self):
        return {
            "plain": self.plain,
            "spawn": self.spawn,
            "force_on": sorted(self.force_on),
            "force_off": sorted(self.force_off),
            "checks": sorted(self.checks),
        }

    def load_state(self, state):
        self.plain = bool(state["plain"])
        self.spawn = bool(state["spawn"])
        self.force_on = set(state["force_on"])
        self.force_off = set(state["force_off"])
        self.checks = set(state["checks"])


S = Switches()

_PASSES = (
    "crlive",
    "leaf",
    "nonleaf",
    "nonleaf_live",
    "single",
    "single_ip",
    "gqr",
)
_CHECKS = ("cr_check", "leaf_poison", "single_check")


def key_of(var):
    name = var[len("NSMBU_RECOMP_"):] if str(var).startswith("NSMBU_RECOMP_") else str(var)
    return name.lower()


class Live:
    def __init__(self, name, check=False):
        self.name = name
        self.check = check

    def __bool__(self):
        if self.check:
            return S.check_enabled(self.name)
        return S.pass_enabled(self.name)


def pass_on(var):
    return S.pass_enabled(key_of(var))


def apply_args(args):
    S.reset()
    S.plain = bool(getattr(args, "plain", False))
    S.spawn = bool(getattr(args, "spawn", False))
    for name in _PASSES:
        if getattr(args, name, False):
            S.force_on.add(name)
        elif getattr(args, "no_" + name, False):
            S.force_off.add(name)
    for name in _CHECKS:
        if getattr(args, name, False):
            S.checks.add(name)

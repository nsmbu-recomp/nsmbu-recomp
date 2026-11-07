"""Docs may name only NSMBU_* variables declared in runtime/src/core/env.h."""
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
ENV_H = ROOT / "runtime" / "src" / "core" / "env.h"
DOCS = ROOT / "docs"
NAME = re.compile(r"NSMBU_[A-Z0-9_]+")
DECLARED = re.compile(r'ENV\([A-Z0-9_]+,\s*"(NSMBU_[A-Z0-9_]+)"\)')
# Not runtime getenv names. Recompiler passes, build ids, decomp symbols, and the CMake renderer switch.
PREFIX_OK = ("NSMBU_RECOMP_", "NSMBU_NSLU_", "NSMBU_ADDR_")
EXACT_OK = {
    "NSMBU_RENDERER",
    "NSMBU_TEST_JOBS",
    "NSMBU_VERBOSE_BUILD",
    "NSMBU_BIN",
    "NSMBU_DATA_DIR",
    "NSMBU_GAME_SAMPLE",
    "NSMBU_VULKAN_PRESENT_MODE",
    "NSMBU_TEST_TRUST_NATIVE_MODS",
}


def declared_names():
    return set(DECLARED.findall(ENV_H.read_text(encoding="utf-8")))


class EnvNames(unittest.TestCase):
    def test_documented_names_are_declared(self):
        known = declared_names()
        self.assertGreater(len(known), 0)
        unknown = []
        for path in sorted(DOCS.rglob("*.md")):
            text = path.read_text(encoding="utf-8")
            for match in NAME.finditer(text):
                name = match.group(0)
                nxt = text[match.end():match.end() + 1]
                if nxt == "*" or nxt.islower():
                    continue
                if name in EXACT_OK or name.startswith(PREFIX_OK):
                    continue
                if name not in known:
                    line = text.count("\n", 0, match.start()) + 1
                    unknown.append(f"{path.relative_to(ROOT)}:{line}: {name}")
        self.assertEqual(unknown, [])


if __name__ == "__main__":
    unittest.main()

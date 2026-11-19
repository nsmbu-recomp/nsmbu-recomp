"""Docs may name only NSMBU_* settings declared in runtime/src/core/env.h or CMake build ids."""
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
ENV_H = ROOT / "runtime" / "src" / "core" / "env.h"
DOCS = ROOT / "docs"
NAME = re.compile(r"NSMBU_[A-Z0-9_]+")
DECLARED = re.compile(r'ENV\([A-Z0-9_]+,\s*"(NSMBU_[A-Z0-9_]+)"\)')
# Not runtime settings: decomp symbols and the CMake renderer switch.
PREFIX_OK = ("NSMBU_NSLU_", "NSMBU_ADDR_")
EXACT_OK = {
    "NSMBU_RENDERER",
    "NSMBU_HAS_VULKAN",
    "NSMBU_HAS_METAL",
    "NSMBU_SDL_HOST",
    "NSMBU_BUNDLED_DEPS",
    "NSMBU_BUNDLED_ZSTD",
    "NSMBU_SETUP_GUI",
    "NSMBU_VERSION",
    "NSMBU_VK_DECLARE",
    "NSMBU_VK_LOAD",
    "NSMBU_NSLU_USA",
    "NSMBU_NSLU_EU",
}


def declared_names():
    return set(DECLARED.findall(ENV_H.read_text(encoding="utf-8")))


def flag_of(name):
    return "--" + name.removeprefix("NSMBU_").lower().replace("_", "-")


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

    def test_flags_md_lists_every_runtime_flag(self):
        text = (DOCS / "developer" / "flags.md").read_text(encoding="utf-8")
        missing = []
        for name in sorted(declared_names()):
            flag = flag_of(name)
            if not re.search(r"(?<![\w-])" + re.escape(flag) + r"(?![\w-])", text):
                missing.append(flag)
        self.assertEqual(missing, [])


if __name__ == "__main__":
    unittest.main()

#!/usr/bin/env python3
"""Parser checks for the decomp function list: python3 tools/decomp_build/test_gen.py"""
import os
import tempfile
import unittest

import gen


def write_tree(root, files, hooks=""):
    for name, text in files.items():
        path = os.path.join(root, name)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w", encoding="utf-8") as handle:
            handle.write(text)
    os.makedirs(os.path.join(root, "hooks"), exist_ok=True)
    with open(os.path.join(root, "hooks", "hooks.txt"), "w", encoding="utf-8") as handle:
        handle.write(hooks)


class Collect(unittest.TestCase):
    def test_body_is_a_candidate_and_a_declaration_is_not(self):
        with tempfile.TemporaryDirectory() as root:
            write_tree(root, {
                "src/a.cpp": """
// Address: 0x02000010
s32 plain(s32 value)
{
    return value;
}

// Address: 0x02000020
void only_declared(s32 value);
""",
            })
            funcs, skipped = gen.collect(root, os.path.join(root, "hooks"))
            self.assertEqual([f.addr for f in funcs], [0x02000010])
            self.assertIn("static_cast<s32>(c->r[3])", funcs[0].call[0])
            self.assertTrue(any(addr == 0x02000020 for addr, _w, why in skipped) is False)

    def test_port_hook_stays_off_the_list(self):
        with tempfile.TemporaryDirectory() as root:
            write_tree(root, {
                "src/a.cpp": """
// Address: 0x02000030
void hooked()
{
}
""",
            }, hooks="02000030\n")
            funcs, skipped = gen.collect(root, os.path.join(root, "hooks"))
            self.assertEqual(funcs, [])
            self.assertIn("port hook", skipped[0][2])

    def test_method_reads_r3_and_r4(self):
        with tempfile.TemporaryDirectory() as root:
            write_tree(root, {
                "include/a.h": """
class Boot {
public:
    // Address: 0x02000040
    bool go(const Item& item)
    {
        return true;
    }
};
""",
            })
            funcs, _skipped = gen.collect(root, os.path.join(root, "hooks"))
            text = "\n".join(funcs[0].call)
            self.assertIn("reinterpret_cast<Boot*>", text)
            self.assertIn("c->r[4]", text)
            self.assertIn("self->go(", text)


if __name__ == "__main__":
    unittest.main()

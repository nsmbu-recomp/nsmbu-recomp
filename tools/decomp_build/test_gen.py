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
            self.assertIn("self->Boot::go(", text)


class Guest(unittest.TestCase):
    def test_declaration_calls_the_recompiled_function(self):
        text = """
// Address: 0x029F1594
void* sub_029F1594(u32 size);
"""
        out = gen._prepare_source(text, set())
        self.assertIn("decomp::call<void*>(0x029F1594u, size)", out)

    def test_unnamed_parameter_gets_a_name(self):
        text = """
// Address: 0x024EFA44
bool setCharModelSource_(const FFLStoreData* p_store_data, u16);
"""
        out = gen._prepare_source(text, set())
        self.assertIn("u16 a1", out)
        self.assertIn("decomp::call<bool>(0x024EFA44u, p_store_data, a1)", out)

    def test_class_return_stays_a_declaration(self):
        text = """
// Address: 0x024F01E0
sead::Color4f getFavoriteColor() const;
"""
        out = gen._prepare_source(text, set())
        self.assertNotIn("decomp::call<", out)

    def test_defined_address_stays_a_declaration(self):
        text = """
// Address: 0x029F1594
void* sub_029F1594(u32 size);
"""
        out = gen._prepare_source(text, {0x029F1594})
        self.assertNotIn("decomp::call<", out)

    def test_guest_word_and_function_pointer(self):
        text = """
using AllocFn = void* (*)(void*, u32, u32);
void demo(void* p, void* q)
{
    u32 v = *reinterpret_cast<u32*>(p + 4);
    *reinterpret_cast<void**>(p) = q;
    AllocFn alloc = reinterpret_cast<AllocFn>(*reinterpret_cast<u32*>(v + 0x34));
    alloc(p, v, 4u);
}
"""
        out = gen._prepare_source(text, set())
        self.assertIn("decomp::be32(p + 4)", out)
        self.assertIn("decomp::beptr<void*>(p)", out)
        self.assertIn("decomp::make_fn<void*, void*, u32, u32>(decomp::fn_addr(v + 0x34))", out)

    def test_reused_function_alias_keeps_each_signature(self):
        text = """
using Fn = void* (*)(void*, size_t, u32);
void* alloc(void* heap)
{
    Fn fn = reinterpret_cast<Fn>(*reinterpret_cast<u32*>(heap));
    return fn(heap, 4u, 4u);
}
using Fn = void (*)(void*, void*);
void free_one(void* heap, void* memory)
{
    Fn fn = reinterpret_cast<Fn>(*reinterpret_cast<u32*>(heap));
    fn(heap, memory);
}
"""
        out = gen._prepare_source(text, set())
        self.assertIn("decomp::make_fn<void*, void*, size_t, u32>", out)
        self.assertIn("decomp::make_fn<void, void*, void*>", out)

    def test_auto_function_pointer_uses_the_alias(self):
        text = """
using Fn = u32 (*)(void*, u32);
u32 call(void* obj)
{
    auto fn = reinterpret_cast<Fn>(*reinterpret_cast<void**>(obj));
    return fn(obj, 4u);
}
"""
        out = gen._prepare_source(text, set())
        self.assertIn("decomp::make_fn<u32, void*, u32>(decomp::fn_addr(obj))", out)


if __name__ == "__main__":
    unittest.main()

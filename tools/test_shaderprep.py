"""Offline shaderprep tests on synthetic records, archives and templates."""
import io
import os
import struct
import sys
import tempfile
import unittest
import zlib
from unittest.mock import patch

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "shader"))
import shaderprep as prep

VS_WORDS = 52
PS_WORDS = 41


def sharcfb(shaders):
    name = b"synthetic\0\0\0"
    head = b"BAHS" + struct.pack("<IIIII", 9, 0, 1, 0, len(name)) + name
    body = b""
    for typ, regs, code in shaders:
        if typ == prep.VS:
            size_off, ptr_off, total = 0xD0, 0xD4, 0xDC
        else:
            size_off, ptr_off, total = 0xA4, 0xA8, 0xAC
        data = bytearray(total)
        for i, v in enumerate(regs):
            struct.pack_into("<I", data, 4 * i, v)
        struct.pack_into("<I", data, size_off, len(code))
        struct.pack_into("<I", data, ptr_off, total)
        data += code
        body += struct.pack("<IIII", 16 + len(data), typ, 0, len(data)) + bytes(data)
    section = struct.pack("<II", 8 + len(body), len(shaders)) + body
    return head + section


def sarc(files):
    names = b""
    nodes = b""
    data = b""
    for i, (name, content) in enumerate(files):
        raw = name.encode() + b"\0"
        raw += b"\0" * (-len(raw) % 4)
        data += b"\0" * (-len(data) % 4)
        nodes += struct.pack(">IIII", i + 1, 0x01000000 | (len(names) // 4), len(data), len(data) + len(content))
        names += raw
        data += content
    sfat = b"SFAT" + struct.pack(">HHI", 12, len(files), 0x65) + nodes
    sfnt = b"SFNT" + struct.pack(">HH", 8, 0) + names
    doff = 0x14 + len(sfat) + len(sfnt)
    doff += -doff % 4
    pad = b"\0" * (doff - 0x14 - len(sfat) - len(sfnt))
    total = doff + len(data)
    head = b"SARC" + struct.pack(">HHIII", 0x14, 0xFEFF, total, doff, 0)
    return head + sfat + sfnt + pad + data


class Hashing(unittest.TestCase):
    def test_empty_is_basis(self):
        self.assertEqual(prep.prog_hash(b""), 0xCBF29CE484222325)

    def test_length_is_part_of_hash(self):
        self.assertNotEqual(prep.prog_hash(b"\0" * 8), prep.prog_hash(b"\0" * 16))

    def test_tail_padding_is_not_ambiguous(self):
        self.assertNotEqual(prep.prog_hash(b"abc"), prep.prog_hash(b"abc\0"))

    def test_stable_and_64_bit(self):
        a = prep.prog_hash(bytes(range(40)))
        self.assertEqual(a, prep.prog_hash(bytes(range(40))))
        self.assertLess(a, 1 << 64)


class Records(unittest.TestCase):
    def test_shader_record_roundtrip(self):
        code, fs = b"\x01" * 24, b"\x02" * 16
        regs = [(0xA214, 7), (0xA21A, 9)]
        raw = prep.shader_record(1, code, fs, regs)
        vertex, c, f, r = prep.parse_shader_record(raw)
        self.assertEqual((vertex, c, f, r), (1, code, fs, regs))

    def test_record_stream_roundtrip_and_truncation(self):
        buf = io.BytesIO()
        prep.write_record(buf, prep.REC_SHADER, b"first")
        prep.write_record(buf, prep.REC_PIPELINE, b"second" * 50)
        data = buf.getvalue()
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "cache.bin")
            with open(path, "wb") as f:
                f.write(data)
            self.assertEqual(list(prep.read_records(path)),
                             [(prep.REC_SHADER, b"first"), (prep.REC_PIPELINE, b"second" * 50)])
            with open(path, "wb") as f:
                f.write(data[:-3])
            self.assertEqual(list(prep.read_records(path)), [(prep.REC_SHADER, b"first")])


class Archives(unittest.TestCase):
    def test_sharcfb_extracts_vertex_and_pixel(self):
        vs_regs = list(range(100, 100 + VS_WORDS))
        ps_regs = list(range(200, 200 + PS_WORDS))
        blob = sharcfb([(prep.VS, vs_regs, b"vsvsvsvs"), (prep.PS, ps_regs, b"pspspsps")])
        self.assertEqual(list(prep.sharcfb_shaders(blob)),
                         [(prep.VS, vs_regs, b"vsvsvsvs"), (prep.PS, ps_regs, b"pspspsps")])

    def test_sharcfb_skips_bad_pointer_and_foreign_magic(self):
        blob = bytearray(sharcfb([(prep.PS, [0] * PS_WORDS, b"codecode")]))
        self.assertEqual(list(prep.sharcfb_shaders(b"SHAB" + bytes(blob[4:]))), [])
        size_at = blob.index(b"codecode") - 0xAC + 0xA4
        struct.pack_into("<I", blob, size_at, 1 << 20)
        self.assertEqual(list(prep.sharcfb_shaders(bytes(blob))), [])

    def test_sarc_names_and_nested_walk(self):
        inner = sharcfb([(prep.PS, [0] * PS_WORDS, b"12345678")])
        outer = sarc([("a.txt", b"hello"), ("inner.sarc", sarc([("m.sharcfb", inner)]))])
        self.assertEqual(dict(prep.sarc(outer))["a.txt"], b"hello")
        walked = dict(prep.walk(outer, "root"))
        self.assertEqual(walked["root/a.txt"], b"hello")
        self.assertEqual(walked["root/inner.sarc/m.sharcfb"], inner)

    def test_game_shaders_dedups_by_type_and_code(self):
        regs = [0] * PS_WORDS
        blob = sharcfb([(prep.PS, regs, b"samesame"), (prep.PS, regs, b"samesame"), (prep.VS, [0] * VS_WORDS, b"samesame")])
        with tempfile.TemporaryDirectory() as d:
            with open(os.path.join(d, "a.sharcfb"), "wb") as f:
                f.write(blob)
            with open(os.path.join(d, "ignored.bin"), "wb") as f:
                f.write(blob)
            found = prep.game_shaders(d)
        self.assertEqual(set(found), {(prep.PS, b"samesame"), (prep.VS, b"samesame")})
        self.assertEqual(found[(prep.PS, b"samesame")][1], "a.sharcfb")


class OwnBlock(unittest.TestCase):
    def test_vertex_block(self):
        w = [0] * VS_WORDS
        w[0x00 // 4], w[0x04 // 4], w[0x08 // 4], w[0x38 // 4] = 1, 2, 3, 4
        w[0x0C // 4] = 2
        w[0x10 // 4], w[0x10 // 4 + 1] = 10, 11
        w[0x40 // 4] = 1
        w[0x44 // 4] = 77
        block = prep.own_block(True, w)
        self.assertEqual(block[prep.mmSQ_PGM_RESOURCES_VS], 1)
        self.assertEqual(block[prep.mmVGT_PRIMITIVEID_EN], 2)
        self.assertEqual(block[prep.mmSPI_VS_OUT_CONFIG], 3)
        self.assertEqual(block[prep.mmPA_CL_VS_OUT_CNTL], 4)
        self.assertEqual(block[prep.mmSPI_VS_OUT_ID_0 + 1], 11)
        self.assertEqual(block[prep.mmSQ_VTX_SEMANTIC_CLEAR], 0xFFFFFFFF)
        self.assertEqual(block[prep.mmSQ_VTX_SEMANTIC_0], 77)

    def test_pixel_block(self):
        w = [0] * PS_WORDS
        w[0], w[2], w[3], w[4], w[5], w[37], w[38], w[39], w[40] = 5, 6, 7, 1, 8, 9, 10, 11, 12
        block = prep.own_block(False, w)
        self.assertEqual(block[prep.mmSQ_PGM_RESOURCES_PS], 5)
        self.assertEqual(block[prep.mmSPI_PS_IN_CONTROL_0 + 1], 7)
        self.assertEqual(block[prep.mmSPI_PS_INPUT_CNTL_0], 8)
        self.assertEqual(block[prep.mmCB_SHADER_MASK], 9)
        self.assertEqual(block[prep.mmSPI_INPUT_Z], 12)
        self.assertNotIn(prep.mmSQ_VTX_SEMANTIC_0, block)

    def test_counts_are_clamped(self):
        w = [0] * VS_WORDS
        w[0x0C // 4] = 1000
        w[0x40 // 4] = 1000
        block = prep.own_block(True, w)
        self.assertNotIn(prep.mmSPI_VS_OUT_ID_0 + 10, block)
        self.assertIn(prep.mmSQ_VTX_SEMANTIC_0 + 31, block)
        self.assertNotIn(prep.mmSQ_VTX_SEMANTIC_0 + 32, block)


class TemplateBuild(unittest.TestCase):
    def test_template_roundtrip_drops_program_addresses(self):
        code, fs = b"\x11" * 16, b"\x22" * 8
        regs = [(prep.mmSQ_PGM_START_VS, 0x1234), (prep.mmSQ_PGM_RESOURCES_VS, 5)]
        pipeline = b"P" * 168
        with tempfile.TemporaryDirectory() as d:
            cache = os.path.join(d, "cache.bin")
            template = os.path.join(d, "sub", "template.bin")
            with open(cache, "wb") as f:
                prep.write_record(f, prep.REC_SHADER, prep.shader_record(1, code, fs, regs))
                prep.write_record(f, prep.REC_PIPELINE, pipeline)
            with patch("sys.stdout", io.StringIO()):
                prep.cmd_template(cache, template)
            with open(template, "rb") as f:
                self.assertEqual(f.read(4), b"WWHT")
            states, pipelines = prep.read_template(template)
        self.assertEqual(pipelines, [pipeline])
        self.assertEqual(len(states), 1)
        h, vertex, fetch, kept = states[0]
        self.assertEqual((h, vertex, fetch), (prep.prog_hash(code), 1, fs))
        self.assertEqual(kept, [(prep.mmSQ_PGM_RESOURCES_VS, 5)])

    def test_merge_keeps_old_states(self):
        with tempfile.TemporaryDirectory() as d:
            paths = []
            for n, code in enumerate((b"\x01" * 8, b"\x02" * 8)):
                cache = os.path.join(d, "cache%d.bin" % n)
                with open(cache, "wb") as f:
                    prep.write_record(f, prep.REC_SHADER, prep.shader_record(0, code, b"", [(prep.mmSQ_PGM_RESOURCES_PS, n)]))
                paths.append(cache)
            first, merged = os.path.join(d, "t1.bin"), os.path.join(d, "t2.bin")
            with patch("sys.stdout", io.StringIO()):
                prep.cmd_template(paths[0], first)
                prep.cmd_template(paths[1], merged, merge=first)
            states, _ = prep.read_template(merged)
        self.assertEqual({s[0] for s in states}, {prep.prog_hash(b"\x01" * 8), prep.prog_hash(b"\x02" * 8)})

    def test_read_template_rejects_garbage(self):
        with tempfile.TemporaryDirectory() as d:
            bad = os.path.join(d, "bad.bin")
            with open(bad, "wb") as f:
                f.write(b"NOPE" + zlib.compress(b"x"))
            with self.assertRaises(SystemExit):
                prep.read_template(bad)
            with self.assertRaises(SystemExit):
                prep.read_template(os.path.join(d, "missing.bin"))

    def test_build_emits_known_pipeline_and_speculative_records(self):
        known_code, spec_code = b"\xaa" * 16, b"\xbb" * 16
        ps_words = [0] * PS_WORDS
        pipeline = b"P" * 168
        regs = [(prep.mmSQ_PGM_RESOURCES_PS, 0)]
        with tempfile.TemporaryDirectory() as d:
            cache = os.path.join(d, "cache.bin")
            with open(cache, "wb") as f:
                prep.write_record(f, prep.REC_SHADER, prep.shader_record(0, known_code, b"", regs))
                prep.write_record(f, prep.REC_PIPELINE, pipeline)
            template, out = os.path.join(d, "template.bin"), os.path.join(d, "headstart.bin")
            games = {(prep.PS, known_code): (ps_words, "k"), (prep.PS, spec_code): (ps_words, "s")}
            with patch("sys.stdout", io.StringIO()):
                prep.cmd_template(cache, template)
                with patch.object(prep, "game_shaders", return_value=games):
                    prep.cmd_build(d, out, template, 2)
            records = list(prep.read_records(out))
        kinds = [t for t, _ in records]
        self.assertEqual(kinds, [prep.REC_SHADER, prep.REC_PIPELINE, prep.REC_SPECULATIVE])
        self.assertEqual(prep.parse_shader_record(records[0][1])[1], known_code)
        self.assertEqual(records[1][1], pipeline)
        self.assertEqual(prep.parse_shader_record(records[2][1])[1], spec_code)
        known_regs = dict(prep.parse_shader_record(records[0][1])[3])
        self.assertEqual(known_regs[prep.mmSQ_PGM_START_PS + 1], len(known_code) >> 3)

    def test_build_without_pipelines(self):
        code = b"\xcc" * 8
        with tempfile.TemporaryDirectory() as d:
            cache = os.path.join(d, "cache.bin")
            with open(cache, "wb") as f:
                prep.write_record(f, prep.REC_SHADER, prep.shader_record(0, code, b"", [(prep.mmSQ_PGM_RESOURCES_PS, 1)]))
                prep.write_record(f, prep.REC_PIPELINE, b"P" * 168)
            template, out = os.path.join(d, "t.bin"), os.path.join(d, "o.bin")
            with patch("sys.stdout", io.StringIO()):
                prep.cmd_template(cache, template)
                with patch.object(prep, "game_shaders", return_value={(prep.PS, code): ([0] * PS_WORDS, "k")}):
                    prep.cmd_build(d, out, template, 2, with_pipelines=False)
            kinds = [t for t, _ in prep.read_records(out)]
        self.assertEqual(kinds, [prep.REC_SHADER])


if __name__ == "__main__":
    unittest.main()

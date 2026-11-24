import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import slot


class TakeArgs(unittest.TestCase):
    def test_slot_only(self):
        s, p, rest = slot.take_args(["1", "--trace"])
        self.assertEqual(s, 1)
        self.assertIsNone(p)
        self.assertEqual(rest, ["--trace"])

    def test_pack_only(self):
        s, p, rest = slot.take_args(["2-3"])
        self.assertIsNone(s)
        self.assertEqual(p, "2-3")
        self.assertEqual(rest, [])

    def test_take_slot_strips_pack(self):
        s, rest = slot.take_slot(["1-1", "extra"])
        self.assertIsNone(s)
        self.assertEqual(rest, ["extra"])

    def test_rejects_both(self):
        with self.assertRaises(SystemExit):
            slot.take_args(["1", "1-1"])

    def test_rejects_two_slots(self):
        with self.assertRaises(SystemExit):
            slot.take_args(["1", "2"])

    def test_course_file(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "1-27.szs")
            with open(path, "wb") as handle:
                handle.write(b"")
            s, p, rest = slot.take_args([path, "--trace"])
            self.assertIsNone(s)
            self.assertEqual(p, "1-27")
            self.assertEqual(rest, ["--trace"])

    def test_course_file_basename(self):
        with tempfile.TemporaryDirectory() as tmp:
            pack_dir = os.path.join(tmp, "game", "content", "Common", "course_res_pack")
            os.makedirs(pack_dir)
            with open(os.path.join(pack_dir, "1-27.szs"), "wb") as handle:
                handle.write(b"")
            old = os.getcwd()
            os.chdir(tmp)
            try:
                s, p, rest = slot.take_args(["1-27.szs"])
            finally:
                os.chdir(old)
            self.assertIsNone(s)
            self.assertEqual(p, "1-27")
            self.assertEqual(rest, [])

    def test_missing_course_file(self):
        with self.assertRaises(SystemExit):
            slot.take_args(["game/nope/1-27.szs"])

    def test_save_dir_is_not_a_course(self):
        s, p, rest = slot.take_args(["--save", ".tmp/playtest/opened-course/save", "1-5"])
        self.assertIsNone(s)
        self.assertEqual(p, "1-5")
        self.assertEqual(rest, ["--save", ".tmp/playtest/opened-course/save"])

    def test_bad_course_file_name(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "nope.szs")
            with open(path, "wb") as handle:
                handle.write(b"")
            with self.assertRaises(SystemExit):
                slot.take_args([path])


if __name__ == "__main__":
    unittest.main(verbosity=2)

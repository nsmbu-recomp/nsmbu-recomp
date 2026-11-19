import os
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "tools" / "lib"))
import runtime_flags
import slot


class RuntimeFlagsEncoder(unittest.TestCase):
    def test_bare_one_flag(self):
        self.assertEqual(runtime_flags.args_from_env({"NSMBU_NO_AUDIO": "1"}), ["--no-audio"])

    def test_other_values_use_equals(self):
        self.assertEqual(
            runtime_flags.args_from_env({"NSMBU_PRESS": "a,b", "NSMBU_LOG_SLOW_SWAP": "30"}),
            ["--press=a,b", "--log-slow-swap=30"],
        )

    def test_flag_name(self):
        self.assertEqual(runtime_flags.flag_name("NSMBU_NO_AUDIO"), "--no-audio")

    def test_strip_runtime(self):
        env = {"HOME": "/home/me", "NSMBU_NO_AUDIO": "1", "WIIU_COMMON_KEY": "secret", "PATH": "/bin"}
        out = runtime_flags.strip_runtime(env)
        self.assertEqual(out["HOME"], "/home/me")
        self.assertEqual(out["PATH"], "/bin")
        self.assertNotIn("NSMBU_NO_AUDIO", out)
        self.assertNotIn("WIIU_COMMON_KEY", out)

    def test_has_flag(self):
        self.assertTrue(runtime_flags.has_flag(["--no-audio"], "--no-audio"))
        self.assertTrue(runtime_flags.has_flag(["--no-audio=0"], "--no-audio"))
        self.assertFalse(runtime_flags.has_flag(["--trace"], "--no-audio"))


class TakeArgsFlags(unittest.TestCase):
    def test_res_scale_equals_not_slot(self):
        s, p, rest = slot.take_args(["--res-scale=1"])
        self.assertIsNone(s)
        self.assertIsNone(p)
        self.assertEqual(rest, ["--res-scale=1"])

    def test_res_scale_space_consumes_one(self):
        s, p, rest = slot.take_args(["--res-scale", "1"])
        self.assertIsNone(s)
        self.assertIsNone(p)
        self.assertEqual(rest, ["--res-scale", "1"])

    def test_no_audio_then_course(self):
        s, p, rest = slot.take_args(["--no-audio", "1-1"])
        self.assertIsNone(s)
        self.assertEqual(p, "1-1")
        self.assertEqual(rest, ["--no-audio"])

    def test_open_consumes_course_without_pack(self):
        s, p, rest = slot.take_args(["--open", "1-1"])
        self.assertIsNone(s)
        self.assertIsNone(p)
        self.assertEqual(rest, ["--open", "1-1"])


if __name__ == "__main__":
    unittest.main(verbosity=2)
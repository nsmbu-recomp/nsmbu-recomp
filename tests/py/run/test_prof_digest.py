import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "tools" / "run"))
import prof_digest


LOG = """
[prof] frame 900: 120 frames (0 hold), 20.00 ms/frame, 50.0 swaps/s, 48.0 logic steps/s; render thread CPU 9.00 ms/frame, in ops 8.00 ms/frame, idle (waiting for commands) 10.00 ms/frame
[prof] ops ms/frame: regs 0.20 (10/frame) draw 8.00 (100/frame)
[prof] frame 1080: 120 frames (0 hold), 16.70 ms/frame, 59.9 swaps/s, 60.0 logic steps/s; render thread CPU 6.60 ms/frame, in ops 6.20 ms/frame, idle (waiting for commands) 8.10 ms/frame
[prof] ops ms/frame: regs 0.10 (13000/frame) draw 6.00 (195/frame) swap 0.05 (1/frame)
[prof] draw phases ms/frame (195 of 195 draws sampled): shader 0.10 indices 0.05 textures 5.20 uniforms 0.40; untracked 0.00; 30.77 us/draw
[prof] render thread waits ms/frame: GPU 1.20 (2.0/frame) acquire 0.40 present 0.10; game thread waits for the render thread: none
"""


class DigestTests(unittest.TestCase):
    def test_empty_log_names_the_path(self):
        text = prof_digest.format_digest("boot only\n", ".tmp/repro/nsmbu.log")
        self.assertEqual(text, "[profile] no [prof] windows in .tmp/repro/nsmbu.log\n")

    def test_digest_uses_the_last_window(self):
        text = prof_digest.format_digest(LOG, ".tmp/repro/nsmbu.log")
        self.assertIn("last of 2 windows, frame 1080: 16.70 ms/frame, 59.9 swaps/s, 60.0 logic steps/s", text)
        self.assertIn("render cpu 6.60 ms, idle 8.10 ms", text)
        self.assertIn("waits ms/frame: gpu 1.20, acquire 0.40, present 0.10", text)
        self.assertIn("top ops ms/frame: draw 6.00, regs 0.10, swap 0.05", text)
        self.assertIn("top draw phases ms/frame: textures 5.20, uniforms 0.40, shader 0.10", text)
        self.assertNotIn("frame 900", text)
        self.assertIn("mean of last 2 windows: 18.35 ms/frame", text)

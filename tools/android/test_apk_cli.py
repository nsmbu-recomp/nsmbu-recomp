#!/usr/bin/env python3
import io
import json
import os
import sys
import unittest
from unittest.mock import patch

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "tools", "android"))
import apk


class JavaPick(unittest.TestCase):
    def test_java_major_from_version_text(self):
        self.assertEqual(apk.java_major_from_version_text('openjdk version "25.0.1"'), 25)
        self.assertEqual(apk.java_major_from_version_text('openjdk version "1.8.0_412"'), 8)
        self.assertEqual(apk.java_major_from_version_text('openjdk version "21.0.6"'), 21)

    def test_pick_gradle_java_home_prefers_21_over_default_25(self):
        with patch.dict(os.environ, {}, clear=True):
            with patch.object(apk, "jdk_candidates", return_value=[("/usr/lib/jvm/java-21", 21)]):
                self.assertEqual(apk.pick_gradle_java_home(), "/usr/lib/jvm/java-21")


class ListDevices(unittest.TestCase):
    def test_parse_devices(self):
        stdout = b"List of devices attached\nemulator-5554\tdevice product:sdk model:sdk_gphone64_arm64\n"
        with patch.object(apk, "run", return_value=type("R", (), {"stdout": stdout, "returncode": 0})()):
            out = apk.list_devices()
        self.assertEqual(out, [{"serial": "emulator-5554", "model": "sdk_gphone64_arm64"}])


class MainFlow(unittest.TestCase):
    def test_list_devices_json(self):
        buf = io.StringIO()
        with patch.object(apk, "list_devices", return_value=[{"serial": "abc", "model": None}]):
            with patch("sys.stdout", buf):
                self.assertEqual(apk.main(["--list-devices"]), 0)
        self.assertEqual(json.loads(buf.getvalue()), [{"serial": "abc", "model": None}])


if __name__ == "__main__":
    unittest.main()

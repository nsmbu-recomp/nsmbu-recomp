"""Real shared translation/host compilation through the durable job runner."""
from debug_fixture_build import register as register_fixture_build
import ctypes
import fcntl
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

from ondevice_setup import run, process_resources
from setup_adapter import Adapter

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("job_fixture", ROOT / "recomp/android_fixture.py")
fixture = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fixture)


def game_tree(out, content=b"authored content"):
    (out / "code").mkdir(parents=True)
    (out / "code/red-pro2.rpx").write_bytes(fixture.synthetic_rpx())
    (out / "content/Common/course_res_pack").mkdir(parents=True)
    (out / "content/Common/course_res_pack/1-1.szs").write_bytes(b"authored course")
    (out / "content/fixture.bin").write_bytes(content)
    (out / "meta").mkdir()
    (out / "meta/meta.xml").write_text("<menu><title_id>0005000010101d00</title_id></menu>")


class SetupJobTests(unittest.TestCase):
    def setUp(self):
        if not shutil.which("clang") or not shutil.which("ar"):
            self.skipTest("host clang/ar required")
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name).resolve()
        self.package = self.root / "package"
        shutil.copytree(ROOT / "tools", self.package / "tools",
                        ignore=shutil.ignore_patterns("__pycache__", "node_modules"))
        shutil.copytree(ROOT / "runtime/include", self.package / "sdk/include")
        link = (["-shared", "-Wl,-all_load", "{gamecode}", "-o", "{out}"] if sys.platform == "darwin" else
                ["-shared", "-Wl,--whole-archive", "{gamecode}", "-Wl,--no-whole-archive", "-o", "{out}"])
        manifest = {"gamecode_cflags": ["-std=c11", "-fPIC", "-I{gen}", "-I{sdk}/include"], "link": link}
        (self.package / "sdk/manifest.json").write_text(json.dumps(manifest))
        self.job = self.root / "job"
        self.game = self.job / "input/game"
        game_tree(self.game)
        self.configuration = self.job / "job.json"
        self.value = {"schema": 1, "jobs": 2, "port_revision": "fixture-v1",
                      "input": {"kind": "folder", "path": "input/game"},
                      "compiler": {"identity": "authored-host-compiler", "cc": [shutil.which("clang")],
                                   "cxx": [shutil.which("clang")],
                                   "ar": [shutil.which("ar")], "rsp": False}}

    def adapter(self, *args):
        adapter = Adapter(*args)
        register_fixture_build(adapter.setup)
        original = adapter.setup.recompile
        def recompile(game, generated):
            count = original(game, generated)
            (Path(generated) / "code_harness.c").write_text(
                fixture.HARNESS.replace("int main(void)", "int synthetic_check(void)"))
            return count + 1
        adapter.setup.recompile = recompile
        return adapter

    def native(self, command, env=None, cancel=lambda: False):
        result = subprocess.run(command, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        return result.returncode, result.stdout

    def execute(self, native=None):
        self.configuration.write_text(json.dumps(self.value))
        return run(self.configuration, self.package, native or self.native, self.adapter)

    def state(self):
        return json.loads((self.job / "state.json").read_text())

    def test_full_job_loads_result_and_reuses_after_restart(self):
        first = self.execute()
        self.assertEqual(ctypes.CDLL(first["library"]).synthetic_check(), 0)
        self.assertEqual(first["game"], str(self.game))
        state = self.state()
        self.assertEqual(state["state"], "ready_to_activate")
        self.assertGreater(state["resources_latest"]["worker_peak_rss_bytes"], 0)
        self.assertGreater(state["resources_latest"]["largest_reaped_child_peak_rss_bytes"], 0)
        self.assertGreaterEqual(state["resources_latest"]["reaped_children_cpu_seconds"],
                                state["resources_before"]["reaped_children_cpu_seconds"])
        def unexpected(*args, **kwargs): self.fail("Completed job invoked a compiler")
        self.assertEqual(first, self.execute(unexpected))

    def test_pause_and_failed_update_preserve_last_ready_candidate(self):
        first = self.execute()
        ready = (self.job / "ready.json").read_bytes()
        (self.job / "pause.json").write_text('{"reason":"manual"}')
        self.assertIsNone(self.execute())
        self.assertEqual(self.state()["state"], "paused")
        self.assertEqual(ready, (self.job / "ready.json").read_bytes())
        (self.job / "pause.json").unlink()
        self.value["port_revision"] = "fixture-v2"
        def fail(*args, **kwargs): return 1, b"authored compiler failure"
        with self.assertRaisesRegex(Exception, "authored compiler failure"): self.execute(fail)
        self.assertEqual(self.state()["state"], "failed")
        self.assertEqual(ready, (self.job / "ready.json").read_bytes())
        self.assertEqual(ctypes.CDLL(first["library"]).synthetic_check(), 0)
        second = self.execute()
        self.assertNotEqual(first["library"], second["library"])

    def test_low_space_update_keeps_ready_library_and_retries_after_space_restored(self):
        from types import SimpleNamespace
        from unittest.mock import patch
        first = self.execute()
        ready = (self.job / "ready.json").read_bytes()
        self.value["port_revision"] = "fixture-low-space-update"
        with patch("setup_adapter.shutil.disk_usage", return_value=SimpleNamespace(free=0)):
            with self.assertRaisesRegex(Exception, "1 GiB working reserve"):
                self.execute(lambda *a, **k: self.fail("must not launch a compiler"))
        self.assertEqual(self.state()["state"], "failed")
        self.assertEqual((self.job / "ready.json").read_bytes(), ready)
        self.assertEqual(ctypes.CDLL(first["library"]).synthetic_check(), 0)
        second = self.execute()
        self.assertNotEqual(first["library"], second["library"])
        self.assertEqual(ctypes.CDLL(second["library"]).synthetic_check(), 0)

    def test_rejects_outside_input_and_unsupported_manifest(self):
        self.value["input"]["path"] = "../outside"
        with self.assertRaisesRegex(ValueError, "inside the setup job"): self.execute()
        self.value["schema"] = 99
        with self.assertRaisesRegex(ValueError, "job version"): self.execute()
        self.value["schema"] = 1
        self.value["jobs"] = 5
        with self.assertRaisesRegex(ValueError, "concurrency"): self.execute()

    def test_second_worker_cannot_change_state(self):
        with (self.job / "worker.lock").open("a+b") as lock:
            fcntl.flock(lock, fcntl.LOCK_EX)
            with self.assertRaisesRegex(RuntimeError, "already running"): self.execute()
            self.assertFalse((self.job / "state.json").exists())

    def test_manual_pause_survives_a_fresh_worker(self):
        (self.job / "manual-pause.json").write_text('{"schema":1}')
        self.assertIsNone(self.execute())
        self.assertEqual(self.state()["state"], "paused")
        self.assertFalse((self.job / "ready.json").exists())
        (self.job / "manual-pause.json").unlink()
        self.assertEqual(ctypes.CDLL(self.execute()["library"]).synthetic_check(), 0)

    def test_incomplete_folder_names_the_missing_parts(self):
        (self.game / "code/red-pro2.rpx").unlink()
        with self.assertRaisesRegex(ValueError, "missing code/red-pro2.rpx"): self.execute()
        self.assertEqual(self.state()["state"], "failed")

    def test_update_folder_is_merged_before_translation(self):
        game_tree(self.job / "input/update", b"authored update content")
        self.value["input"]["update"] = "input/update"
        result = self.execute()
        merged = Path(result["game"])
        self.assertNotEqual(merged, self.game)
        self.assertEqual((merged / "content/fixture.bin").read_bytes(), b"authored update content")
        self.assertEqual((self.game / "content/fixture.bin").read_bytes(), b"authored content")
        self.assertEqual(ctypes.CDLL(result["library"]).synthetic_check(), 0)
        self.value["input"]["update"] = "../outside"
        with self.assertRaisesRegex(ValueError, "inside the setup job"): self.execute()

    def image_job(self, **keys):
        self.value["input"] = {"kind": "image", "path": "input/dump", **keys}
        (self.job / "input/dump").write_bytes(b"authored synthetic disc input")
        self.value["extractor"] = {"path": "authored-extractor", "identity": "authored-fixture"}
        self.configuration.write_text(json.dumps(self.value))

    def extracting(self, seen):
        def factory(*args):
            adapter = self.adapter(*args)
            def extract(path, kind, extractor, identity, native, keys, update):
                self.assertEqual((kind, extractor, identity, update), ("image", "authored-extractor", "authored-fixture", None))
                seen.append((keys.disc, keys.common))
                return self.game
            adapter.extract = extract
            return adapter
        return factory

    def test_private_key_copies_accept_cemu_keys_txt(self):
        disc, common = bytes(range(16)), bytes(range(16, 32))
        payload = ("# synthetic Cemu keys.txt\ncommon = %s\ndisc = %s\n" % (common.hex(), disc.hex())).encode()
        for name in ("disc", "common"):
            (self.job / ("input/" + name + ".key")).write_bytes(payload)
        self.image_job(disc_key="input/disc.key", common_key="input/common.key")
        seen = []
        for _ in range(2):
            result = run(self.configuration, self.package, self.native, self.extracting(seen))
            self.assertTrue(Path(result["library"]).is_file())
            self.assertEqual((self.job / "input/disc.key").read_bytes(), payload)
        self.assertEqual(seen, [(disc, common)] * 2)

    def test_empty_or_garbage_memory_key_identifies_its_role(self):
        self.image_job()
        for name in ("disc", "common"):
            for value in (b"", b"garbage", b"0" * 4097):
                with self.assertRaisesRegex(ValueError, "Invalid " + name + " key file"):
                    run(self.configuration, self.package, self.native, self.adapter, key_files={name: value})
                self.assertEqual(self.state()["state"], "failed")
                self.assertFalse((self.job / "input/disc.key").exists())
                self.assertFalse((self.job / "input/common.key").exists())
        with self.assertRaisesRegex(ValueError, "Wii U common key is needed"):
            run(self.configuration, self.package, self.native, self.adapter, key_files={"disc": bytes(16)})

    def test_one_keys_txt_in_memory_supplies_both_keys_without_persistence(self):
        self.image_job()
        disc, common = bytes(range(16)), bytes(range(16, 32))
        payload = ("common = %s\ndisc = %s\n" % (common.hex(), disc.hex())).encode()
        seen = []
        for field in ("disc", "common"):
            files = {field: payload}
            result = run(self.configuration, self.package, self.native, self.extracting(seen), key_files=files)
            self.assertTrue(Path(result["library"]).is_file())
            self.assertEqual(files, {})
            for path in self.job.rglob("*"):
                if path.suffix == ".json":
                    self.assertNotIn(disc.hex(), path.read_text())
                    self.assertNotIn(common.hex(), path.read_text())
            self.assertFalse((self.job / "input/disc.key").exists())
            self.assertFalse((self.job / "input/common.key").exists())
        self.assertEqual(seen, [(disc, common)] * 2)

    def test_invalid_private_key_can_be_corrected_in_same_job(self):
        first = self.execute()
        ready = (self.job / "ready.json").read_bytes()
        disc = self.job / "input/disc.key"
        disc.write_bytes(b"invalid authored key file")
        (self.job / "input/common.key").write_bytes(b"*" * 16)
        self.image_job(disc_key="input/disc.key", common_key="input/common.key")
        with self.assertRaisesRegex(ValueError, "Invalid disc key file"): self.execute()
        self.assertEqual(self.state()["state"], "failed")
        self.assertEqual(ready, (self.job / "ready.json").read_bytes())
        self.assertEqual(ctypes.CDLL(first["library"]).synthetic_check(), 0)
        disc.write_bytes(b"+" * 16)
        seen = []
        result = run(self.configuration, self.package, self.native, self.extracting(seen))
        self.assertEqual(seen, [(b"+" * 16, b"*" * 16)])
        self.assertEqual((self.job / "input/dump").read_bytes(), b"authored synthetic disc input")
        self.assertEqual(self.state()["state"], "ready_to_activate")
        self.assertEqual(ctypes.CDLL(result["library"]).synthetic_check(), 0)


class ResourceMetricsTests(unittest.TestCase):
    def test_platform_units_and_separate_lifetime_scopes(self):
        from types import SimpleNamespace
        from unittest.mock import patch
        own = SimpleNamespace(ru_maxrss=123, ru_utime=2., ru_stime=3.)
        child = SimpleNamespace(ru_maxrss=456, ru_utime=7., ru_stime=11.)
        for platform, scale in (("linux", 1024), ("android", 1024), ("darwin", 1)):
            with self.subTest(platform=platform), patch("ondevice_setup.sys.platform", platform), \
                    patch("ondevice_setup.resource.getrusage", side_effect=[own, child]):
                measured = process_resources()
            self.assertEqual(measured["worker_peak_rss_bytes"], 123 * scale)
            self.assertEqual(measured["largest_reaped_child_peak_rss_bytes"], 456 * scale)
            self.assertEqual(measured["reaped_children_cpu_seconds"], 18.)
            self.assertIn("not additive", measured["scope"])

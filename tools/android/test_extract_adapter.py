"""Extraction fault checks through the unchanged installer's pipe protocol."""
from debug_fixture_build import register as register_fixture_build
from pathlib import Path
import tempfile
import unittest

from setup_adapter import Adapter, Paused, digest, inventory

PACKAGE = Path(__file__).resolve().parents[2]


def game_tree(out, rpx, title="0005000010101d00"):
    (out / "code").mkdir(parents=True)
    (out / "code/red-pro2.rpx").write_bytes(rpx)
    (out / "content/Common/course_res_pack").mkdir(parents=True)
    (out / "content/Common/course_res_pack/1-1.szs").write_bytes(b"authored course")
    (out / "content/fixture.bin").write_bytes(b"authored content")
    (out / "meta").mkdir()
    (out / "meta/meta.xml").write_text("<menu><title_id>%s</title_id></menu>" % title)


class ExtractionTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.image = self.root / "synthetic.bin"
        self.image.write_bytes(b"authored fixture identity")
        self.calls, self.events = [], []
        self.rpx = self.root / "synthetic.rpx"
        from debug_fixture_build import synthetic_rpx
        self.rpx.write_bytes(synthetic_rpx())

    def adapter(self, pause=lambda: False):
        adapter = Adapter(PACKAGE, self.root / "jobs", "port-v1", self.events.append, pause)
        register_fixture_build(adapter.setup)
        return adapter

    def runner(self, command, **options):
        self.calls.append(command)
        if "info" in command:
            return 0, (b"format wua\nselected 0005000010101d00_v64\n"
                       b"version 64\nbytes 100\n"
                       b"title 0005000010101d00 64 0005000010101d00_v64 3 100\n"), b""
        out = Path(command[-1])
        game_tree(out, self.rpx.read_bytes())
        options["on_output"]("stdout", b"phase extract\nprogress 100 100\n")
        return 0, b"", b""

    def extract(self, adapter=None, runner=None, **options):
        return (adapter or self.adapter()).extract(
            self.image, "archive", "/synthetic-extractor", "test-identity", runner or self.runner, **options)

    def update(self):
        update = self.root / "update"
        game_tree(update, self.rpx.read_bytes())
        (update / "content/fixture.bin").write_bytes(b"authored update content")
        return update

    def test_update_folder_is_applied_and_part_of_the_identity(self):
        base = self.extract()
        self.assertEqual((base / "content/fixture.bin").read_bytes(), b"authored content")
        updated = self.extract(update_dir=self.update())
        self.assertNotEqual(base, updated)
        self.assertEqual((updated / "content/fixture.bin").read_bytes(), b"authored update content")
        self.assertEqual((base / "content/fixture.bin").read_bytes(), b"authored content")

    def test_folder_with_update_is_merged_into_a_durable_copy(self):
        folder = self.root / "folder"
        game_tree(folder, self.rpx.read_bytes())
        adapter = self.adapter()
        self.assertEqual(adapter.prepare_folder(folder), folder.resolve())
        update = self.update()
        merged = adapter.prepare_folder(folder, update)
        self.assertNotEqual(merged, folder.resolve())
        self.assertEqual((merged / "content/fixture.bin").read_bytes(), b"authored update content")
        self.assertEqual((folder / "content/fixture.bin").read_bytes(), b"authored content")
        again = self.adapter()
        again.setup.apply_update_to = lambda *args: self.fail("merged copy must be reused")
        self.assertEqual(merged, again.prepare_folder(folder, update))

    def test_disc_image_needs_both_keys(self):
        adapter = self.adapter()
        with self.assertRaisesRegex(adapter.setup.SetupError, "keys are required"):
            adapter.extract(self.image, "image", "/synthetic-extractor", "test-identity", self.runner)
        self.assertEqual(self.calls, [])

    def test_pause_during_dump_checksum_stops_before_native_extraction(self):
        self.image.write_bytes(b"authored dump bytes" * 200000)
        checks = 0
        def pause():
            nonlocal checks
            checks += 1
            return checks >= 4
        with self.assertRaises(Paused):
            self.extract(self.adapter(pause))
        self.assertEqual(self.calls, [])
        self.assertFalse(list((self.root / "jobs").iterdir()))
        self.assertEqual(self.events[-1]["state"], "paused")
        game = self.extract()
        self.assertEqual((game / "code/red-pro2.rpx").read_bytes(), self.rpx.read_bytes())

    def test_verified_checkpoint_reused_and_corruption_rebuilt(self):
        game = self.extract()
        expected = inventory(game)
        self.assertEqual(game, self.extract(runner=lambda *a, **k: self.fail("checkpoint not reused")))
        (game / "content/fixture.bin").write_bytes(b"corrupt")
        self.assertEqual(game, self.extract())
        self.assertEqual(expected, inventory(game))
        self.assertEqual(len(self.calls), 6)

    def test_failed_new_input_preserves_completed_generation(self):
        game = self.extract()
        expected = inventory(game)
        self.image.write_bytes(b"different authored input")
        def fail(*args, **kwargs):
            raise OSError("synthetic tool failure")
        with self.assertRaises(OSError):
            self.extract(runner=fail)
        self.assertEqual(expected, inventory(game))
        self.assertFalse(list((self.root / "jobs").rglob("pending-*")))

    def test_insufficient_extraction_space_keeps_previous_generation(self):
        previous = self.extract()
        expected = inventory(previous)
        self.image.write_bytes(b"new authored input")
        adapter = self.adapter()
        adapter.setup.free_space = lambda path: 0
        with self.assertRaisesRegex(adapter.setup.SetupError, "not enough free disk space"):
            self.extract(adapter)
        self.assertEqual(inventory(previous), expected)
        self.assertEqual(len(self.calls), 5)
        self.assertFalse(list((self.root / "jobs").rglob("pending-*")))

    def test_pause_during_native_execution_keeps_no_partial_checkpoint(self):
        paused = [False]
        def runner(command, **options):
            if "info" in command:
                return self.runner(command, **options)
            paused[0] = True
            options["on_output"]("stdout", b"phase extract\n")
        with self.assertRaises(Paused):
            self.extract(self.adapter(lambda: paused[0]), runner)
        self.assertFalse(list((self.root / "jobs").rglob("pending-*")))
        self.assertFalse(list((self.root / "jobs").rglob("complete.json")))
        self.assertTrue(self.extract().is_dir())

    def test_log_open_failure_restores_installer_hooks(self):
        adapter = self.adapter()
        original = adapter.setup.subprocess, adapter.setup.extractor, adapter.setup.GUI, adapter.setup.LOG
        def fail(*args):
            raise OSError("synthetic log failure")
        adapter.setup.Log.open = fail
        with self.assertRaises(OSError):
            self.extract(adapter)
        self.assertEqual(original, (adapter.setup.subprocess, adapter.setup.extractor,
                                    adapter.setup.GUI, adapter.setup.LOG))
        self.assertFalse(list((self.root / "jobs").rglob("pending-*")))

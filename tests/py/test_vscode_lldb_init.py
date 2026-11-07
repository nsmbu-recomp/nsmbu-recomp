import importlib.util
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "tools" / "run" / "vscode_lldb_init.py"


def load_init():
    spec = importlib.util.spec_from_file_location("vscode_lldb_init", SCRIPT)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class LldbInitCommands(unittest.TestCase):
    def test_run_args_are_not_settings_options(self):
        init = load_init()
        lines = init.command_lines(
            "/tmp/nsmbu",
            {"NSMBU_PROFILE": "1"},
            ["--game", "/tmp/game", "--save", "/tmp/save"],
        )
        run_args = next(line for line in lines if "target.run-args" in line)
        self.assertIn("settings set -- target.run-args", run_args)
        self.assertIn("'--game'", run_args)
        self.assertIn("'--save'", run_args)
        text = "\n".join(lines)
        self.assertIn("process handle SIGSEGV --pass true --stop false --notify false", text)
        self.assertLess(text.index("process handle SIGSEGV"), text.index("process launch"))
        self.assertEqual(lines[-1], "process launch --stop-at-entry")

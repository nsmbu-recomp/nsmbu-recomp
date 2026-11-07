"""Load tests/py/runtime without importing it as the runtime package."""
import importlib.util
import unittest
from pathlib import Path


def load_tests(loader, standard_tests, pattern):
    suite = unittest.TestSuite()
    folder = Path(__file__).resolve().parent / "runtime"
    for path in sorted(folder.glob("test_*.py")):
        spec = importlib.util.spec_from_file_location("nsmbu_" + path.stem, path)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        suite.addTests(loader.loadTestsFromModule(module))
    return suite

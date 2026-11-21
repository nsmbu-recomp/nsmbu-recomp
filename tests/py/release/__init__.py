import sys
from pathlib import Path
_pkg = Path(__file__).resolve().parents[3] / "tools/release"
if str(_pkg) not in sys.path:
    sys.path.insert(0, str(_pkg))

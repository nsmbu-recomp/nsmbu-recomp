"""Refresh the git-ignored Android launcher art from the active dump's meta/iconTex.tga (dev only)."""
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import game_versions as gv

ICON_NAME = "iconTex.tga"
MAKE_ICON = os.path.join(ROOT, "runtime", "android", "make_icon.py")


def running_in_ci():
    return os.environ.get("GITHUB_ACTIONS") == "true" or os.environ.get("CI") == "true"


def icon_tga_path(root=None):
    try:
        dump = gv.active_dir(root)
    except gv.GameVersionsError:
        return None
    path = os.path.join(dump, "meta", ICON_NAME)
    return path if os.path.isfile(path) else None


def android_local_icon_ready(root=None):
    return os.path.isdir(os.path.join(root or ROOT, "runtime", "android", "app", "local-res"))


def refresh_android_local_icon(root=None):
    """Write runtime/android/app/local-res from the active dump when not in CI. Returns True on success."""
    root = root or ROOT
    if running_in_ci():
        return False
    tga = icon_tga_path(root)
    if not tga:
        return False
    if not os.path.isfile(MAKE_ICON):
        return False
    dump = os.path.dirname(os.path.dirname(tga))
    r = subprocess.run(
        [sys.executable, MAKE_ICON, dump],
        cwd=root,
        capture_output=True,
        text=True,
    )
    if r.returncode != 0:
        if os.environ.get("NSMBU_VERBOSE_BUILD") == "1":
            sys.stderr.write((r.stderr or r.stdout or "make_icon failed") + "\n")
        return False
    return android_local_icon_ready(root)

#!/usr/bin/env python3
"""NSMBU (native PC port): first-run setup, repair and update.

The release contains no game files. This program builds the game on your machine from your own
disc dump:

  1. asks for the disc image (.wux/.wud) and its keys, a Cemu Wii U archive (.wua; no keys), or an
     already extracted game folder;
  2. extracts the game files (tools/bin/nsmbu-extract);
  3. translates the game's PowerPC code to C (recomp/recomp.py);
  4. compiles that code with a C compiler (Apple's Command Line Tools on macOS, a pinned llvm-mingw
     on Windows, a pinned zig toolchain on Linux; downloaded and checked automatically);
  5. links it with the prebuilt runtime (sdk/) into the game executable;
  6. precompiles shaders by running the game once in the background, then starts it.

Run it again to play, to repair the installation, or to update after downloading a new release.
Saves are never changed without asking.

Non-interactive use (tests, scripts):
  setup.py --yes --image DISC.wux [--disc-key FILE] --common-key FILE [--update-dir DIR]
           [--data-dir DIR] [--no-launch]
  setup.py --yes --archive GAME.wua [...]
  setup.py --yes --game-dir EXTRACTED_GAME [...]
  setup.py --yes --gen-dir GENERATED_C --no-launch   (build check with placeholder code, no game)

Keys can also come from the WIIU_COMMON_KEY environment variable / IMAGE.key next to the image.
A key file holds 16 raw bytes, one line of 32 hex digits, or the common/disc line of a Cemu keys.txt
(UTF-8 or UTF-16, with a byte order mark or without).
Keys are never printed, logged or stored.
"""
import argparse
import getpass
import glob
import hashlib
import json
import os
import queue
import re
import select
import shutil
import subprocess
import sys
import struct
import tarfile
import threading
import time
import urllib.request
import zipfile
import zlib
from concurrent.futures import ThreadPoolExecutor, as_completed

HERE = os.path.dirname(os.path.abspath(__file__))
PKG = os.path.normpath(os.path.join(HERE, ".."))

PORTABLE = os.path.isfile(os.path.join(PKG, "portable.txt"))

sys.dont_write_bytecode = True
os.environ["PYTHONDONTWRITEBYTECODE"] = "1"
IS_MAC = sys.platform == "darwin"
IS_WIN = sys.platform.startswith("win")
IS_LINUX = not IS_MAC and not IS_WIN
EXE_SUFFIX = ".exe" if IS_WIN else ""

LONGPATH_PREFIX = "\\\\?\\"
WINDOWS_MAX_PATH = 259
DEEPEST_DATA_PATH = 79
MAX_DATA_DIR_CHARS = WINDOWS_MAX_PATH - 1 - DEEPEST_DATA_PATH
NATIVE_WINDOWS_PATHS = IS_WIN and os.path.join("a", "b") == "a\\b"

def windows_path(p):
    """p as a normalised Windows path with backslashes, whatever separator the Python flavour uses (MSYS2's
    Python writes "/"): no "." or ".." components, no doubled separators; a drive root keeps its backslash."""
    p = os.fspath(p).replace("/", "\\")
    if p.startswith("\\\\"):
        parts = p[2:].split("\\")
        root, rest = "\\\\" + "\\".join(parts[:2]) + "\\", parts[2:]
    elif len(p) >= 2 and p[1] == ":":
        root, rest = p[:2] + "\\", p[2:].split("\\")
    else:
        root, rest = "", p.split("\\")
    out = []
    for part in rest:
        if part in ("", "."):
            continue
        if part == "..":
            if out:
                out.pop()
            continue
        out.append(part)
    return (root + "\\".join(out)) if (root or out) else "."

def longpath(p, windows=None):
    """p for Windows file APIs without the MAX_PATH limit: the absolute, normalised path with backslashes
    and the extended-length prefix (\\\\?\\C:\\..., \\\\?\\UNC\\server\\share\\... for a network share).
    Unchanged on other platforms and with a Python that joins paths with "/" (NATIVE_WINDOWS_PATHS).
    windows=True: the string form only (tests simulate Windows anywhere). Use it only for the file operation
    itself, never in messages."""
    if not (NATIVE_WINDOWS_PATHS if windows is None else windows):
        return p
    p = os.fspath(p)
    if p.startswith(LONGPATH_PREFIX) or p.startswith("\\\\.\\"):
        return p
    p = windows_path(os.path.abspath(p) if windows is None else p)
    if p.startswith("\\\\"):
        return LONGPATH_PREFIX + "UNC\\" + p[2:]
    return LONGPATH_PREFIX + p

def plain_path(p):
    """The normal form of a path longpath() made (for messages and the log)."""
    p = os.fspath(p)
    if p.startswith(LONGPATH_PREFIX + "UNC\\"):
        return "\\\\" + p[len(LONGPATH_PREFIX) + 4:]
    if p.startswith(LONGPATH_PREFIX):
        return p[len(LONGPATH_PREFIX):]
    return p

def plain_text(text):
    """text (an error message) with every extended-length path shown in its normal form."""
    text = str(text)
    for bs in ("\\\\", "\\"):
        prefix = bs * 2 + "?" + bs
        text = text.replace(prefix + "UNC" + bs, bs * 2).replace(prefix, "")
    return text

def path_length_problem(data_dir, package=None, windows=None):
    """None, or the message that stops setup before it downloads anything when the data folder is too
    deep for Windows: the game and its tools use ordinary paths (no \\\\?\\), and the deepest names below the
    data folder are the shader cache nsmbu\\shadercache\\vulkan-<8 hex>-<8 hex>-<32 hex>.bin (79 characters,
    DEEPEST_DATA_PATH), the compiler's toolchain\\llvm-mingw-...-aarch64\\bin\\aarch64-w64-mingw32-clang++.exe
    (78) and the game files (64). Setup itself unpacks, moves and deletes the compiler with longpath().
    package: the release folder; when the data folder is inside it (a portable release), that is the folder
    the player moves."""
    if not (IS_WIN if windows is None else windows):
        return None
    data_dir = windows_path(plain_path(data_dir))
    if len(data_dir) <= MAX_DATA_DIR_CHARS:
        return None
    if package and data_dir.rsplit("\\", 1)[0].lower() == windows_path(package).lower():
        package = windows_path(package)
        limit = MAX_DATA_DIR_CHARS - (len(data_dir) - len(package))
        return ("This folder's path is too long for Windows (%d characters, at most %d work). Move the NSMBU "
                "folder to a shorter place such as C:\\Games\\NSMBU and start it again."
                % (len(package), limit))
    return ("The install folder's path is too long for Windows (%d characters, at most %d work). Choose a "
            "shorter place such as C:\\Games\\NSMBU and start it again." % (len(data_dir), MAX_DATA_DIR_CHARS))
APP_NAME = "NSMBU"
TITLE_IDS = {
    "0005000010101d00": "USA",
    "0005000010101e00": "Europe",
    "0005000010101c00": "Japan",
    "000500001014b700": "USA + Luigi U",
    "000500001014b800": "Europe + Luigi U",
}

sys.path.insert(0, os.path.join(PKG, "recomp"))
import builds as game_builds
ALL_BUILDS = game_builds.all_builds()
SUPPORTED_BUILDS = {b.title_id: game_builds.by_title(b.title_id) for b in ALL_BUILDS}
SUPPORTED_TITLE = game_builds.canonical_build().title_id

SUPPORTED_VERSION = game_builds.canonical_build().title_version

SUPPORTED_RPX_SHA256 = game_builds.canonical_build().sha256

def supported_titles_text():
    return " or ".join("%s-%s (%s)" % (b.title_id[:8].upper(), b.title_id[8:].upper(), b.name)
                       for b in SUPPORTED_BUILDS.values())

def build_title_version(title_id):
    """Preferred title version for this base title id, or None if the title is unknown."""
    build = SUPPORTED_BUILDS.get((title_id or "").lower())
    return build.title_version if build else None

def accepted_title_versions(title_id):
    return {b.title_version for b in game_builds.builds_for_title(title_id)}

def supported_versions_text():
    return " or ".join("%s title version %d" % (b.name, b.title_version) for b in ALL_BUILDS)

EXTRACT_ERRORS = {
    3: "disc_key_bad",
    4: "disc_key_wrong",
    5: "common_key_bad",
    6: "common_key_wrong",
    7: "image_bad",
    8: "image_damaged",
    9: "write_failed",
    10: "wrong_title",
}

class SetupError(Exception):
    pass

class Log:
    def __init__(self):
        self.f = None

    def open(self, path):
        os.makedirs(os.path.dirname(path), exist_ok=True)
        self.f = open(path, "a", encoding="utf-8")
        self.write("==== setup %s, %s, Python %s" % (time.strftime("%Y-%m-%d %H:%M:%S"), sys.platform,
                                                    sys.version.split()[0]))

    def write(self, text):
        if self.f:
            self.f.write(plain_text(text).rstrip("\n") + "\n")
            self.f.flush()

LOG = Log()

class Protocol:
    """--gui-protocol: one JSON object per line on stdout (events), requests as JSON lines on stdin.
    Requests are never logged (a pasted key travels in one)."""

    def __init__(self):
        self.out = sys.stdout
        self.lock = threading.Lock()

    def emit(self, obj):
        line = json.dumps(obj)
        with self.lock:
            self.out.write(line + "\n")
            self.out.flush()

GUI = None

def say(text=""):
    text = plain_text(text)
    if GUI:
        GUI.emit({"event": "log", "text": text})
    else:
        print(text, flush=True)
    LOG.write(text)

def step(n, total, text, sid=""):
    if GUI:
        GUI.emit({"event": "step", "n": n, "total": total, "title": text, "id": sid})
        LOG.write("[%d/%d] %s" % (n, total, text))
        return
    say("")
    say("[%d/%d] %s" % (n, total, text))

class Progress:
    """One updating line: '  label  [#####.....]  42%  detail'."""

    def __init__(self, label):
        self.label = label
        self.last = 0.0
        self.tty = sys.stdout.isatty()
        self.lastpct = -1

    def update(self, done, total, detail="", force=False):
        now = time.time()
        pct = int(100 * done / total) if total else 100
        if not force and now - self.last < 0.2 and pct == self.lastpct:
            return
        self.last = now
        if GUI:
            GUI.emit({"event": "progress", "label": self.label, "done": done, "total": total, "detail": detail})
        elif self.tty:
            bar = "#" * (pct // 4) + "." * (25 - pct // 4)
            line = "  %s [%s] %3d%% %s" % (self.label, bar, pct, detail)
            sys.stdout.write("\r" + line[:110].ljust(110))
            sys.stdout.flush()
        elif pct // 10 != self.lastpct // 10 or force:
            print("  %s %d%% %s" % (self.label, pct, detail), flush=True)
        self.lastpct = pct

    def done(self, detail=""):
        self.update(1, 1, detail, force=True)
        if self.tty and not GUI:
            sys.stdout.write("\n")
            sys.stdout.flush()
        LOG.write("  %s done %s" % (self.label, detail))

def human(n):
    for unit in ("bytes", "KB", "MB", "GB"):
        if n < 1024 or unit == "GB":
            return ("%d %s" % (n, unit)) if unit == "bytes" else ("%.1f %s" % (n, unit))
        n /= 1024.0

def run_logged(cmd, cwd=None, env=None, what="command"):
    """Runs a command with its output going to the log; raises SetupError with the output tail."""
    LOG.write("$ " + " ".join(cmd))
    p = subprocess.run(cmd, cwd=cwd, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    out = p.stdout.decode("utf-8", "replace")
    LOG.write(out)
    if p.returncode != 0:
        tail = "\n".join(out.strip().splitlines()[-25:])
        raise SetupError("%s failed (exit code %d):\n%s" % (what, p.returncode, tail))
    return out

class UI:
    def __init__(self, interactive):
        self.interactive = interactive

    def need(self, what):
        if not self.interactive:
            raise SetupError("missing %s (non-interactive mode: pass it on the command line)" % what)

    def ask(self, prompt, default=""):
        self.need(prompt)
        try:
            ans = input(prompt + (" [%s] " % default if default else " ")).strip()
        except EOFError:
            raise SetupError("input ended")
        return ans or default

    def yesno(self, prompt, default=True, noninteractive=None):
        if not self.interactive:
            return default if noninteractive is None else noninteractive
        d = "Y/n" if default else "y/N"
        while True:
            a = self.ask("%s (%s)" % (prompt, d)).lower()
            if not a:
                return default
            if a in ("y", "yes", "j", "ja", "o", "oui", "s", "si"):
                return True
            if a in ("n", "no", "nein", "non"):
                return False

    def choose(self, prompt, options, default=1):
        self.need(prompt)
        say(prompt)
        for i, o in enumerate(options, 1):
            say("  %d) %s" % (i, o))
        while True:
            a = self.ask("Choose 1-%d:" % len(options), str(default))
            if a.isdigit() and 1 <= int(a) <= len(options):
                return int(a) - 1

    def secret(self, prompt):
        self.need(prompt)
        try:
            return getpass.getpass(prompt + " ")
        except EOFError:
            raise SetupError("input ended")

    def pick_path(self, title, folder=False, filetypes=None):
        """A native file/folder dialog where available, else a typed (or drag-and-dropped) path."""
        self.need(title)
        p = native_dialog(title, folder, filetypes)
        if p:
            say("  " + p)
            return p
        while True:
            a = self.ask("%s\n  Type the path (or drag the %s into this window) and press Enter:" %
                         (title, "folder" if folder else "file"))
            p = clean_path(a)
            if p and os.path.exists(p):
                return p
            say("  Not found: %s" % (p or "(nothing entered)"))

def clean_path(s):
    s = s.strip()
    if len(s) >= 2 and s[0] == s[-1] and s[0] in "'\"":
        s = s[1:-1]
    elif not IS_WIN:
        s = re.sub(r"\\(.)", r"\1", s)
    return os.path.abspath(os.path.expanduser(s)) if s else ""

def native_dialog(title, folder, filetypes):
    if os.environ.get("NSMBU_SETUP_NO_DIALOGS"):
        return None
    try:
        if IS_MAC:
            what = "choose folder" if folder else "choose file"
            script = 'POSIX path of (%s with prompt "%s")' % (what, title.replace('"', "'"))
            p = subprocess.run(["osascript", "-e", script], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
            out = p.stdout.decode("utf-8").strip()
            return out.rstrip("/") if p.returncode == 0 and out else None
        if IS_WIN:
            return win_folder_dialog(title) if folder else win_file_dialog(title, filetypes)
        if not (os.environ.get("DISPLAY") or os.environ.get("WAYLAND_DISPLAY")):
            return None
        if shutil.which("zenity"):
            cmd = ["zenity", "--file-selection", "--title=" + title] + (["--directory"] if folder else [])
        elif shutil.which("kdialog"):
            cmd = ["kdialog", "--title", title] + (["--getexistingdirectory", os.path.expanduser("~")] if folder
                                                    else ["--getopenfilename", os.path.expanduser("~")])
        else:
            return None
        p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
        out = p.stdout.decode("utf-8", "replace").strip()
        return out if p.returncode == 0 and out else None
    except OSError:
        return None

def _win_com():
    import ctypes
    ctypes.windll.ole32.CoInitializeEx(None, 2)
    return ctypes

def win_file_dialog(title, filetypes):
    """The standard Open dialog (GetOpenFileNameW). Returns the chosen file or None."""
    ctypes = _win_com()
    from ctypes import wintypes

    class OPENFILENAMEW(ctypes.Structure):
        _fields_ = [("lStructSize", wintypes.DWORD), ("hwndOwner", wintypes.HWND), ("hInstance", wintypes.HINSTANCE),
                    ("lpstrFilter", wintypes.LPCWSTR), ("lpstrCustomFilter", wintypes.LPWSTR),
                    ("nMaxCustFilter", wintypes.DWORD), ("nFilterIndex", wintypes.DWORD), ("lpstrFile", wintypes.LPWSTR),
                    ("nMaxFile", wintypes.DWORD), ("lpstrFileTitle", wintypes.LPWSTR), ("nMaxFileTitle", wintypes.DWORD),
                    ("lpstrInitialDir", wintypes.LPCWSTR), ("lpstrTitle", wintypes.LPCWSTR), ("Flags", wintypes.DWORD),
                    ("nFileOffset", wintypes.WORD), ("nFileExtension", wintypes.WORD), ("lpstrDefExt", wintypes.LPCWSTR),
                    ("lCustData", wintypes.LPARAM), ("lpfnHook", ctypes.c_void_p), ("lpTemplateName", wintypes.LPCWSTR),
                    ("pvReserved", ctypes.c_void_p), ("dwReserved", wintypes.DWORD), ("FlagsEx", wintypes.DWORD)]

    flt = ctypes.create_unicode_buffer("".join("%s\0%s\0" % (n, p) for n, p in (filetypes or [])) +
                                       "All files (*.*)\0*.*\0\0")
    buf = ctypes.create_unicode_buffer(32768)
    ofn = OPENFILENAMEW()
    ofn.lStructSize = ctypes.sizeof(OPENFILENAMEW)
    ofn.lpstrFilter = ctypes.cast(flt, wintypes.LPCWSTR)
    ofn.nFilterIndex = 1
    ofn.lpstrFile = ctypes.cast(buf, wintypes.LPWSTR)
    ofn.nMaxFile = len(buf)
    ofn.lpstrTitle = title
    ofn.Flags = 0x00080000 | 0x00001000 | 0x00000800 | 0x00000008
    get = ctypes.windll.comdlg32.GetOpenFileNameW
    get.argtypes = [ctypes.POINTER(OPENFILENAMEW)]
    get.restype = wintypes.BOOL
    return buf.value if get(ctypes.byref(ofn)) and buf.value else None

def win_folder_dialog(title):
    """The standard folder picker (SHBrowseForFolderW). Returns the chosen folder or None."""
    ctypes = _win_com()
    from ctypes import wintypes

    class BROWSEINFOW(ctypes.Structure):
        _fields_ = [("hwndOwner", wintypes.HWND), ("pidlRoot", ctypes.c_void_p), ("pszDisplayName", wintypes.LPWSTR),
                    ("lpszTitle", wintypes.LPCWSTR), ("ulFlags", wintypes.UINT), ("lpfn", ctypes.c_void_p),
                    ("lParam", wintypes.LPARAM), ("iImage", ctypes.c_int)]

    shell32 = ctypes.windll.shell32
    shell32.SHBrowseForFolderW.argtypes = [ctypes.POINTER(BROWSEINFOW)]
    shell32.SHBrowseForFolderW.restype = ctypes.c_void_p
    shell32.SHGetPathFromIDListW.argtypes = [ctypes.c_void_p, wintypes.LPWSTR]
    shell32.SHGetPathFromIDListW.restype = wintypes.BOOL
    ctypes.windll.ole32.CoTaskMemFree.argtypes = [ctypes.c_void_p]
    name = ctypes.create_unicode_buffer(260)
    bi = BROWSEINFOW()
    bi.pszDisplayName = ctypes.cast(name, wintypes.LPWSTR)
    bi.lpszTitle = title
    bi.ulFlags = 0x0001 | 0x0040
    pidl = shell32.SHBrowseForFolderW(ctypes.byref(bi))
    if not pidl:
        return None
    path = ctypes.create_unicode_buffer(32768)
    ok = shell32.SHGetPathFromIDListW(pidl, path)
    ctypes.windll.ole32.CoTaskMemFree(pidl)
    return path.value if ok and path.value else None

def win_known_folder(csidl):
    """A shell folder (SHGetFolderPathW): 0x02 the Start menu's Programs, 0x10 the Desktop."""
    import ctypes
    buf = ctypes.create_unicode_buffer(32768)
    if ctypes.windll.shell32.SHGetFolderPathW(None, csidl, None, 0, buf) != 0 or not buf.value:
        raise OSError("SHGetFolderPathW(0x%x) failed" % csidl)
    return buf.value

def win_shortcut(link, target, arguments="", workdir="", icon=""):
    """Writes a Windows shortcut (.lnk) with the shell's ShellLink object (IShellLinkW + IPersistFile)."""
    ctypes = _win_com()
    import uuid
    from ctypes import wintypes

    def guid(s):
        return (ctypes.c_ubyte * 16).from_buffer_copy(uuid.UUID(s).bytes_le)

    def method(obj, index, *argtypes):
        vtbl = ctypes.cast(ctypes.cast(obj, ctypes.POINTER(ctypes.c_void_p))[0], ctypes.POINTER(ctypes.c_void_p))
        return lambda *a: ctypes.WINFUNCTYPE(ctypes.HRESULT, ctypes.c_void_p, *argtypes)(vtbl[index])(obj, *a)

    def release(obj):
        vtbl = ctypes.cast(ctypes.cast(obj, ctypes.POINTER(ctypes.c_void_p))[0], ctypes.POINTER(ctypes.c_void_p))
        ctypes.WINFUNCTYPE(ctypes.c_ulong, ctypes.c_void_p)(vtbl[2])(obj)

    clsid_shell_link = guid("00021401-0000-0000-c000-000000000046")
    iid_shell_link_w = guid("000214f9-0000-0000-c000-000000000046")
    iid_persist_file = guid("0000010b-0000-0000-c000-000000000046")
    create = ctypes.windll.ole32.CoCreateInstance
    create.restype = ctypes.HRESULT
    sl, pf = ctypes.c_void_p(), ctypes.c_void_p()
    create(ctypes.byref(clsid_shell_link), None, 1, ctypes.byref(iid_shell_link_w), ctypes.byref(sl))
    try:
        method(sl, 20, wintypes.LPCWSTR)(target)
        method(sl, 11, wintypes.LPCWSTR)(arguments)
        method(sl, 9, wintypes.LPCWSTR)(workdir)
        if icon:
            method(sl, 17, wintypes.LPCWSTR, ctypes.c_int)(icon, 0)
        method(sl, 0, ctypes.c_void_p, ctypes.c_void_p)(ctypes.byref(iid_persist_file), ctypes.byref(pf))
        try:
            method(pf, 6, wintypes.LPCWSTR, wintypes.BOOL)(link, True)
        finally:
            release(pf)
    finally:
        release(sl)
    return link

def parse_key(data):
    """16 raw bytes, or 32 hex digits (whitespace, an optional 0x and dashes ignored). None if malformed."""
    if isinstance(data, bytes) and len(data) == 16:
        return data
    if isinstance(data, bytes):
        if data[:3] == b"\xef\xbb\xbf":
            data = data[3:]
        try:
            data = data.decode("ascii")
        except UnicodeDecodeError:
            return None
    h = re.sub(r"[\s-]", "", data.lstrip("\ufeff"))
    if h[:2].lower() == "0x":
        h = h[2:]
    if not re.fullmatch(r"[0-9a-fA-F]{32}", h):
        return None
    return bytes.fromhex(h)

def decode_key_text(data):
    """A key file's text: UTF-16 (byte order mark), UTF-8 (byte order mark), else UTF-8 and then latin-1."""
    if data[:2] == b"\xff\xfe":
        return data[2:].decode("utf-16-le", "replace")
    if data[:2] == b"\xfe\xff":
        return data[2:].decode("utf-16-be", "replace")
    if data[:3] == b"\xef\xbb\xbf":
        data = data[3:]
    try:
        return data.decode("utf-8")
    except UnicodeDecodeError:
        return data.decode("latin-1")

def keys_in_text(text):
    """The keys of a line-oriented key file (Cemu's keys.txt, for example): a dict of common, disc, bare and title."""
    out = {"common": None, "disc": None, "bare": None, "title": []}
    for line in re.split(r"\r\n|[\n\r]", text):
        line = line.strip().lstrip("\ufeff").strip()
        if not line or line.startswith("#") or line.startswith("//"):
            continue
        if "=" in line:
            label, _, value = line.partition("=")
        elif ":" in line:
            label, _, value = line.partition(":")
        else:
            label, value = None, line
        value = value.strip().strip("\"'").strip()
        if label is None:
            k = parse_key(value)
            if k and out["bare"] is None:
                out["bare"] = k
            continue
        name = " ".join(label.strip().strip("\"'").replace("_", " ").split()).lower()
        k = parse_key(value)
        if not k:
            continue
        if name in ("common", "common key", "wii u common key"):
            if out["common"] is None:
                out["common"] = k
        elif name in ("disc", "disc key"):
            if out["disc"] is None:
                out["disc"] = k
        elif re.fullmatch(r"[0-9a-f]{16}", name):
            out["title"].append((name, k))
    return out

def read_key_data(path):
    """A key file's bytes (at most 4096), or None."""
    try:
        if os.path.getsize(path) > 4096:
            return None
        with open(path, "rb") as f:
            return f.read()
    except OSError:
        return None

def key_from_data(data, kind):
    """The disc key or common key of a key file's bytes: 16 raw bytes, one line of 32 hex digits, or a keys.txt line."""
    if len(data) == 16:
        return data
    found = keys_in_text(decode_key_text(data))
    return found[kind] or found["bare"]

def named_key_from_data(data, kind):
    """The common or disc line of a line-oriented key file's bytes (a Cemu keys.txt); None without that line."""
    if len(data) == 16:
        return None
    return keys_in_text(decode_key_text(data))[kind]

def read_key_file(path, kind):
    """The disc key or common key of a key file: 16 raw bytes, one line of 32 hex digits, or a keys.txt line."""
    data = read_key_data(path)
    return None if data is None else key_from_data(data, kind)

def read_named_key_file(path, kind):
    """The common or disc line of a line-oriented key file (a Cemu keys.txt); None without that line."""
    data = read_key_data(path)
    return None if data is None else named_key_from_data(data, kind)

def key_file_problem(path, kind):
    """Why read_key_file(path, kind) found no key; None when it holds one."""
    try:
        if os.path.getsize(path) > 4096:
            return key_data_problem(None, kind)
        with open(path, "rb") as f:
            data = f.read()
    except OSError as e:
        return "That file cannot be read: %s" % e
    return key_data_problem(data, kind)

def key_data_problem(data, kind):
    """Why key_from_data(data, kind) found no key (data None: larger than 4096 bytes); None when it holds one."""
    what = "the Wii U common key" if kind == "common" else "the disc key"
    hint = ("That file does not contain %s: it must hold 16 raw bytes or one line of 32 hex digits, "
            "or be a Cemu keys.txt with a line for it." % what)
    if data is None or len(data) > 4096:
        return "%s This file is larger than 4096 bytes." % hint
    found = keys_in_text(decode_key_text(data))
    if found[kind] or found["bare"]:
        return None
    if found["title"]:
        return "That file holds a title key for %s, not %s." % (format_title_id(found["title"][0][0]), what)
    other = "disc" if kind == "common" else "common"
    if found[other]:
        return "That file holds the %s key, not %s." % ("disc" if other == "disc" else "Wii U common", what)
    return hint

class Keys:
    def __init__(self):
        self.disc = None
        self.common = None

    def stdin_blob(self):
        return ("disc %s\ncommon %s\n" % (self.disc.hex(), self.common.hex())).encode()

def ask_key(ui, what, hint, kind):
    """Asks for a pasted key or a key file; returns 16 bytes."""
    while True:
        i = ui.choose("The %s is needed (%s). How do you want to provide it?" % (what, hint),
                      ["type or paste it (32 hex digits; the input is hidden)", "choose a key file"])
        if i == 0:
            k = parse_key(ui.secret("Type or paste the %s and press Enter:" % what))
            if k:
                return k
            say("  That is not a key: it must be 32 hex digits (0-9, a-f).")
        else:
            p = ui.pick_path("Choose the %s file" % what)
            k = read_key_file(p, kind)
            if k:
                return k
            say("  %s" % key_file_problem(p, kind))

def default_data_dir():
    if PORTABLE:
        return os.path.join(PKG, "data")
    return legacy_data_dir()

def legacy_data_dir():
    """Where releases before 0.2 installed (and where a non-portable setup still does)."""
    if IS_MAC:
        return os.path.expanduser("~/Library/Application Support/nsmbu")
    if IS_WIN:
        return os.path.join(os.environ.get("LOCALAPPDATA") or os.path.expanduser("~\\AppData\\Local"), "NSMBU")
    base = os.environ.get("XDG_DATA_HOME") or os.path.expanduser("~/.local/share")
    return os.path.join(base, "nsmbu")

def free_space(path):
    while not os.path.exists(path):
        path = os.path.dirname(path)
    return shutil.disk_usage(path).free

class Toolchain:
    def __init__(self, cc, cxx, ar, env=None, rsp=False, desc=""):
        self.cc, self.cxx, self.ar, self.env, self.rsp, self.desc = cc, cxx, ar, env, rsp, desc

def load_toolchains():
    with open(os.path.join(HERE, "toolchains.json")) as f:
        return json.load(f)

def download(url, dst, sha256, size_hint, label):
    """Downloads url to dst with a progress bar and checks the SHA-256 before keeping it."""
    tmp = dst + ".part"
    say("  Downloading %s" % url)
    h = hashlib.sha256()
    req = urllib.request.Request(url, headers={"User-Agent": "nsmbu-setup"})
    try:
        with urllib.request.urlopen(req, timeout=60) as r, open(tmp, "wb") as f:
            total = int(r.headers.get("Content-Length") or size_hint or 0)
            pr = Progress(label)
            done = 0
            while True:
                chunk = r.read(1 << 20)
                if not chunk:
                    break
                f.write(chunk)
                h.update(chunk)
                done += len(chunk)
                pr.update(done, total or done, human(done))
            pr.done(human(done))
    except OSError as e:
        raise SetupError("download failed: %s\nCheck your internet connection and run setup again." % e)
    if h.hexdigest() != sha256:
        os.remove(tmp)
        raise SetupError("the download of %s is corrupt or was changed (SHA-256 mismatch); nothing was installed. "
                         "Run setup again; if this repeats, report it." % url)
    os.replace(tmp, dst)

def ensure_xcode_clt(ui):
    def ok():
        try:
            return (subprocess.run(["xcode-select", "-p"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0
                    and subprocess.run(["xcrun", "--find", "clang"], stdout=subprocess.DEVNULL,
                                       stderr=subprocess.DEVNULL).returncode == 0)
        except OSError:
            return False

    if ok():
        return
    if not ui.interactive:
        raise SetupError("Apple's Command Line Tools are not installed (run: xcode-select --install)")
    say("  The game code is compiled with Apple's free Command Line Tools, which are not installed yet.")
    say("  A system dialog opens now: click \"Install\" and accept the license. This takes a few minutes.")
    subprocess.run(["xcode-select", "--install"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    t0 = time.time()
    while not ok():
        if time.time() - t0 > 3 * 3600:
            raise SetupError("the Command Line Tools installation did not finish; run setup again afterwards")
        time.sleep(5)
    say("  Command Line Tools installed.")

def get_toolchain(name, data_dir, ui):
    tcs = load_toolchains()["toolchains"]
    if name not in tcs:
        raise SetupError("this release needs an unknown toolchain %r (damaged download?)" % name)
    tc = tcs[name]
    kind = tc["kind"]
    if kind == "xcode-clt":
        ensure_xcode_clt(ui)
        return Toolchain(["xcrun", "clang"], ["xcrun", "clang++"], ["xcrun", "ar"], desc="Apple Command Line Tools")
    if kind == "host-clang":
        cc = shutil.which("clang-19") or shutil.which("clang-18") or shutil.which("clang")
        cxx = shutil.which("clang++-19") or shutil.which("clang++-18") or shutil.which("clang++")
        if not cc or not cxx:
            raise SetupError("this developer package needs clang/clang++ on PATH")
        return Toolchain([cc], [cxx], ["ar"], desc="host " + os.path.basename(cc))
    root = os.path.join(data_dir, "toolchain")
    os.makedirs(root, exist_ok=True)
    tdir = os.path.join(root, tc["dir"])
    marker = os.path.join(tdir, ".nsmbu-toolchain")
    if os.environ.get("NSMBU_TOOLCHAIN_DIR"):
        tdir = os.environ["NSMBU_TOOLCHAIN_DIR"]
    elif not (os.path.isfile(marker) and open(marker).read().strip() == tc["sha256"]):
        need = tc.get("size", 0) * 6
        if free_space(root) < need:
            raise SetupError("not enough free disk space for the compiler (%s needed)" % human(need))
        say("  Getting the compiler (%s, %s download, once)." % (tc["dir"], human(tc.get("size", 0))))
        arc = os.path.join(root, os.path.basename(tc["url"].split("?")[0]))
        download(tc["url"], arc, tc["sha256"], tc.get("size"), "compiler")
        tmp = os.path.join(root, "unpack.tmp")
        shutil.rmtree(longpath(tmp), ignore_errors=True)
        os.makedirs(longpath(tmp))
        say("  Unpacking...")
        if arc.endswith(".zip"):
            with zipfile.ZipFile(arc) as z:
                z.extractall(longpath(tmp))
        elif shutil.which("tar"):
            run_logged(["tar", "-xf", arc, "-C", tmp], what="unpacking the compiler")
        else:
            with tarfile.open(arc) as t:
                t.extractall(longpath(tmp))
        src = os.path.join(tmp, tc["dir"])
        if not os.path.isdir(longpath(src)):
            raise SetupError("unexpected compiler archive layout")
        shutil.rmtree(longpath(tdir), ignore_errors=True)
        os.replace(longpath(src), longpath(tdir))
        shutil.rmtree(longpath(tmp), ignore_errors=True)
        os.remove(arc)
        with open(marker, "w") as f:
            f.write(tc["sha256"] + "\n")
    if kind == "llvm-mingw":
        b = os.path.join(tdir, "bin")
        triple = tc.get("triple", "x86_64-w64-mingw32")
        return Toolchain([os.path.join(b, triple + "-clang.exe")], [os.path.join(b, triple + "-clang++.exe")],
                         [os.path.join(b, "llvm-ar.exe")], rsp=True, desc="llvm-mingw " + tc["dir"])
    if kind == "zig":
        z = os.path.join(tdir, "zig")
        env = dict(os.environ)
        env.setdefault("ZIG_GLOBAL_CACHE_DIR", os.path.join(root, "zig-cache"))
        t = ["-target", tc["target"]]
        return Toolchain([z, "cc"] + t, [z, "c++"] + t, [z, "ar"], env=env, desc="zig " + tc["dir"])
    raise SetupError("unsupported toolchain kind %r" % kind)

def extractor():
    p = os.path.join(PKG, "tools", "bin", "nsmbu-extract" + EXE_SUFFIX)
    if not os.path.isfile(p):
        raise SetupError("tools/bin/nsmbu-extract%s is missing from this release folder (incomplete download?)" % EXE_SUFFIX)
    return p

def disc_info(image, keys):
    p = subprocess.run([extractor(), "--keys-stdin", "info", image], input=keys.stdin_blob(), stdout=subprocess.PIPE,
                       stderr=subprocess.PIPE)
    err = p.stderr.decode("utf-8", "replace").strip()
    LOG.write("nsmbu-extract info: exit %d %s" % (p.returncode, err))
    if p.returncode != 0:
        return EXTRACT_ERRORS.get(p.returncode, "image_bad"), err, None
    info = {}
    for line in p.stdout.decode().splitlines():
        k, _, v = line.partition(" ")
        info[k] = v
    return None, "", info

def format_title_id(tid):
    tid = (tid or "").lower()
    if len(tid) != 16:
        return tid
    return "%s-%s" % (tid[:8].upper(), tid[8:].upper())

def image_identity(image, disc_key_path=None, common_key=None, update_dir=None):
    """Optional title / update fields for a disc image when the keys (and update) are already known.

    Returns a dict suitable for a GUI probe or check_keys reply (empty when nothing can be read yet)."""
    out = {}
    keys = Keys()
    if disc_key_path:
        keys.disc = read_key_file(disc_key_path, "disc")
    if common_key:
        keys.common = common_key if isinstance(common_key, (bytes, bytearray)) else None
    if keys.disc and keys.common:
        problem, err, info = disc_info(image, keys)
        if not problem and info:
            tid = info.get("title_id", "")
            out["title_id"] = tid
            out["title"] = title_desc(tid)
            out["title_id_text"] = format_title_id(tid)
            try:
                out["files"] = int(info.get("files", 0))
            except ValueError:
                pass
            try:
                out["bytes"] = int(info.get("bytes", 0))
            except ValueError:
                pass
            build = SUPPORTED_BUILDS.get(tid.lower())
            if build:
                out["build"] = build.name
    if update_dir:
        try:
            build = check_update_folder(update_dir)
            out["update_name"] = build.name
            out["update_version"] = build.title_version
            out["update_sha256"] = build.sha256
            out["update_title"] = title_desc(build.title_id, build.title_version)
        except SetupError:
            pass
    return out

INCOMPLETE_GAME_FIX = (
    "Use the full game (base disc or Cemu dump) with the matching 1.3.0 update merged into one folder. "
    "Do not point the game at an Update / 0005000e… folder alone."
)

def check_title(title_id):
    """Raises SetupError unless this is a version of the game the port can be built from."""
    if title_id in SUPPORTED_BUILDS:
        return
    tid = (title_id or "").lower()
    if tid.startswith("0005000e") and ("00050000" + tid[8:]) in TITLE_IDS:
        raise SetupError("this folder is only %s, not the full game. %s" % (title_desc(tid), INCOMPLETE_GAME_FIX))
    region = TITLE_IDS.get(title_id)
    if region:
        raise SetupError("this is the %s version of New Super Mario Bros. U (title %s). The port can be built from %s."
                         % (region, title_id, supported_titles_text()))
    raise SetupError("this disc is not New Super Mario Bros. U (title id %s)" % title_id)

def title_desc(tid, version=None):
    """'New Super Mario Bros. U (USA), version 0', 'the update for New Super Mario Bros. U (USA), version 16', ..."""
    tid = tid.lower()
    region = TITLE_IDS.get("00050000" + tid[8:])
    name = "New Super Mario Bros. U (%s)" % region if region else "title %s-%s" % (tid[:8].upper(), tid[8:].upper())
    kind = tid[:8]
    v = "" if version is None else ", version %d" % version
    if kind == "0005000e":
        return "the update for %s%s" % (name, v)
    if kind == "0005000c":
        return "downloadable content for %s%s" % (name, v)
    return name + v

_ANY_SUPPORTED = object()

def archive_info(path, title=_ANY_SUPPORTED):
    """nsmbu-extract info on a Cemu archive, asking for a title (by default: whichever supported build
    the archive holds; None: just list them).
    Returns (problem, message, info);
    info: {"titles": [{id, version, folder, files, bytes}], "selected", "title_id", "version", "files", "bytes"}
    (also for problem "wrong_title": what the archive does contain)."""
    if title is _ANY_SUPPORTED:

        _, _, listed = _archive_info_one(path, None)
        title = next((t["id"] for t in listed.get("titles", []) if t["id"] in SUPPORTED_BUILDS), SUPPORTED_TITLE)
    return _archive_info_one(path, title)

def _archive_info_one(path, title):
    p = subprocess.run([extractor()] + (["--title", title] if title else []) + ["info", path], stdin=subprocess.DEVNULL,
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    err = p.stderr.decode("utf-8", "replace").strip()
    LOG.write("nsmbu-extract info (archive): exit %d %s" % (p.returncode, err))
    info = {"titles": []}
    for line in p.stdout.decode("utf-8", "replace").splitlines():
        k, _, v = line.partition(" ")
        if k == "title":
            f = v.split(" ")
            if len(f) == 5 and f[1].isdigit():
                info["titles"].append({"id": f[0], "version": int(f[1]), "folder": f[2], "files": int(f[3]),
                                       "bytes": int(f[4])})
        else:
            info[k] = v
    if p.returncode != 0:
        return EXTRACT_ERRORS.get(p.returncode, "image_bad"), err.replace("error: ", "", 1), info
    if info.get("format") != "wua":
        return "image_bad", "not a Cemu Wii U archive", info
    return None, "", info

def archive_choice(info):
    """Which title of a Cemu archive the port uses, and why. Returns (folder, notes) or raises SetupError
    with the reason the archive cannot be used."""
    titles = info.get("titles", [])
    found = ", ".join("%s (%s)" % (title_desc(t["id"], t["version"]), t["folder"]) for t in titles) or "no Wii U titles"
    if not info.get("selected"):
        games = [t for t in titles if t["id"][:8] == "00050000"]
        for t in games:
            if t["id"] in TITLE_IDS:
                try:
                    check_title(t["id"])
                except SetupError as e:
                    raise SetupError("this archive contains %s" % str(e)[len("this is "):])
        updates = ["0005000e" + t[8:] for t in SUPPORTED_BUILDS]
        need_vers = {b.title_id: b.title_version for b in SUPPORTED_BUILDS.values()}
        if any(t["id"] in updates and need_vers.get("00050000" + t["id"][8:]) == t["version"] for t in titles):
            raise SetupError("this archive has a 1.3.0 update but not the base game title (%s). "
                             "In Cemu, make the archive with the game and update (it contains: %s)."
                             % (supported_titles_text(), found))
        raise SetupError("this archive does not contain New Super Mario Bros. U, %s (it contains: %s)."
                         % (supported_titles_text(), found))
    version = int(info.get("version", -1))
    selected_id = next((t["id"] for t in titles if t["folder"] == info["selected"]), "").lower()
    update_ids = ["0005000e" + x[8:] for x in SUPPORTED_BUILDS]
    base_tid = "00050000" + selected_id[8:] if selected_id[:8] in ("00050000", "0005000e") else selected_id
    if base_tid not in SUPPORTED_BUILDS:
        check_title(base_tid if base_tid in TITLE_IDS else selected_id)
    accepted = accepted_title_versions(base_tid)
    if accepted and version not in accepted:
        raise SetupError("the game in this archive is version %d; the port accepts title version %s "
                         "(%s, folder %s)." % (version, " or ".join(str(v) for v in sorted(accepted)),
                                              SUPPORTED_BUILDS[base_tid].name, info["selected"]))
    notes = []
    for t in titles:
        if t["folder"] == info["selected"]:
            continue
        if t["id"] in update_ids:
            notes.append("Also present: %s (%s)." % (title_desc(t["id"], t["version"]), t["folder"]))
        else:
            notes.append("Not used: %s (%s), not needed for this game." % (title_desc(t["id"], t["version"]), t["folder"]))
    return info["selected"], notes

def game_folder_title(path):
    meta = os.path.join(path, "meta", "meta.xml")
    try:
        with open(meta, "rb") as f:
            m = re.search(rb"<title_id[^>]*>\s*([0-9A-Fa-f]{16})\s*<", f.read())
        return m.group(1).decode().lower() if m else None
    except OSError:
        return None

def code_title_version(path):
    """(title id, title version) from code/app.xml (the code's own metadata), else from meta/meta.xml;
    None where it cannot be read."""
    for rel, base in ((("code", "app.xml"), 16), (("meta", "meta.xml"), 10)):
        try:
            with open(os.path.join(path, *rel), "rb") as f:
                d = f.read(65536)
        except OSError:
            continue
        tid = re.search(rb"<title_id[^>]*>\s*([0-9A-Fa-f]{16})\s*<", d)
        ver = re.search(rb"<title_version[^>]*>\s*([0-9A-Fa-f]+)\s*<", d)
        if tid or ver:
            try:
                v = int(ver.group(1), base) if ver else None
            except ValueError:
                v = None
            return (tid.group(1).decode().lower() if tid else None), v
    return None, None

def file_sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()

GAME_VERSION_FIX = ("Use New Super Mario Bros. U USA (title version 64 or base disc 0), Europe (title version 80 "
                    "or base disc 0), or an NSMBU + NSLU pack (USA or Europe, title version 0): a disc image, "
                    "optionally with the matching 1.3.0 update, a merged game folder, or a Cemu archive.")

UPDATE_DIRNAME = "update"

def check_game_version(path):
    """The build of the game in `path` (recomp/builds.py), or SetupError if it is not one the
    port can be built from. The translated code and its hooks are made for exactly those files
    (USA or Europe 1.3.0), so this runs before translating."""
    rpx = os.path.join(path, "code", "red-pro2.rpx")
    try:
        digest = file_sha256(rpx)
    except OSError as e:
        raise SetupError("cannot read %s: %s" % (rpx, e))
    build = game_builds.by_sha256(digest)
    if build:
        return build
    tid, ver = code_title_version(path)
    updates = ["0005000e" + t[8:] for t in SUPPORTED_BUILDS]
    base_tid = "00050000" + tid[8:] if tid and tid[:8] in ("00050000", "0005000e") else tid
    accepted = accepted_title_versions(base_tid) if base_tid else set()
    if tid and tid not in SUPPORTED_BUILDS and tid not in updates:
        found = "the game code (code/red-pro2.rpx) is from %s (title %s-%s)" % (title_desc(tid, ver), tid[:8].upper(),
                                                                            tid[8:].upper())
    elif accepted and ver is not None and ver not in accepted:
        found = ("the game code (code/red-pro2.rpx) is %s (per the folder's app.xml/meta.xml); need title version %s"
                 % (title_desc(tid, ver) if tid else "an unknown build",
                    " or ".join(str(v) for v in sorted(accepted))))
    else:
        found = ("the game code (code/red-pro2.rpx) is not a file the port knows (SHA-256 %s...): not a supported "
                 "build (%s), or a modified or damaged copy" % (digest[:16], supported_versions_text()))
    LOG.write("red-pro2.rpx SHA-256 %s, known: %s" % (
        digest, ", ".join("%s %s" % (b.name, b.sha256[:16]) for b in ALL_BUILDS)))
    raise SetupError("%s. The port needs New Super Mario Bros. U, %s (%s). %s"
                     % (found, supported_titles_text(), supported_versions_text(), GAME_VERSION_FIX))

def valid_game_folder(path):
    return (os.path.isfile(os.path.join(path, "code", "red-pro2.rpx")) and os.path.isdir(os.path.join(path, "content"))
            and os.path.isfile(os.path.join(path, "meta", "meta.xml")))

def path_ci(root, *parts):
    """Join path parts under root with a case-insensitive match for each name; None if missing."""
    at = root
    for part in parts:
        try:
            names = os.listdir(at)
        except OSError:
            return None
        want = part.lower()
        hit = next((n for n in names if n.lower() == want), None)
        if hit is None:
            return None
        at = os.path.join(at, hit)
    return at

def require_playable_game_folder(path):
    """Full playable game tree (base content + code), not an update-only dump. Raises SetupError."""
    if not valid_game_folder(path):
        raise SetupError("%s is not an extracted game folder (needs code/red-pro2.rpx, content/, meta/meta.xml)" % path)
    tid = game_folder_title(path)
    if tid and tid.startswith("0005000e"):
        raise SetupError("%s is only %s, not the full game. %s" % (path, title_desc(tid), INCOMPLETE_GAME_FIX))
    course = path_ci(path, "content", "Common", "course_res_pack", "1-1.szs")
    if not course or not os.path.isfile(course):
        raise SetupError(
            "%s is missing the base course file content/Common/course_res_pack/1-1.szs. "
            "That usually means only the update was installed. %s" % (path, INCOMPLETE_GAME_FIX))

def find_sidecar_update(image):
    cand = os.path.join(os.path.dirname(os.path.abspath(image)), UPDATE_DIRNAME)
    return cand if valid_game_folder(cand) else None

def check_update_folder(path):
    """1.3.0 update folder for a supported build (code/, content/, meta/), or SetupError."""
    if not valid_game_folder(path):
        raise SetupError("%s is not an update folder (needs code/red-pro2.rpx, content/, meta/meta.xml)" % path)
    build = check_game_version(path)
    if build.name in ("USA_V0", "EU_V0"):
        raise SetupError("%s is %s (title version %d); a 1.3.0 update is required here, not the base disc"
                         % (path, build.name, build.title_version))
    tid, ver = code_title_version(path)
    update_tid = "0005000e" + build.title_id[8:]
    if tid and tid not in (build.title_id, update_tid):
        raise SetupError("the update folder title id is %s; need %s or %s (%s 1.3.0)"
                         % (tid, update_tid, build.title_id, build.name))
    if ver is not None and ver != build.title_version:
        raise SetupError("the update folder is title version %d; need %d (%s 1.3.0)"
                         % (ver, build.title_version, build.name))
    return build

def apply_update_to(dst, update_dir):
    for name in ("code", "content", "meta"):
        src = os.path.join(update_dir, name)
        if not os.path.isdir(src):
            raise SetupError("update is incomplete: missing %s/%s" % (UPDATE_DIRNAME, name))
        os.makedirs(os.path.join(dst, name), exist_ok=True)
        shutil.copytree(src, os.path.join(dst, name), dirs_exist_ok=True)

def get_update_dir(image, ui, args):
    """Optional matching 1.3.0 update next to the disc image, --update-dir, chosen interactively,
    or None to install the base disc alone."""
    path = getattr(args, "update_dir", None) or find_sidecar_update(image)
    if path:
        path = os.path.abspath(path)
        build = check_update_folder(path)
        say("  OK: %s 1.3.0 update in %s" % (build.name, path))
        return path
    if not ui.interactive:
        say("  No 1.3.0 update folder (optional); will use the base disc if its RPX is known.")
        return None
    if not ui.yesno("Use an optional 1.3.0 update folder? (No = base disc alone)", default=True):
        say("  Continuing without an update folder.")
        return None
    while True:
        p = ui.pick_path("Choose the optional 1.3.0 update folder (it contains code, content and meta)",
                         folder=True)
        if not p:
            say("  Continuing without an update folder.")
            return None
        try:
            check_update_folder(p)
            return os.path.abspath(p)
        except SetupError as e:
            say("  %s" % e)

def folder_size(path):
    total = 0
    for dp, _, fns in os.walk(longpath(path)):
        for fn in fns:
            try:
                total += os.path.getsize(os.path.join(dp, fn))
            except OSError:
                pass
    return total

def get_disc_keys(image, ui, args):
    keys = Keys()
    if args.disc_key:
        keys.disc = read_key_file(args.disc_key, "disc")
        if not keys.disc:
            raise SetupError("%s: %s" % (args.disc_key, key_file_problem(args.disc_key, "disc")))
    else:
        side = os.path.splitext(image)[0] + ".key"
        if os.path.isfile(side):
            keys.disc = read_key_file(side, "disc")
            if keys.disc:
                say("  Disc key: found %s" % os.path.basename(side))
    if args.common_key:
        keys.common = read_key_file(args.common_key, "common")
        if not keys.common:
            raise SetupError("%s: %s" % (args.common_key, key_file_problem(args.common_key, "common")))
    elif os.environ.get("WIIU_COMMON_KEY"):
        keys.common = parse_key(os.environ["WIIU_COMMON_KEY"])
        if not keys.common:
            raise SetupError("WIIU_COMMON_KEY is not 32 hex digits")
        say("  Common key: from WIIU_COMMON_KEY")
    else:
        src, keys.common = find_common_key(image)
        if keys.common:
            say("  Common key: found %s" % src)
    if not keys.disc:
        keys.disc = ask_key(ui, "disc key", "16 bytes, dumped together with this disc, usually a .key file next to the image", "disc")
    if not keys.common:
        keys.common = ask_key(ui, "Wii U common key", "16 bytes, the same for every Wii U, dumped from your console", "common")
    while True:
        problem, err, info = disc_info(image, keys)
        if not problem:
            return keys, info
        if problem in ("disc_key_bad", "disc_key_wrong"):
            say("  The disc key does not match this disc image.")
            if not ui.interactive or args.disc_key:
                raise SetupError(err)
            keys.disc = ask_key(ui, "disc key", "it must be the key dumped together with this disc", "disc")
        elif problem in ("common_key_bad", "common_key_wrong"):
            say("  The Wii U common key is not correct.")
            if not ui.interactive or args.common_key:
                raise SetupError(err)
            keys.common = ask_key(ui, "Wii U common key", "the same 16 bytes for every Wii U console", "common")
        else:
            raise SetupError("cannot read the disc image: %s" % err)

def extract_game(image, keys, info, data_dir, title=None, update_dir=None):
    """Extracts a disc image (keys) or one title folder of a Cemu archive (title, no keys) into data_dir/game.
    For a disc image, update_dir is the matching 1.3.0 update folder applied after the base extract."""
    dst = os.path.join(data_dir, "game")
    tmp = os.path.join(data_dir, "game.partial")
    shutil.rmtree(tmp, ignore_errors=True)
    total = int(info.get("bytes", 0))
    need = total + (1 << 30)
    if free_space(data_dir) < need:
        raise SetupError("not enough free disk space in %s: %s needed" % (data_dir, human(need)))
    run_extract(image, keys, tmp, title)
    if not valid_game_folder(tmp):
        raise SetupError("the extracted files are incomplete (no code/red-pro2.rpx)")
    try:
        if update_dir:
            say("  Applying 1.3.0 update from %s" % update_dir)
            apply_update_to(tmp, update_dir)
        build = check_game_version(tmp)
        require_playable_game_folder(tmp)
    except SetupError:
        shutil.rmtree(tmp, ignore_errors=True)
        raise
    replace_dir(tmp, dst)

def run_extract(image, keys, out, title=None, only=None):
    """nsmbu-extract into out: a disc image (keys) or one title of a Cemu archive (title, no keys);
    only: path patterns (nsmbu-extract --only) to take just those files. Removes out on failure."""
    pr = Progress("extracting")
    opts = []
    for pattern in only or []:
        opts += ["--only", pattern]
    if title:
        cmd = [extractor(), "--title", title, "--progress"] + opts + ["extract", image, out]
    else:
        cmd = [extractor(), "--keys-stdin", "--progress"] + opts + ["extract", image, out]
    p = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    errbuf = []
    t = threading.Thread(target=lambda: errbuf.append(p.stderr.read()))
    t.start()
    if keys:
        p.stdin.write(keys.stdin_blob())
    p.stdin.close()
    what, verifying = "", False
    for line in p.stdout:
        parts = line.decode().split()
        if parts == ["phase", "verify"]:
            pr, verifying = Progress("checking the archive"), True
            what = "checking the archive: " if GUI else ""
        elif parts == ["phase", "extract"]:
            if verifying:
                pr.done()
            pr, what, verifying = Progress("extracting"), "", False
        elif len(parts) == 3 and parts[0] == "progress":
            pr.update(int(parts[1]), int(parts[2]) or 1, "%s%s of %s" % (what, human(int(parts[1])), human(int(parts[2]))))
    rc = p.wait()
    t.join()
    err = b"".join(errbuf).decode("utf-8", "replace").strip()
    LOG.write(err)
    if rc != 0:
        shutil.rmtree(out, ignore_errors=True)
        raise SetupError("extracting the game failed: %s" % err)
    pr.done()

def copy_game_folder(src, data_dir):
    dst = os.path.join(data_dir, "game")
    if os.path.realpath(src) == os.path.realpath(dst):
        return
    total = folder_size(src)
    if free_space(data_dir) < total + (1 << 30):
        raise SetupError("not enough free disk space in %s: %s needed" % (data_dir, human(total + (1 << 30))))
    tmp = os.path.join(data_dir, "game.partial")
    shutil.rmtree(tmp, ignore_errors=True)
    pr = Progress("copying")
    done = 0
    for dp, dns, fns in os.walk(src):
        rel = os.path.relpath(dp, src)
        os.makedirs(os.path.join(tmp, rel), exist_ok=True)
        for fn in fns:
            s = os.path.join(dp, fn)
            shutil.copyfile(s, os.path.join(tmp, rel, fn))
            done += os.path.getsize(s)
            pr.update(done, total or 1, "%s of %s" % (human(done), human(total)))
    pr.done()
    replace_dir(tmp, dst)

def replace_dir(new, dst):
    old = dst + ".old"
    shutil.rmtree(old, ignore_errors=True)
    if os.path.exists(dst):
        os.replace(dst, old)
    os.replace(new, dst)
    shutil.rmtree(old, ignore_errors=True)

def installed_build(game_dir):
    """The build the installed game files are, or None when nothing is installed yet."""
    try:
        return game_builds.by_sha256(file_sha256(os.path.join(game_dir, "code", "red-pro2.rpx")))
    except OSError:
        return None

def recompile(game_dir, gen_dir):
    shutil.rmtree(gen_dir, ignore_errors=True)
    rpx = os.path.join(game_dir, "code", "red-pro2.rpx")
    cmd = [sys.executable, os.path.join(PKG, "recomp", "recomp.py"), rpx, gen_dir, "--progress"]
    env = dict(os.environ, NSMBU_RECOMP_MOD_HOOKS="0", PYTHONDONTWRITEBYTECODE="1")
    LOG.write("$ " + " ".join(cmd))
    pr = Progress("translating")
    pr.update(0, 1, "starting", force=True)
    p = subprocess.Popen(cmd, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, bufsize=1)
    out_lines = []
    for line in p.stdout:
        line = line.rstrip("\n")
        out_lines.append(line)
        if line.startswith("progress "):
            parts = line.split(None, 3)
            if len(parts) >= 3:
                try:
                    done, total = int(parts[1]), int(parts[2])
                except ValueError:
                    continue
                detail = parts[3] if len(parts) > 3 else ""
                pr.update(done, total, detail)
            continue
        if line.strip():
            say(line)
    rc = p.wait()
    LOG.write("\n".join(out_lines))
    if rc != 0:
        tail = "\n".join(out_lines[-25:])
        raise SetupError("translating the game code failed (exit code %d):\n%s" % (rc, tail))
    n = len(glob.glob(os.path.join(glob.escape(gen_dir), "code_*.c")))
    if n == 0:
        raise SetupError("the recompiler wrote no code")
    pr.done("%d source files" % n)
    return n

def default_jobs():
    n = os.cpu_count() or 2
    mem = None
    try:
        if IS_MAC:
            mem = int(subprocess.check_output(["sysctl", "-n", "hw.memsize"]))
        elif IS_LINUX:
            with open("/proc/meminfo") as f:
                mem = int(re.search(r"MemTotal:\s+(\d+)", f.read()).group(1)) * 1024
        elif IS_WIN:
            import ctypes

            class MS(ctypes.Structure):
                _fields_ = [("dwLength", ctypes.c_ulong), ("dwMemoryLoad", ctypes.c_ulong),
                            ("ullTotalPhys", ctypes.c_ulonglong), ("a", ctypes.c_ulonglong * 6)]
            ms = MS()
            ms.dwLength = ctypes.sizeof(MS)
            ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(ms))
            mem = ms.ullTotalPhys
    except Exception:
        pass
    if mem:
        n = min(n, max(1, int(mem / (1.5 * (1 << 30)))))
    return max(1, n)

def sub(arg, m):
    for k, v in m.items():
        arg = arg.replace("{%s}" % k, v)
    return arg

def fwd(p):
    return p.replace("\\", "/")

def compile_gamecode(tc, manifest, gen_dir, obj_dir, jobs, cancel=None, progress=None):
    os.makedirs(obj_dir, exist_ok=True)
    srcs = sorted(glob.glob(os.path.join(glob.escape(gen_dir), "code_*.c")))
    srcs += [os.path.join(gen_dir, f) for f in ("table.c", "imports.c")]
    srcs.sort(key=lambda s: -os.path.getsize(s))
    m = {"sdk": fwd(os.path.join(PKG, "sdk")), "gen": fwd(gen_dir)}
    flags = [sub(a, m) for a in manifest["gamecode_cflags"]]
    objs = []
    pr = Progress("compiling")
    total = len(srcs)
    done = [0]
    lock = threading.Lock()
    failures = []

    def one(src):
        if cancel:
            cancel()
        obj = os.path.join(obj_dir, os.path.basename(src) + ".o")
        cmd = tc.cc + flags + ["-c", fwd(src), "-o", fwd(obj)]
        p = subprocess.run(cmd, env=tc.env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        out = p.stdout.decode("utf-8", "replace")
        with lock:
            if p.returncode != 0:
                failures.append((src, out))
                LOG.write("$ " + " ".join(cmd) + "\n" + out)
            done[0] += 1
            if progress:
                progress(done[0], total)
            pr.update(done[0], total, "%d of %d files" % (done[0], total))
        return obj

    LOG.write("compile flags: " + " ".join(tc.cc + flags))
    pr.update(0, total, "0 of %d files (%d at a time)" % (total, jobs), force=True)
    with ThreadPoolExecutor(max_workers=jobs) as ex:
        for f in as_completed([ex.submit(one, s) for s in srcs]):
            objs.append(f.result())
    if failures:
        src, out = failures[0]
        raise SetupError("compiling %s failed:\n%s" % (os.path.basename(src), "\n".join(out.strip().splitlines()[-20:])))
    pr.done("%d files" % total)
    return sorted(objs)

def link_game(tc, manifest, objs, work, out_exe, recipe=None, glue=()):
    """recipe: a link line other than manifest["link"] (the Android runtime SDK), whose {glue} stands
    for the objects `glue`"""
    lib = os.path.join(work, "libgamecode.a")
    if os.path.exists(lib):
        os.remove(lib)
    if tc.rsp:
        rsp = os.path.join(work, "ar.rsp")
        with open(rsp, "w") as f:
            f.write("\n".join('"%s"' % fwd(o) for o in objs))
        run_logged(tc.ar + ["rcs", fwd(lib), "@" + fwd(rsp)], env=tc.env, what="archiving the game code")
    else:
        run_logged(tc.ar + ["rcs", lib] + objs, env=tc.env, what="archiving the game code")
    m = {"sdk": fwd(os.path.join(PKG, "sdk")), "gamecode": fwd(lib), "out": fwd(out_exe)}
    args = []
    for a in (recipe or manifest["link"]):
        args += [fwd(g) for g in glue] if a == "{glue}" else [sub(a, m)]
    if IS_LINUX:
        args += ["-Wl,-rpath,$ORIGIN"]
    if tc.rsp:
        rsp = os.path.join(work, "link.rsp")
        with open(rsp, "w") as f:
            f.write("\n".join('"%s"' % a.replace('"', '\\"') for a in args))
        cmd = tc.cxx + ["@" + fwd(rsp)]
    else:
        cmd = tc.cxx + args
    run_logged(cmd, env=tc.env, what="linking the game")
    if not os.path.isfile(out_exe):
        raise SetupError("the linker produced no executable")

def mac_app(app_path, exe_src, data_dir, version):
    """~/Applications/NSMBU.app: the game binary plus a launcher that points it at the data folder."""
    tmp = app_path + ".tmp"
    shutil.rmtree(tmp, ignore_errors=True)
    macos = os.path.join(tmp, "Contents", "MacOS")
    os.makedirs(macos)
    shutil.copy2(exe_src, os.path.join(macos, "nsmbu"))
    launcher = os.path.join(macos, "launch")
    with open(launcher, "w") as f:
        f.write('#!/bin/sh\n# written by the NSMBU setup\ncd "%s" || exit 1\nexec "$(dirname "$0")/nsmbu" '
                '--game game --save nsmbu/save "$@"\n' % data_dir.replace('"', '\\"'))
    os.chmod(launcher, 0o755)
    plist = """<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>CFBundleName</key><string>%s</string>
  <key>CFBundleDisplayName</key><string>%s</string>
  <key>CFBundleIdentifier</key><string>io.github.nsmbrecomp.nsmbu</string>
  <key>CFBundleExecutable</key><string>launch</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleShortVersionString</key><string>%s</string>
  <key>CFBundleVersion</key><string>%s</string>
  <key>LSMinimumSystemVersion</key><string>14.0</string>
  <key>NSHighResolutionCapable</key><true/>
  <key>LSApplicationCategoryType</key><string>public.app-category.games</string>
</dict></plist>
""" % (APP_NAME, APP_NAME, version.lstrip("v"), version.lstrip("v"))
    with open(os.path.join(tmp, "Contents", "Info.plist"), "w") as f:
        f.write(plist)
    subprocess.run(["codesign", "--force", "--deep", "-s", "-", tmp], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    os.makedirs(os.path.dirname(app_path), exist_ok=True)
    replace_dir(tmp, app_path)

def game_icon_png(data_dir):
    """The game's own icon (game/meta/iconTex.tga, uncompressed 32-bit) as PNG bytes, or None."""
    try:
        with open(os.path.join(data_dir, "game", "meta", "iconTex.tga"), "rb") as f:
            d = f.read()
    except OSError:
        return None
    if len(d) < 18 or d[1] != 0 or d[2] != 2 or d[16] != 32:
        return None
    w, h = struct.unpack("<HH", d[12:16])
    start = 18 + d[0]
    if not w or not h or len(d) < start + w * h * 4:
        return None
    rows = []
    for y in range(h):
        src = y if d[17] & 0x20 else h - 1 - y
        row = bytearray(d[start + src * w * 4:start + (src + 1) * w * 4])
        row[0::4], row[2::4] = row[2::4], row[0::4]
        rows.append(b"\0" + bytes(row))

    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(b"".join(rows), 9)) + chunk(b"IEND", b""))

def write_game_icon(data_dir, ico):
    """nsmbu.ico (a PNG-compressed icon) or nsmbu.png in the data folder, from the game's own icon."""
    png = game_icon_png(data_dir)
    if not png:
        return None
    path = os.path.join(data_dir, "nsmbu.ico" if ico else "nsmbu.png")
    with open(path, "wb") as f:
        if ico:
            w, h = struct.unpack(">II", png[16:24])
            f.write(struct.pack("<HHH", 0, 1, 1)
                    + struct.pack("<BBBBHHII", w % 256, h % 256, 0, 0, 1, 32, len(png), 22) + png)
        else:
            f.write(png)
    return path

def linux_launchers(data_dir, exe):
    play = os.path.join(data_dir, "play.sh")
    with open(play, "w") as f:
        f.write('#!/bin/sh\n# written by the NSMBU setup\ncd "%s" || exit 1\nexec "%s" --game game --save nsmbu/save "$@"\n'
                % (data_dir, exe))
    os.chmod(play, 0o755)
    apps = os.path.join(os.environ.get("XDG_DATA_HOME") or os.path.expanduser("~/.local/share"), "applications")
    os.makedirs(apps, exist_ok=True)
    with open(os.path.join(apps, "nsmbu.desktop"), "w") as f:
        f.write("[Desktop Entry]\nType=Application\nName=%s\nComment=New Super Mario Bros. U, native PC port\n"
                "Exec=\"%s\"\nPath=%s\nTerminal=false\nCategories=Game;\n" % (APP_NAME, play, data_dir))
        icon = write_game_icon(data_dir, ico=False)
        if icon:
            f.write("Icon=%s\n" % icon)
    return play

def windows_shortcuts(data_dir, exe):
    """Start menu and Desktop shortcuts to the built game (a failure is logged, not fatal)."""
    icon = write_game_icon(data_dir, ico=True)
    for csidl in (0x02, 0x10):
        try:
            win_shortcut(os.path.join(win_known_folder(csidl), APP_NAME + ".lnk"), exe, "--game game --save nsmbu/save",
                         data_dir, icon or "")
        except (OSError, AttributeError, ValueError) as e:
            LOG.write("shortcut not created (folder 0x%x): %s" % (csidl, e))

def launch(state, data_dir):
    say("")
    say("Starting the game...")
    if IS_MAC and state.get("app") and os.path.isdir(state["app"]):
        subprocess.Popen(["open", state["app"]])
        return
    exe = state["exe"]
    args = [exe, "--game", state.get("game_dir") or os.path.join(data_dir, "game"),
            "--save", save_dir(data_dir)]
    if IS_WIN:
        subprocess.Popen(args, cwd=data_dir, creationflags=0x00000008 | 0x00000200)
    else:
        subprocess.Popen(args, cwd=data_dir, start_new_session=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

LAUNCHER = {"darwin": "NSMBU.app", "win32": "NSMBU.exe"}.get(sys.platform, "nsmbu-launcher")

def create_shortcut():
    """Optional (portable release): a shortcut to the release folder's launcher. Returns its path."""
    target = os.path.join(PKG, LAUNCHER)
    if IS_MAC:
        apps = os.path.expanduser("~/Applications")
        os.makedirs(apps, exist_ok=True)
        link = os.path.join(apps, APP_NAME + ".app")
        if os.path.islink(link):
            os.remove(link)
        if os.path.exists(link):
            raise SetupError("%s already exists" % link)
        os.symlink(target, link)
    elif IS_LINUX:
        apps = os.path.join(os.environ.get("XDG_DATA_HOME") or os.path.expanduser("~/.local/share"), "applications")
        os.makedirs(apps, exist_ok=True)
        link = os.path.join(apps, "nsmbu.desktop")
        with open(link, "w") as f:
            f.write("[Desktop Entry]\nType=Application\nName=%s\nExec=\"%s\"\nPath=%s\nTerminal=false\nCategories=Game;\n"
                    "Actions=setup;\n\n[Desktop Action setup]\nName=Setup (repair, update, change game)\n"
                    "Exec=\"%s\" --setup\n" % (APP_NAME, target, PKG, target))
    else:
        link = os.path.join(os.environ.get("APPDATA", ""), "Microsoft", "Windows", "Start Menu", "Programs", APP_NAME + ".lnk")
        try:
            win_shortcut(link, target, workdir=PKG)
        except (OSError, AttributeError, ValueError) as e:
            LOG.write("shortcut not created: %s" % e)
    say("  Shortcut: %s" % link)
    return link

def toolchain_dir(data_dir):
    return os.path.join(data_dir, "toolchain")

def remove_toolchain(data_dir):
    """Deletes the downloaded compiler (repair needs setup to restore it)."""
    d = toolchain_dir(data_dir)
    n = folder_size(d) if os.path.isdir(d) else 0
    shutil.rmtree(longpath(d), ignore_errors=True)
    say("  Removed the downloaded compiler (%s)" % human(n))
    return n

def save_dir(data_dir):
    return os.path.join(data_dir, "nsmbu", "save")

def save_user_dir(data_dir):
    return os.path.join(save_dir(data_dir), "user")

SAVE_FILES = ("rp_personal_savedata.dat", "rp_savedata.dat")

def have_save(data_dir):
    user = save_user_dir(data_dir)
    return os.path.isfile(os.path.join(user, "rp_personal_savedata.dat"))

def import_save(kind, path, data_dir, replace=False):
    """Copies an NSMBU Wii U save (a folder holding rp_personal_savedata.dat, or the file itself) into
    nsmbu/save/ (the personal save into user/, the common save into common/). An existing save is only
    replaced when asked, and then first moved to nsmbu/save.backup-<time>. Returns a message."""
    root = save_dir(data_dir)
    user = save_user_dir(data_dir)
    if have_save(data_dir) and not replace:
        raise SetupError("a save already exists in %s" % user)
    if kind != "nsmbu":
        raise SetupError("unknown save import kind %r" % kind)
    tmp = os.path.join(data_dir, "save-import.tmp")
    shutil.rmtree(tmp, ignore_errors=True)
    os.makedirs(tmp)
    try:
        src = os.path.dirname(path) if os.path.isfile(path) else path
        found = {}
        for base, _, names in os.walk(src):
            for f in names:
                if f in SAVE_FILES and f not in found:
                    found[f] = os.path.join(base, f)
        if "rp_personal_savedata.dat" not in found:
            raise SetupError("no rp_personal_savedata.dat in %s" % src)
        backup = None
        if os.path.isdir(root) and any(os.listdir(p) for p in (user, os.path.join(root, "common")) if os.path.isdir(p)):
            backup = root + ".backup-" + time.strftime("%Y%m%d-%H%M%S")
            os.replace(root, backup)
        os.makedirs(os.path.dirname(root), exist_ok=True)
        os.makedirs(root)
        os.makedirs(user)
        shutil.copy2(found["rp_personal_savedata.dat"], os.path.join(user, "rp_personal_savedata.dat"))
        if "rp_savedata.dat" in found:
            common = os.path.join(root, "common")
            os.makedirs(common)
            shutil.copy2(found["rp_savedata.dat"], os.path.join(common, "rp_savedata.dat"))
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    n = len(SAVE_FILES) if len(found) == len(SAVE_FILES) else 1
    msg = "Copied %d save file%s to %s" % (n, "" if n == 1 else "s", root)
    if backup:
        msg += " (the previous save is in %s)" % backup
    say("  " + msg)
    return msg

def source_checkout_user_dir():
    """`<repo>/nsmbu` when this setup.py lives in a source tree (the `nsmbu` CLI sets NSMBU_USER_DIR there)."""
    root = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
    nsmbu = os.path.join(root, "nsmbu")
    if os.path.isdir(nsmbu):
        return nsmbu
    return os.path.join(root, "user")

def legacy_config_dirs():
    """Settings folders to import from: source-checkout userdata, then older per-user config dirs."""
    dirs = []
    root = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
    for name in ("nsmbu", "user"):
        checkout = os.path.join(root, name)
        if os.path.isdir(checkout):
            dirs.append(checkout)
    if IS_MAC:
        dirs.extend([os.path.expanduser("~/Library/Application Support/NSMBU"),
                     os.path.expanduser("~/Library/Application Support/nsmbu")])
    elif IS_WIN:
        dirs.append(os.path.join(os.environ.get("APPDATA", ""), "NSMBU"))
    else:
        dirs.append(os.path.join(os.environ.get("XDG_CONFIG_HOME") or os.path.expanduser("~/.config"), "nsmbu"))
    return dirs

SETTINGS_ITEMS = ["controls.json", "settings.ini", "display.plist", "states", "shadercache"]

def import_sources(path=None):
    """(save folder or None, [(source, name)] settings items) of an earlier installation (path=None)
    or of another release folder."""
    items = []
    if path:
        root = os.path.join(path, "data") if os.path.isdir(os.path.join(path, "data")) else path
        save = None
        for rel in (("nsmbu", "save", "user"), ("save", "user")):
            candidate = os.path.join(root, *rel)
            if os.path.isfile(os.path.join(candidate, "rp_personal_savedata.dat")):
                save = candidate
                break
        if save is None:
            save = os.path.join(root, "nsmbu", "save", "user")
        cfg = [os.path.join(root, name) for name in ("nsmbu", "user") if os.path.isdir(os.path.join(root, name))]
        if not cfg:
            cfg = [os.path.join(root, "nsmbu")]
    else:
        checkout_save = os.path.join(source_checkout_user_dir(), "save", "user")
        legacy_candidates = [
            os.path.join(legacy_data_dir(), "nsmbu", "save", "user"),
            os.path.join(legacy_data_dir(), "save", "user"),
        ]
        if os.path.isfile(os.path.join(checkout_save, "rp_personal_savedata.dat")):
            save = checkout_save
        else:
            save = next((c for c in legacy_candidates if os.path.isfile(os.path.join(c, "rp_personal_savedata.dat"))),
                        legacy_candidates[0])
        cfg = legacy_config_dirs()
    seen = set()
    for d in cfg:
        for name in SETTINGS_ITEMS + ["shaders.bin", "graphics.plist"]:
            src = os.path.join(d, name)
            if name not in seen and os.path.exists(src):
                seen.add(name)
                items.append((src, name))
    if not path and IS_MAC and "shaders.bin" not in seen and os.path.isfile(os.path.expanduser("~/Library/Caches/nsmbu/shaders.bin")):
        items.append((os.path.expanduser("~/Library/Caches/nsmbu/shaders.bin"), "shaders.bin"))
    has_save = os.path.isfile(os.path.join(save, "rp_personal_savedata.dat"))
    return (save if has_save else None), items

def import_existing(data_dir, path=None, replace=False):
    """Copies (never moves) saves and settings of an earlier installation or another release folder."""
    if path and os.path.realpath(path) in (os.path.realpath(PKG), os.path.realpath(data_dir)):
        raise SetupError("that is this folder")
    save, items = import_sources(path)
    if not save and not items:
        raise SetupError("no saves or settings found there")
    msgs = []
    if save:
        msgs.append(import_save("nsmbu", save, data_dir, replace))
    user = os.path.join(data_dir, "nsmbu")
    os.makedirs(user, exist_ok=True)
    copied = []
    for src, name in items:
        dst = os.path.join(user, name)
        if os.path.exists(dst):
            continue
        if os.path.isdir(src):
            shutil.copytree(src, dst)
        else:
            shutil.copy2(src, dst)
        copied.append(name)
    if copied:
        msgs.append("Copied settings: %s" % ", ".join(copied))
        say("  Copied settings to %s: %s" % (user, ", ".join(copied)))
    return "; ".join(msgs)

def maybe_import_save(ui, data_dir):
    """Offers to copy an existing save into the port's save folder; never overwrites without asking."""
    user = save_user_dir(data_dir)
    if not ui.interactive:
        return
    if have_save(data_dir):
        say("  Your existing save in %s is kept." % user)
        return
    options = ["no, start with a new save",
               "an NSMBU save (a folder with rp_personal_savedata.dat, e.g. from Cemu or a Wii U)",
               "copy saves and settings from another NSMBU folder"]
    legacy = PORTABLE and any(import_sources())
    if legacy:
        options.append("copy saves and settings from my earlier installation (copied, not moved)")
    i = ui.choose("Do you want to use an existing save?", options)
    if i == 0:
        return
    try:
        if i == 2:
            import_existing(data_dir, ui.pick_path("Choose the other NSMBU folder", folder=True))
        elif i == 3:
            import_existing(data_dir)
        elif i == 1:
            import_save("nsmbu", ui.pick_path("Choose the folder that contains rp_personal_savedata.dat", folder=True),
                        data_dir)
    except SetupError as e:
        say("  Save not imported: %s (you can copy it to %s later)" % (e, user))

def load_manifest():
    p = os.path.join(PKG, "sdk", "manifest.json")
    if not os.path.isfile(p):
        raise SetupError("sdk/manifest.json is missing: run setup from the unpacked release folder")
    with open(p) as f:
        m = json.load(f)
    plat = {"darwin": "macos", "win32": "windows"}.get(sys.platform, "linux")
    if not m["platform"].startswith(plat):
        raise SetupError("this release is for %s; download the one for your system" % m["platform"])
    arch = m["platform"].split("-", 1)[1] if "-" in m["platform"] else ""
    if IS_LINUX and arch and normalize_arch(arch) != host_arch():
        raise SetupError("this release is for %s, but this computer is %s: download the %s release instead"
                         % (m["platform"], host_arch(), "linux-" + host_arch()))
    return m

def normalize_arch(a):
    a = a.lower()
    return {"amd64": "x86_64", "x64": "x86_64", "arm64": "aarch64"}.get(a, a)

def host_arch():
    """x86_64 or aarch64 (uname -m), which picks the Linux release, its pinned zig and Python."""
    import platform
    return normalize_arch(platform.machine())

def read_state(data_dir):
    try:
        with open(os.path.join(data_dir, "install.json")) as f:
            return resolved_state(json.load(f), data_dir)
    except (OSError, ValueError):
        return {}

def rel_to_data(path, data_dir):
    """Portable release: paths inside the data folder are stored relative to it, so the release folder
    can be moved or renamed; paths outside (an extracted game folder used in place) stay absolute."""
    if not PORTABLE or not path:
        return path
    try:
        rel = os.path.relpath(path, data_dir)
    except ValueError:
        return path
    return path if rel.startswith("..") else rel.replace(os.sep, "/")

def abs_from_data(path, data_dir):
    return os.path.normpath(path if not path or os.path.isabs(path) else os.path.join(data_dir, path))

def resolved_state(state, data_dir):
    st = dict(state)
    for k in ("exe", "game_dir"):
        if st.get(k):
            st[k] = abs_from_data(st[k], data_dir)
    return st

def write_state(data_dir, state):
    st = dict(state)
    for k in ("exe", "game_dir", "data_dir"):
        if st.get(k):
            st[k] = rel_to_data(st[k], data_dir)
    if PORTABLE:
        st.pop("data_dir", None)
    with open(os.path.join(data_dir, "install.json"), "w") as f:
        json.dump(st, f, indent=1)

class Ctx:
    """What one setup run works with (paths, the release, the options)."""

    def __init__(self, args):
        self.args = args
        self.manifest = load_manifest()
        self.version = self.manifest["version"]
        self.data_dir = os.path.abspath(args.data_dir or default_data_dir())
        self.app_dir = os.path.abspath(args.app_dir or os.path.expanduser("~/Applications"))
        self.exe_dir = os.path.join(self.data_dir, "bin")
        self.exe = os.path.join(self.exe_dir, self.manifest["exe"])
        os.makedirs(self.data_dir, exist_ok=True)
        LOG.open(os.path.join(self.data_dir, "setup.log"))
        problem = path_length_problem(self.data_dir, PKG)
        if problem:
            LOG.write("data folder %s: %s" % (self.data_dir, problem))
            raise SetupError(problem)
        self.game_dir = self.state().get("game_dir") or os.path.join(self.data_dir, "game")

    def state(self):
        return read_state(self.data_dir)

    def installed(self):
        st = self.state()
        return bool(st) and os.path.isfile(st.get("exe", "")) and valid_game_folder(self.game_dir)

STEP_TITLES = {
    "keys": "Checking the disc image and keys",
    "archive": "Checking the Cemu archive",
    "folder": "Checking the game folder",
    "compiler": "Getting the compiler",
    "extract": "Extracting the game files",
    "copy": "Copying the game files",
    "translate": "Translating the game code to C",
    "compile": "Compiling the game code (the longest step: several minutes)",
    "app": "Building the game",
    "shaders": "Precompiling shaders",
}

def plan_steps(kind):
    steps = {"image": ["keys", "compiler", "extract", "translate", "compile", "app"],
             "archive": ["archive", "compiler", "extract", "translate", "compile", "app"],
             "folder": ["folder", "compiler"] + ([] if PORTABLE else ["copy"]) + ["translate", "compile", "app"],
             "installed": ["compiler", "translate", "compile", "app"],
             "gen": ["compiler", "compile", "app"]}[kind]
    if kind != "gen":
        steps = steps + ["shaders"]
    return steps

def install(ctx, source, keys=None, info=None, ui=None, check_keys=None):
    """The whole build for one source: ("image", path) | ("archive", path) | ("folder", path) |
    ("installed", game_dir) | ("gen", generated C). check_keys(image) -> (keys, info) is called for an image without keys.
    Returns the new install state."""
    ui = ui or UI(False)
    args, manifest, data_dir = ctx.args, ctx.manifest, ctx.data_dir
    kind = source[0]
    steps = plan_steps(kind)
    total = len(steps)
    counter = iter(range(1, total + 1))

    def begin(sid):
        step(next(counter), total, STEP_TITLES[sid], sid)

    archive_title = None
    update_dir = None

    if kind == "image":
        begin("keys")
        if not os.path.isfile(source[1]):
            raise SetupError("disc image not found: %s" % source[1])
        if keys is None or info is None:
            keys, info = check_keys(source[1])
        check_title(info.get("title_id", ""))
        say("  OK: %s, %s files, %s" % (title_desc(info.get("title_id", "")), info.get("files"),
                                        human(int(info.get("bytes", 0)))))
        update_dir = get_update_dir(source[1], ui, args)
    elif kind == "archive":
        begin("archive")
        if not os.path.isfile(source[1]):
            raise SetupError("Cemu archive not found: %s" % source[1])
        problem, err, info = archive_info(source[1])
        if problem and problem != "wrong_title":
            raise SetupError("cannot read the Cemu archive: %s" % err)
        archive_title, notes = archive_choice(info)
        say("  Using %s from the archive (folder %s): %s files, %s. A Cemu archive needs no keys." %
            (title_desc(info["title_id"], int(info["version"])), archive_title, info.get("files"),
             human(int(info.get("bytes", 0)))))
        for n in notes:
            say("  " + n)
    elif kind == "folder":
        begin("folder")
        require_playable_game_folder(source[1])
        tid = game_folder_title(source[1])
        if tid:
            check_title(tid)
        build = check_game_version(source[1])
        say("  OK: %s (%s, title version %d)" % (source[1], build.name, build.title_version))
    elif kind == "installed":
        if not valid_game_folder(ctx.game_dir):
            raise SetupError("no installed game files in %s; run setup with your disc image" % ctx.game_dir)
        require_playable_game_folder(ctx.game_dir)
        tid = game_folder_title(ctx.game_dir)
        if tid:
            check_title(tid)
        build = check_game_version(ctx.game_dir)

    begin("compiler")
    tc = get_toolchain(manifest["toolchain"], data_dir, ui)
    say("  Using %s." % tc.desc)

    game_dir = ctx.game_dir
    if kind in ("image", "archive"):
        begin("extract")
        t0 = time.time()
        extract_game(source[1], keys, info, data_dir,
                     archive_title if kind == "archive" else None,
                     update_dir if kind == "image" else None)
        game_dir = os.path.join(data_dir, "game")
        say("  Game files are in %s (%d s)" % (game_dir, time.time() - t0))
    elif kind == "folder":
        if PORTABLE:
            game_dir = os.path.abspath(source[1])
            say("  Using the game files in %s (not copied)" % game_dir)
        else:
            begin("copy")
            t0 = time.time()
            copy_game_folder(source[1], data_dir)
            game_dir = os.path.join(data_dir, "game")
            say("  Game files are in %s (%d s)" % (game_dir, time.time() - t0))
    ctx.game_dir = game_dir
    keys = None

    work = os.path.join(data_dir, "work")
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)
    if free_space(data_dir) < (2 << 30):
        raise SetupError("not enough free disk space in %s for building (2 GB needed)" % data_dir)
    if kind == "gen":
        gen_dir = source[1]
    else:
        begin("translate")
        t0 = time.time()
        gen_dir = os.path.join(work, "gen")
        nfiles = recompile(ctx.game_dir, gen_dir)
        say("  %d source files (%d s)" % (nfiles, time.time() - t0))

    begin("compile")
    jobs = args.jobs or default_jobs()
    t0 = time.time()
    objs = compile_gamecode(tc, manifest, gen_dir, os.path.join(work, "obj"), jobs)
    say("  compiled in %d s" % (time.time() - t0))

    begin("app")
    say("  Linking...")
    exe, exe_dir = ctx.exe, ctx.exe_dir
    os.makedirs(exe_dir, exist_ok=True)
    new_exe = exe + ".new" + EXE_SUFFIX
    link_game(tc, manifest, objs, work, new_exe)
    for rf in manifest.get("runtime_files", []):

        shutil.copyfile(os.path.join(PKG, "sdk", "runtime", rf), os.path.join(exe_dir, rf))
        shutil.copymode(os.path.join(PKG, "sdk", "runtime", rf), os.path.join(exe_dir, rf))
    os.replace(new_exe, exe)
    say("  Built %s" % exe)

    state = {"version": ctx.version, "platform": manifest["platform"], "exe": exe, "data_dir": data_dir,
             "game_dir": ctx.game_dir, "portable": PORTABLE,
             "installed": time.strftime("%Y-%m-%d %H:%M:%S"), "toolchain": manifest["toolchain"],
             "placeholder_code": kind == "gen"}
    if os.environ.get("APPIMAGE"):

        state["appimage"] = os.environ["APPIMAGE"]
    if PORTABLE:

        with open(os.path.join(exe_dir, "portable.txt"), "w") as f:
            f.write("Portable mode: this game keeps its settings, controls, save states and shader caches in\n"
                    "../nsmbu (next to this folder) instead of your user folders. Delete this file to use those.\n")
        os.makedirs(os.path.join(data_dir, "nsmbu"), exist_ok=True)
        if args.shortcuts and kind != "gen":
            state["shortcut"] = create_shortcut()
    elif not args.no_shortcuts and kind != "gen":
        if IS_MAC:
            app = os.path.join(ctx.app_dir, APP_NAME + ".app")
            mac_app(app, exe, data_dir, ctx.version)
            os.remove(exe)
            if not os.listdir(exe_dir):
                os.rmdir(exe_dir)
            state["app"] = app
            state["exe"] = os.path.join(app, "Contents", "MacOS", "nsmbu")
            say("  App: %s" % app)
        elif IS_LINUX:
            state["launcher"] = linux_launchers(data_dir, exe)
            say("  Launcher: %s (also in your applications menu)" % state["launcher"])
        else:
            windows_shortcuts(data_dir, exe)
            say("  Shortcuts: Start menu and desktop (\"%s\")" % APP_NAME)
    if kind != "gen":
        begin("shaders")
        precompile_shaders(state["exe"], ctx.game_dir, data_dir)
    if not args.keep_work:
        shutil.rmtree(work, ignore_errors=True)
    if kind != "gen":
        os.makedirs(save_dir(data_dir), exist_ok=True)
    write_state(data_dir, state)
    return state

def main():
    ap = argparse.ArgumentParser(description="NSMBU setup", formatter_class=argparse.RawDescriptionHelpFormatter,
                                 epilog=__doc__)
    ap.add_argument("--image", help="disc image (.wux or .wud; a .wua is taken as --archive)")
    ap.add_argument("--archive", help="Cemu Wii U archive (.wua): no keys needed")
    ap.add_argument("--disc-key", help="disc key file (default: IMAGE.key)")
    ap.add_argument("--common-key", help="Wii U common key file (or WIIU_COMMON_KEY)")
    ap.add_argument("--update-dir", help="optional 1.3.0 update folder for that disc (code/, content/, meta/); "
                    "default: update/ next to the disc image if present")
    ap.add_argument("--game-dir", help="an already extracted game folder (code/, content/, meta/) instead of an image")
    ap.add_argument("--gen-dir", help="use this generated code instead of recompiling (build checks)")
    ap.add_argument("--data-dir", help="where the game is installed (default: %s)" % default_data_dir())
    ap.add_argument("--app-dir", help="macOS: where the app goes (default: ~/Applications)")
    ap.add_argument("--repair", action="store_true", help="rebuild the game code from the installed game files")
    ap.add_argument("--jobs", type=int, help="parallel compiler processes")
    ap.add_argument("--yes", action="store_true", help="non-interactive (also NSMBU_SETUP_NONINTERACTIVE=1)")
    ap.add_argument("--no-launch", action="store_true", help="do not start the game at the end")
    ap.add_argument("--no-shortcuts", action="store_true", help="no app bundle / menu entries")
    ap.add_argument("--shortcuts", action="store_true",
                    help="portable release: also add a shortcut (Applications / Start menu / applications menu)")
    ap.add_argument("--keep-work", action="store_true", help="keep the generated code and objects")
    ap.add_argument("--gui-protocol", action="store_true",
                    help="machine interface for the graphical installer (JSON lines; see installer/README.md)")
    args = ap.parse_args()
    try:
        sys.stdout.reconfigure(errors="replace")
    except AttributeError:
        pass
    if args.gui_protocol:
        return gui_main(args)
    interactive = not (args.yes or os.environ.get("NSMBU_SETUP_NONINTERACTIVE")) and sys.stdin.isatty()
    ui = UI(interactive)
    try:
        rc = run(args, ui)
    except SetupError as e:
        say("")
        say("Setup stopped: %s" % e)
        if LOG.f:
            say("Details are in %s" % LOG.f.name)
        rc = 1
    except KeyboardInterrupt:
        say("\nSetup cancelled. Run it again to continue; nothing that was finished is lost.")
        rc = 130
    if interactive and (IS_WIN or IS_MAC):
        try:
            input("\nPress Enter to close this window.")
        except (EOFError, KeyboardInterrupt):
            pass
    return rc

def run(args, ui):
    ctx = Ctx(args)
    version, data_dir, game_dir = ctx.version, ctx.data_dir, ctx.game_dir
    say("New Super Mario Bros. U - native PC port, setup %s" % version)
    say("This release contains no game files. Setup builds the game from your own dump of the game.")
    say("Install folder: %s" % data_dir)
    state = ctx.state()
    installed = ctx.installed()
    source = None
    if args.gen_dir:
        source = ("gen", os.path.abspath(args.gen_dir))
    elif args.archive or (args.image and args.image.lower().endswith(".wua")):
        source = ("archive", os.path.abspath(args.archive or args.image))
    elif args.image:
        source = ("image", os.path.abspath(args.image))
    elif args.game_dir:
        source = ("folder", os.path.abspath(args.game_dir))
    elif args.repair:
        if not valid_game_folder(game_dir):
            raise SetupError("no installed game files to repair from; run setup with your disc image")
        source = ("installed", game_dir)
    elif installed and ui.interactive:
        say("")
        if state.get("version") != version:
            say("Installed: %s. This release: %s." % (state.get("version"), version))
            i = ui.choose("What do you want to do?", ["update to %s (keeps your game files and saves)" % version,
                                                      "play the installed version", "quit"])
            if i == 1:
                launch(state, data_dir)
                return 0
            if i == 2:
                return 0
            source = ("installed", game_dir)
        else:
            i = ui.choose("The game is installed (%s). What do you want to do?" % version,
                          ["play", "repair (rebuild the game code; keeps game files and saves)",
                           "reinstall from a disc image, Cemu archive or game folder", "quit"])
            if i == 0:
                launch(state, data_dir)
                return 0
            if i == 3:
                return 0
            if i == 1:
                source = ("installed", game_dir)
    elif installed and not ui.interactive and state.get("version") == version:
        say("Already installed (%s)." % version)
        if not args.no_launch:
            launch(state, data_dir)
        return 0
    elif valid_game_folder(game_dir) and not ui.interactive:
        source = ("installed", game_dir)
    if source is None:
        if valid_game_folder(game_dir) and ui.yesno("Game files from an earlier setup were found in %s. Use them?" % game_dir):
            source = ("installed", game_dir)
        else:
            say("")
            i = ui.choose("What do you have?", [
                "a disc image (.wux or .wud) with its keys (1.3.0 update folder optional)",
                "a Cemu Wii U archive (.wua; no keys needed; USA 64 or 0, Europe 80 or 0, or a combo pack)",
                "an already extracted game folder (code, content, meta; 1.3.0 update optional)",
            ])
            if i in (0, 1):
                p = ui.pick_path("Choose your NSMBU disc image (.wux or .wud)" if i == 0 else
                                 "Choose your NSMBU Cemu archive (.wua)",
                                 filetypes=[("Wii U disc image (*.wux;*.wud)", "*.wux;*.wud")] if i == 0 else
                                 [("Cemu Wii U archive (*.wua)", "*.wua")])

                source = ("archive" if p.lower().endswith(".wua") else "image", p)
            else:
                while True:
                    p = ui.pick_path("Choose the extracted game folder (it contains code, content and meta)", folder=True)
                    if valid_game_folder(p):
                        break
                    if valid_game_folder(os.path.dirname(p)):
                        p = os.path.dirname(p)
                        break
                    say("  That folder does not contain code/red-pro2.rpx, content and meta/meta.xml.")
                source = ("folder", p)

    state = install(ctx, source, ui=ui, check_keys=lambda image: get_disc_keys(image, ui, args))
    if source[0] != "gen":
        maybe_import_save(ui, data_dir)
    if PORTABLE and ui.interactive and source[0] != "gen":
        if not args.shortcuts and ui.yesno("Add a shortcut to %s?" % {"darwin": "your Applications folder", "win32": "the Start menu"}
                                           .get(sys.platform, "your applications menu"), False):
            state["shortcut"] = create_shortcut()
            write_state(data_dir, state)
        tdir = toolchain_dir(data_dir)
        if os.path.isdir(tdir) and ui.yesno("Remove the downloaded compiler (%s)? Repair needs it; "
                                            "run setup again to restore it." % human(folder_size(tdir)), False):
            remove_toolchain(data_dir)
    say("")
    say("Done. Saves are in %s" % save_dir(data_dir))
    if source[0] == "gen":
        say("(built with placeholder code: this executable cannot run the game)")
        return 0
    if not args.no_launch and (not ui.interactive or ui.yesno("Start the game now?", True)):
        launch(state, data_dir)
    return 0

def parse_warm_progress(line):
    prefix = "[warm] progress "
    text = (line or "").strip()
    if not text.startswith(prefix):
        return None
    parts = text[len(prefix):].split(None, 2)
    if len(parts) < 2:
        return None
    try:
        done, total = int(parts[0]), int(parts[1])
    except ValueError:
        return None
    return done, total, parts[2] if len(parts) > 2 else ""

def stdin_ready():
    if IS_WIN:
        import ctypes
        import msvcrt
        from ctypes import wintypes
        avail = wintypes.DWORD()
        handle = msvcrt.get_osfhandle(sys.stdin.fileno())
        ok = ctypes.windll.kernel32.PeekNamedPipe(handle, None, 0, None, ctypes.byref(avail), None)
        return bool(ok) and avail.value > 0
    ready, _, _ = select.select([sys.stdin], [], [], 0)
    return bool(ready)

def shader_cancel_request(line):
    try:
        req = json.loads(line)
    except ValueError:
        return False
    return req.get("cmd") == "shaders_cancel"

def shader_warm_failure(code, compiled, tail):
    if code == 0 and compiled == 0:
        head = "The game ran, but it did not compile any shaders."
    else:
        head = "The game stopped before shader precompile finished (exit code %d)." % code
    lines = [ln for ln in tail if ln.strip() and not ln.startswith("[warm] progress ")][-8:]
    body = "\n".join(lines)
    hint = "Run Setup again and choose Repair to retry. Shaders also compile the first time a scene needs them."
    return head + (("\n" + body) if body else "") + "\n" + hint

def precompile_shaders(exe, game_dir, data_dir):
    """Run the built game until shaders settle. Raises SetupError on failure or cancel."""
    if not exe or not os.path.isfile(exe):
        raise SetupError("The game program is missing; cannot precompile shaders.")
    if not valid_game_folder(game_dir):
        raise SetupError("The game files are missing from %s; cannot precompile shaders." % game_dir)
    work = os.path.join(data_dir, "work", "shader-warm")
    save = os.path.join(work, "save")
    stop = os.path.join(work, "stop")
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(save)
    say("  Precompiling shaders.")
    bar = Progress("shaders")
    last = {"done": -1}

    def on_progress(done, total, detail):
        if done == last["done"]:
            return
        last["done"] = done
        text = detail or ("Waiting for the game to draw." if not done else "%d shaders compiled" % done)
        bar.update(done, total, text)
        if done:
            say("  " + (detail or "%d shaders compiled" % done))

    try:
        code, compiled, cancelled, tail = run_shader_warm(exe, game_dir, save, stop, on_progress)
    except OSError as e:
        raise SetupError("Could not start the game to compile shaders.\n%s" % e)
    finally:
        shutil.rmtree(work, ignore_errors=True)
    if cancelled:
        raise SetupError("Shader precompile was stopped.")
    if code != 0 or compiled == 0:
        raise SetupError(shader_warm_failure(code if code is not None else -1, compiled, tail))
    bar.done("%d shaders compiled" % compiled)
    say("  Shader precompile finished (%d shaders compiled)." % compiled)

def run_shader_warm(exe, game_dir, save_dir, stop_file, on_progress):
    env = os.environ.copy()
    for key in ("NSMBU_USER_DIR", "NSMBU_SHADER_CACHE", "NSMBU_VK_SHADER_CACHE",
                "NSMBU_VK_PIPELINE_CACHE", "NSMBU_HEADSTART", "NSMBU_EXIT_AT_FRAME"):
        env.pop(key, None)
    env["NSMBU_SHADER_WARM"] = stop_file
    env["NSMBU_HIDDEN_WINDOWS"] = "1"
    env["NSMBU_NO_AUDIO"] = "1"
    env["NSMBU_NO_HOST_INPUT"] = "1"
    env["NSMBU_VK_PRESENT_MODE"] = "immediate"
    proc = subprocess.Popen(
        [exe, "--game", game_dir, "--save", save_dir],
        cwd=os.path.dirname(exe) or None,
        env=env,
        stdin=subprocess.DEVNULL,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.PIPE,
        universal_newlines=True,
        encoding="utf-8",
        errors="replace",
        start_new_session=not IS_WIN)
    lines = queue.Queue()

    def read_err():
        try:
            for raw in proc.stderr:
                lines.put(raw)
        finally:
            lines.put(None)

    threading.Thread(target=read_err, daemon=True).start()
    compiled = 0
    tail = []
    cancelled = False
    stdin_open = True
    kill_at = None
    code = None
    while code is None:
        if stdin_open and not cancelled and stdin_ready():
            incoming = sys.stdin.readline()
            if not incoming:
                stdin_open = False
            elif shader_cancel_request(incoming):
                cancelled = True
                open(stop_file, "w").close()
                kill_at = time.time() + 8
        try:
            raw = lines.get(timeout=0.1)
        except queue.Empty:
            if cancelled and kill_at is not None and time.time() >= kill_at and proc.poll() is None:
                proc.kill()
            continue
        if raw is None:
            code = proc.wait()
            break
        text = raw.rstrip("\n")
        tail.append(text)
        del tail[:-40]
        progress = parse_warm_progress(text)
        if progress:
            done, total, detail = progress
            compiled = max(compiled, done)
            if on_progress:
                on_progress(done, total, detail)
    return code, compiled, cancelled, tail

def find_sidecar_disc_key(image):
    side = os.path.splitext(image)[0] + ".key"
    return side if os.path.isfile(side) and read_key_file(side, "disc") else None

def find_common_key(image):
    if os.environ.get("WIIU_COMMON_KEY") and parse_key(os.environ["WIIU_COMMON_KEY"]):
        return "WIIU_COMMON_KEY", parse_key(os.environ["WIIU_COMMON_KEY"])
    for d in (os.path.dirname(image), PKG):
        p = os.path.join(d, "common.key")
        k = read_key_file(p, "common") if os.path.isfile(p) else None
        if k:
            return p, k
    for d in (os.path.dirname(image), PKG):
        p = os.path.join(d, "keys.txt")
        k = read_named_key_file(p, "common") if os.path.isfile(p) else None
        if k:
            return p, k
    return None, None

KEY_MESSAGES = {
    "disc_key_bad": "That is not a disc key: it must be 16 raw bytes or 32 hex digits.",
    "disc_key_wrong": "The disc key does not match this disc image. It must be the key dumped together with this disc.",
    "common_key_bad": "That is not a Wii U common key: it must be 16 raw bytes or 32 hex digits.",
    "common_key_wrong": "The Wii U common key is not correct (it is the same 16 bytes on every Wii U).",
    "image_bad": "This file is not a readable Wii U disc image (.wux or .wud).",
    "archive_bad": "This file is not a readable Cemu Wii U archive (.wua).",
    "image_damaged": "The disc image is damaged.",
}

def gui_main(args):
    global GUI
    GUI = Protocol()
    try:
        ctx = Ctx(args)
    except SetupError as e:
        GUI.emit({"event": "fatal", "message": str(e)})
        return 1
    session = {"keys": None, "info": None, "image": None, "update_dir": None}

    def hello():
        st = ctx.state()
        legacy_save, legacy_items = import_sources() if PORTABLE else (None, [])
        GUI.emit({"event": "hello", "version": ctx.version, "platform": ctx.manifest["platform"],
                  "data_dir": ctx.data_dir, "app_dir": ctx.app_dir if IS_MAC else "",
                  "log": LOG.f.name if LOG.f else "", "portable": PORTABLE, "package": PKG,
                  "installed": st if ctx.installed() else None, "game_files": valid_game_folder(ctx.game_dir),
                  "game_dir": ctx.game_dir, "save_exists": have_save(ctx.data_dir),
                  "legacy": bool(legacy_save or legacy_items), "free_bytes": free_space(ctx.data_dir),
                  "toolchain": ctx.manifest["toolchain"]})

    def reply(req, ok=True, **kw):
        kw.update({"event": "reply", "id": req.get("id"), "cmd": req.get("cmd"), "ok": ok})
        GUI.emit(kw)

    def fail(req, problem, message):
        reply(req, False, problem=problem, message=message)

    def probe(req):
        p = req.get("path") or ""
        if os.path.isdir(p):
            folder = p if valid_game_folder(p) else (os.path.dirname(p) if valid_game_folder(os.path.dirname(p)) else None)
            if not folder:
                return fail(req, "invalid", "This folder does not contain an extracted game (code, content and meta).")
            try:
                require_playable_game_folder(folder)
            except SetupError as e:
                return fail(req, "incomplete", str(e)[0].upper() + str(e)[1:])
            tid = game_folder_title(folder)
            if tid and tid not in SUPPORTED_BUILDS:
                try:
                    check_title(tid)
                except SetupError as e:
                    return fail(req, "wrong_title", str(e)[0].upper() + str(e)[1:])
            try:
                build = check_game_version(folder)
            except SetupError as e:
                return fail(req, "wrong_version", str(e)[0].upper() + str(e)[1:])
            title = title_desc(tid or build.title_id, build.title_version)
            return reply(req, kind="folder", path=folder, title=title,
                         title_id=tid or build.title_id, title_id_text=format_title_id(tid or build.title_id),
                         build=build.name, update_version=build.title_version, update_sha256=build.sha256,
                         update_title=title, in_place=PORTABLE, bytes=folder_size(folder),
                         message=("Extracted game folder: %s (title %s, RPX SHA-256 %s…).%s"
                                  % (title, format_title_id(tid or build.title_id), build.sha256[:16],
                                     " It is used where it is; nothing is copied." if PORTABLE else "")))
        if not os.path.isfile(p):
            return fail(req, "invalid", "File not found.")
        if p.lower().endswith(".wua"):
            problem, err, info = archive_info(p)
            if problem and problem != "wrong_title":
                return fail(req, problem, "%s %s" % (KEY_MESSAGES["archive_bad"], err))
            try:
                folder, notes = archive_choice(info)
            except SetupError as e:
                return fail(req, "wrong_title", str(e)[0].upper() + str(e)[1:])
            tid = info["title_id"]
            ver = int(info["version"])
            title = title_desc(tid, ver)
            msg = ("Cemu Wii U archive: %s (title %s, folder %s), no keys needed. The game files are extracted from it "
                   "into this folder (about %s)." % (title, format_title_id(tid), folder, human(int(info.get("bytes", 0)))))
            return reply(req, kind="archive", path=p, title=title, title_id=tid, title_id_text=format_title_id(tid),
                         update_version=ver, folder=folder, bytes=int(info.get("bytes", 0)),
                         message=" ".join([msg] + notes))
        if not p.lower().endswith((".wux", ".wud")):
            return fail(req, "invalid", "Choose a .wux or .wud disc image, a .wua Cemu archive, or an extracted game "
                        "folder.")
        disc_key = find_sidecar_disc_key(p)
        src, common = find_common_key(p)
        upd = find_sidecar_update(p)
        ident = image_identity(p, disc_key, common, upd)
        msg_bits = []
        if ident.get("title"):
            msg_bits.append("%s (title %s)" % (ident["title"], ident.get("title_id_text") or format_title_id(ident.get("title_id"))))
            if ident.get("files") is not None and ident.get("bytes") is not None:
                msg_bits.append("%s files, %s on the disc" % (ident["files"], human(ident["bytes"])))
        if ident.get("update_title"):
            uname = ident.get("update_name") or "1.3.0"
            msg_bits.append("%s update: %s, RPX SHA-256 %s…" % (uname, ident["update_title"], ident["update_sha256"][:16]))
        elif ident.get("update_sha256"):
            msg_bits.append("1.3.0 update RPX SHA-256 %s…" % ident["update_sha256"][:16])
        if msg_bits:
            message = ("Wii U disc image: %s. Setup extracts it. A 1.3.0 update folder is optional "
                       "(recommended for mods; the USA or Europe base disc alone also works)." % "; ".join(msg_bits))
        else:
            message = ("Wii U disc image. Setup extracts it. A 1.3.0 update folder is optional "
                       "(recommended for mods; the USA or Europe base disc alone also works).")
        reply(req, kind="image", path=p, disc_key=disc_key, common_key=src, update_dir=upd or "",
              message=message, **ident)

    def request_keys(req, image):
        """(keys, None) or (None, (problem, message)) from a request's key fields (check_keys)."""
        keys = Keys()
        given_disc = req.get("disc_key_hex") or req.get("disc_key_file")
        if req.get("disc_key_hex"):
            keys.disc = parse_key(req["disc_key_hex"])
        elif req.get("disc_key_file"):
            keys.disc = read_key_file(req["disc_key_file"], "disc")
        else:
            side = find_sidecar_disc_key(image)
            keys.disc = read_key_file(side, "disc") if side else None
        if not keys.disc:
            why = key_file_problem(req["disc_key_file"], "disc") if req.get("disc_key_file") else None
            return None, (("disc_key_bad", why or KEY_MESSAGES["disc_key_bad"]) if given_disc
                          else ("disc_key_missing",
                                "No disc key was found next to the image: choose the key file or paste it."))
        given = req.get("common_key_hex") or req.get("common_key_file")
        if req.get("common_key_hex"):
            keys.common = parse_key(req["common_key_hex"])
        elif req.get("common_key_file"):
            keys.common = read_key_file(req["common_key_file"], "common")
        else:
            keys.common = find_common_key(image)[1]
        if not keys.common:
            why = key_file_problem(req["common_key_file"], "common") if req.get("common_key_file") else None
            return None, (("common_key_bad", why or KEY_MESSAGES["common_key_bad"]) if given
                          else ("common_key_missing", "The Wii U common key is needed."))
        return keys, None

    def check(req):
        image = req.get("image") or ""
        keys, why = request_keys(req, image)
        if why:
            return fail(req, *why)
        problem, err, info = disc_info(image, keys)
        if problem:
            return fail(req, problem, KEY_MESSAGES.get(problem, err))
        try:
            check_title(info.get("title_id", ""))
        except SetupError as e:
            return fail(req, "wrong_title", str(e))
        if "update_dir" in req:
            update_dir = req.get("update_dir") or None
        else:
            update_dir = find_sidecar_update(image)
        if update_dir:
            try:
                check_update_folder(update_dir)
            except SetupError as e:
                return fail(req, "wrong_update", str(e)[0].upper() + str(e)[1:])
            update_dir = os.path.abspath(update_dir)
        session.update(keys=keys, info=info, image=image, update_dir=update_dir)
        ident = image_identity(image, None, None, session["update_dir"])
        tid = info.get("title_id", "")
        ident.setdefault("title_id", tid)
        ident.setdefault("title", title_desc(tid))
        ident.setdefault("title_id_text", format_title_id(tid))
        build = SUPPORTED_BUILDS.get((tid or "").lower())
        if build:
            ident.setdefault("build", build.name)
        try:
            ident.setdefault("files", int(info.get("files", 0)))
            ident.setdefault("bytes", int(info.get("bytes", 0)))
        except ValueError:
            pass
        if not update_dir:
            ident["message"] = (ident.get("message") or "") + (
                " No update folder (optional): the base disc will be used if its RPX is known.")
        reply(req, update_dir=session["update_dir"] or "", **ident)

    def do_install(req):
        kind = req.get("source")
        if req.get("jobs"):
            ctx.args.jobs = int(req["jobs"])
        if kind == "image":
            if not session["keys"] or session["image"] != req.get("path"):
                return fail(req, "keys", "Check the keys first.")
            ctx.args.update_dir = session.get("update_dir") or None
            source = ("image", req["path"])
        elif kind == "archive":
            source = ("archive", req.get("path") or "")
        elif kind == "folder":
            source = ("folder", req.get("path") or "")
        elif kind == "installed":
            source = ("installed", ctx.game_dir)
        else:
            return fail(req, "bad_request", "unknown source")
        GUI.emit({"event": "plan", "id": req.get("id"), "steps": [{"id": s, "title": STEP_TITLES[s]} for s in plan_steps(kind)]})
        try:
            state = install(ctx, source, session["keys"], session["info"])
        except SetupError as e:
            LOG.write("setup stopped: %s" % e)
            return fail(req, "install", str(e))
        finally:
            session.update(keys=None, info=None, update_dir=None)
            ctx.args.update_dir = None
        tdir = toolchain_dir(ctx.data_dir)
        reply(req, app=state.get("app", ""), exe=state.get("exe", ""), data_dir=ctx.data_dir,
              game_dir=state.get("game_dir", ""), save_exists=have_save(ctx.data_dir),
              toolchain_bytes=folder_size(tdir) if os.path.isdir(tdir) else 0)

    def save(req):
        try:
            msg = import_save(req.get("kind", "nsmbu"), req.get("path") or "", ctx.data_dir, bool(req.get("replace")))
        except SetupError as e:
            return fail(req, "exists" if have_save(ctx.data_dir) and not req.get("replace") else "save", str(e))
        reply(req, message=msg)

    def do_import_existing(req):
        try:
            msg = import_existing(ctx.data_dir, req.get("path") or None, bool(req.get("replace")))
        except SetupError as e:
            if have_save(ctx.data_dir) and not req.get("replace") and "already exists" in str(e):
                return fail(req, "exists", str(e))
            return fail(req, "import", str(e))
        reply(req, message=msg)

    def do_remove_toolchain(req):
        reply(req, freed=remove_toolchain(ctx.data_dir))

    def do_shortcut(req):
        try:
            reply(req, path=create_shortcut())
        except (SetupError, OSError) as e:
            fail(req, "shortcut", str(e))

    def do_launch(req):
        st = ctx.state()
        if not st:
            return fail(req, "not_installed", "The game is not installed.")
        launch(st, ctx.data_dir)
        reply(req)

    def do_shaders(req):
        st = ctx.state()
        if not ctx.installed() or not st:
            return fail(req, "not_installed",
                        "Install the game first. Recompile shaders is available after setup finishes.")
        if st.get("placeholder_code"):
            return fail(req, "placeholder", "This build cannot run the game, so shaders cannot be compiled.")
        exe = st.get("exe") or ""
        game_dir = st.get("game_dir") or ctx.game_dir
        if not exe or not os.path.isfile(exe):
            return fail(req, "missing_exe",
                        "The game program is missing.\nUse Repair, then try Recompile shaders again.")
        if not valid_game_folder(game_dir):
            return fail(req, "missing_game",
                        "The game files are missing from %s.\nChoose the game again, then try Recompile shaders." % game_dir)
        work = os.path.join(ctx.data_dir, "work", "shader-warm")
        save = os.path.join(work, "save")
        stop = os.path.join(work, "stop")
        shutil.rmtree(work, ignore_errors=True)
        os.makedirs(save)
        say("  Recompiling shaders.")
        GUI.emit({"event": "shader", "phase": "Starting the game", "done": 0, "total": 0,
                  "detail": "Waiting for the game to draw."})
        last = {"done": -1}

        def on_progress(done, total, detail):
            if done == last["done"]:
                return
            last["done"] = done
            phase = "Compiling shaders" if done else "Starting the game"
            GUI.emit({"event": "shader", "phase": phase, "done": done, "total": total,
                      "detail": detail or "Waiting for the game to draw."})
            if done:
                say("  " + (detail or "%d shaders compiled" % done))

        try:
            code, compiled, cancelled, tail = run_shader_warm(exe, game_dir, save, stop, on_progress)
        except OSError as e:
            return fail(req, "spawn",
                        "Could not start the game to compile shaders.\n%s\n"
                        "Run Setup again and choose Repair, or try Recompile shaders later." % e)
        finally:
            shutil.rmtree(work, ignore_errors=True)
        if cancelled:
            say("  Shader recompile stopped (%d shaders compiled)." % compiled)
            return reply(req, cancelled=True, compiled=compiled)
        if code != 0 or compiled == 0:
            message = shader_warm_failure(code if code is not None else -1, compiled, tail)
            say("  " + message.replace("\n", "\n  "))
            return fail(req, "shaders", message)
        say("  Shader recompile finished (%d shaders compiled)." % compiled)
        reply(req, compiled=compiled)

    handlers = {"probe": probe, "check_keys": check, "install": do_install, "import_save": save,
                "import_existing": do_import_existing, "remove_toolchain": do_remove_toolchain,
                "shortcut": do_shortcut, "launch": do_launch, "shaders": do_shaders,
                "hello": lambda req: hello()}
    hello()
    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        try:
            req = json.loads(line)
        except ValueError:
            GUI.emit({"event": "error", "message": "malformed request"})
            continue
        if req.get("cmd") == "quit":
            break
        if req.get("cmd") == "shaders_cancel":
            continue
        h = handlers.get(req.get("cmd"))
        if not h:
            fail(req, "bad_request", "unknown command")
            continue
        try:
            h(req)
        except Exception as e:
            LOG.write("internal error in %s: %r" % (req.get("cmd"), e))
            fail(req, "internal", "%s: %s" % (type(e).__name__, e))
        del req, line
    return 0

if __name__ == "__main__":
    sys.exit(main())

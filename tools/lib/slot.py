import os
import re
import sys

_SLOT = re.compile(r"^([1-5])$")
_WORLD = re.compile(r"^\d+-\d+$")
_FILE_PACK = re.compile(r"^([1-9])-([1-9]\d*)$")
LOAD_FRAME = 200
_ALWAYS_VALUE = {"save"}


def _course_res_packs():
    packs = [os.path.join("game", "content", "Common", "course_res_pack")]
    here = os.path.dirname(os.path.abspath(__file__))
    sys.path.insert(0, here)
    try:
        import game_versions as gv
        packs.append(os.path.join(gv.active_dir(), "content", "Common", "course_res_pack"))
    except Exception:
        packs.append(
            os.path.join(os.path.dirname(os.path.dirname(here)), "game", "content", "Common", "course_res_pack")
        )
    return packs


def course_file_pack(arg):
    base = os.path.basename(arg.replace("\\", "/"))
    if not base.lower().endswith(".szs"):
        sys.exit(f"course file must end in .szs: {arg}")
    stem = base[:-4]
    match = _FILE_PACK.fullmatch(stem)
    pack_no = int(match.group(2)) if match else 0
    if match is None or pack_no < 1 or pack_no > 256:
        sys.exit(f"course file name must look like 1-27.szs: {base}")
    path = arg
    if "/" not in arg and "\\" not in arg:
        path = None
        for pack in _course_res_packs():
            candidate = os.path.join(pack, base)
            if os.path.isfile(candidate):
                path = candidate
                break
        if path is None:
            path = os.path.join(_course_res_packs()[0], base)
    if not os.path.isfile(path):
        sys.exit(f"missing course file {path}")
    return f"{match.group(1)}-{pack_no}"


def _flag_name(arg):
    if not arg.startswith("--"):
        return None
    if "=" in arg:
        return arg[2:arg.index("=")]
    return arg[2:]


def _consumes_value(flag_name, value):
    if value is None or value.startswith("-"):
        return False
    if flag_name in _ALWAYS_VALUE:
        return True
    if _WORLD.fullmatch(value) and flag_name != "open":
        return False
    return True


def take_args(args):
    slot = None
    pack = None
    rest = []
    i = 0
    while i < len(args):
        arg = args[i]
        fname = _flag_name(arg)
        if fname is not None:
            if "=" in arg:
                rest.append(arg)
                i += 1
                continue
            if i + 1 < len(args) and _consumes_value(fname, args[i + 1]):
                rest.append(arg)
                rest.append(args[i + 1])
                i += 2
                continue
            rest.append(arg)
            i += 1
            continue
        if arg.lower().endswith(".szs") or "/" in arg or "\\" in arg:
            found = course_file_pack(arg)
            if pack is not None:
                sys.exit("pass one course pack, such as 1-1")
            if slot is not None:
                sys.exit("pass a save slot or a course pack, not both")
            pack = found
            i += 1
            continue
        if _WORLD.fullmatch(arg):
            if pack is not None:
                sys.exit("pass one course pack, such as 1-1")
            if slot is not None:
                sys.exit("pass a save slot or a course pack, not both")
            pack = arg
            i += 1
            continue
        match = _SLOT.fullmatch(arg)
        if match:
            if slot is not None:
                sys.exit("pass one save slot, such as 1")
            if pack is not None:
                sys.exit("pass a save slot or a course pack, not both")
            slot = int(match.group(1))
            i += 1
            continue
        rest.append(arg)
        i += 1
    return slot, pack, rest


def take_slot(args):
    slot, _, rest = take_args(args)
    return slot, rest


def state_path(slot, user_dir=None):
    if user_dir:
        directory = os.path.join(user_dir, "states")
    elif sys.platform == "darwin":
        directory = os.path.join(os.path.expanduser("~"), "Library", "Application Support", "nsmbu", "states")
    elif sys.platform == "win32":
        directory = os.path.join(os.environ.get("APPDATA") or ".", "NSMBU", "states")
    else:
        root = os.environ.get("XDG_CONFIG_HOME") or os.path.join(os.path.expanduser("~"), ".config")
        directory = os.path.join(root, "nsmbu", "states")
    return os.path.join(directory, f"slot{slot}.bin")


def load_flag(slot, user_dir=None, frame=LOAD_FRAME):
    path = state_path(slot, user_dir)
    if not os.path.isfile(path):
        sys.exit(f"missing {path}. Save a state in slot {slot}.")
    return "--state-load-at=%d:%d" % (frame, slot)


def apply_slot(env, slot, user_dir=None, frame=LOAD_FRAME):
    return load_flag(slot, user_dir, frame)
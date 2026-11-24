import os
import re
import sys

_SLOT = re.compile(r"^([1-5])$")
_WORLD = re.compile(r"^\d+-\d+$")
_FILE_PACK = re.compile(r"^([1-9])-([1-9]\d*)$")
LOAD_FRAME = 200
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


def take_args(args):
    slot = None
    pack = None
    rest = []
    skip_value = False
    for arg in args:
        if skip_value:
            rest.append(arg)
            skip_value = False
            continue
        if arg == "--save":
            rest.append(arg)
            skip_value = True
            continue
        if arg.startswith("--save="):
            rest.append(arg)
            continue
        if arg.lower().endswith(".szs") or "/" in arg or "\\" in arg:
            found = course_file_pack(arg)
            if pack is not None:
                sys.exit("pass one course pack, such as 1-1")
            if slot is not None:
                sys.exit("pass a save slot or a course pack, not both")
            pack = found
            continue
        if _WORLD.fullmatch(arg):
            if pack is not None:
                sys.exit("pass one course pack, such as 1-1")
            if slot is not None:
                sys.exit("pass a save slot or a course pack, not both")
            pack = arg
            continue
        match = _SLOT.fullmatch(arg)
        if match:
            if slot is not None:
                sys.exit("pass one save slot, such as 1")
            if pack is not None:
                sys.exit("pass a save slot or a course pack, not both")
            slot = int(match.group(1))
            continue
        rest.append(arg)
    return slot, pack, rest


def take_slot(args):
    slot, _, rest = take_args(args)
    return slot, rest


def state_path(slot):
    override = os.environ.get("NSMBU_STATE_DIR")
    if override:
        directory = override
    elif os.environ.get("NSMBU_USER_DIR"):
        directory = os.path.join(os.environ["NSMBU_USER_DIR"], "states")
    elif sys.platform == "darwin":
        directory = os.path.join(os.path.expanduser("~"), "Library", "Application Support", "nsmbu", "states")
    elif sys.platform == "win32":
        directory = os.path.join(os.environ.get("APPDATA") or ".", "NSMBU", "states")
    else:
        root = os.environ.get("XDG_CONFIG_HOME") or os.path.join(os.path.expanduser("~"), ".config")
        directory = os.path.join(root, "nsmbu", "states")
    return os.path.join(directory, f"slot{slot}.bin")


def apply_slot(env, slot):
    path = state_path(slot)
    if not os.path.isfile(path):
        sys.exit(f"missing {path}. Save a state in slot {slot}.")
    env.setdefault("NSMBU_STATE_LOAD_AT", f"{LOAD_FRAME}:{slot}")

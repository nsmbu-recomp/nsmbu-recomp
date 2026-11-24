"""Disc and common keys for dumps under game/.

Keys live in game/keys.txt (Cemu format). Optional WIIU_COMMON_KEY still supplies the
common key. Disc images sit next to keys.txt under game/, named by region.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "installer"))
import setup

IMAGE_SUFFIXES = (".wux", ".wud")

# Title id (no dashes) -> standard disc image basename under game/
STANDARD_IMAGES = {
    "0005000010101d00": "New Super Mario Bros. U (USA).wux",
    "0005000010101e00": "New Super Mario Bros. U (Europe).wux",
}

REGION_TITLE = {
    "usa": "0005000010101d00",
    "eu": "0005000010101e00",
    "europe": "0005000010101e00",
}


def is_image_name(name):
    lower = name.lower()
    return any(lower.endswith(s) for s in IMAGE_SUFFIXES)


def keys_txt_path(game):
    return os.path.join(os.path.abspath(game), "keys.txt")


def region_of_version(version_id):
    """usa / eu from a version folder id, or None."""
    low = (version_id or "").lower().replace("_", "-")
    if low.startswith("nsmbu-nslu-eu") or low.startswith("eu") or "europe" in low:
        return "eu"
    if low.startswith("nsmbu-nslu-usa") or low.startswith("usa"):
        return "usa"
    return None


def title_id_of_region(region):
    return REGION_TITLE.get((region or "").lower())


def standard_image_name(region_or_title):
    """Basename for the disc image of a region (usa/eu) or a 16-hex title id."""
    key = (region_or_title or "").lower().replace("-", "")
    if key in REGION_TITLE:
        key = REGION_TITLE[key]
    name = STANDARD_IMAGES.get(key)
    if not name:
        raise ValueError("unknown region or title for a disc image: %r" % region_or_title)
    return name


def list_loose_images(game):
    """Disc images sitting directly in game/ (not inside a version folder)."""
    if not os.path.isdir(game):
        return []
    found = []
    for name in sorted(os.listdir(game)):
        path = os.path.join(game, name)
        if os.path.isfile(path) and is_image_name(name):
            found.append(path)
    return found


def image_for_version(game, version_id):
    """Path of the root disc image for this version folder, or None."""
    region = region_of_version(version_id)
    if not region:
        return None
    title = title_id_of_region(region)
    wanted = standard_image_name(title).lower()
    stem = os.path.splitext(wanted)[0]
    for path in list_loose_images(game):
        base = os.path.basename(path)
        lower = base.lower()
        if lower == wanted:
            return path
        if os.path.splitext(lower)[0] == stem:
            return path
    return None


def guess_title_id(image):
    """Title id guessed from a standard image name, or None."""
    base = os.path.basename(image).lower()
    for title, name in STANDARD_IMAGES.items():
        stem = os.path.splitext(name)[0].lower()
        if base == name.lower() or os.path.splitext(base)[0] == stem:
            return title
        region = "usa" if "usa" in stem else "europe" if "europe" in stem else None
        if region and region in base:
            return title
    if re.search(r"\(usa\)|\[usa\]", base):
        return "0005000010101d00"
    if re.search(r"\(europe\)|\[europe\]|\(eur\)|\[eur\]", base):
        return "0005000010101e00"
    return None


def _strip_inline_comment(line):
    """Drop a # or // comment that is not inside the key value's only token."""
    for sep in ("#", "//"):
        if sep in line:
            left, _, _ = line.partition(sep)
            if left.strip():
                return left
    return line


def _load_keys_txt(game):
    path = keys_txt_path(game)
    if not os.path.isfile(path):
        return None, None
    data = setup.read_key_data(path)
    if data is None:
        return None, path
    text = setup.decode_key_text(data)
    cleaned = []
    for line in re.split(r"\r\n|[\n\r]", text):
        raw = line.strip().lstrip("\ufeff").strip()
        if not raw or raw.startswith("#") or raw.startswith("//"):
            cleaned.append(line)
            continue
        cleaned.append(_strip_inline_comment(line))
    return setup.keys_in_text("\n".join(cleaned)), path


def _bare_keys_from_text(text):
    """Every bare 32-hex line in a keys.txt (Cemu tries each against the disc)."""
    out = []
    seen = set()
    for line in re.split(r"\r\n|[\n\r]", text):
        line = line.strip().lstrip("\ufeff").strip()
        if not line or line.startswith("#") or line.startswith("//"):
            continue
        if "=" in line or ":" in line:
            continue
        k = setup.parse_key(line)
        if k and k not in seen:
            seen.add(k)
            out.append(k)
    return out


def find_common_key(image=None, game=None):
    """(key_bytes, source_label) for the Wii U common key, or (None, None)."""
    if game:
        game = os.path.abspath(game)
    elif image:
        parent = os.path.dirname(os.path.abspath(image))
        game = parent if os.path.isfile(os.path.join(parent, "keys.txt")) else os.path.dirname(parent)
    else:
        game = None

    env = os.environ.get("WIIU_COMMON_KEY")
    if env:
        key = setup.parse_key(env)
        if key:
            return key, "WIIU_COMMON_KEY"

    if game:
        found, path = _load_keys_txt(game)
        if found and found["common"]:
            return found["common"], path
        if found and found["bare"]:
            return found["bare"], path

    return None, None


def disc_key_candidates(image, game=None):
    """Ordered [(key_bytes, source_label)] to try for this disc image."""
    image = os.path.abspath(image)
    if game:
        game = os.path.abspath(game)
    else:
        game = os.path.dirname(image)

    found, path = _load_keys_txt(game)
    if not found:
        return []

    label = path or "keys.txt"
    ordered = []
    seen = set()

    def add(key, src):
        if key and key not in seen:
            seen.add(key)
            ordered.append((key, src))

    title = guess_title_id(image)
    if title:
        for tid, key in found["title"]:
            if tid == title:
                add(key, "%s (%s)" % (label, tid))
    for tid, key in found["title"]:
        add(key, "%s (%s)" % (label, tid))
    add(found["disc"], "%s (disc=)" % label)

    data = setup.read_key_data(keys_txt_path(game))
    if data is not None:
        for key in _bare_keys_from_text(setup.decode_key_text(data)):
            add(key, label)

    return ordered


def find_disc_key(image, game=None):
    """(key_bytes, source_label) for this disc image, or (None, None).

    Prefers a title-id line that matches the image name; otherwise the first candidate
    from keys.txt. open_disc may try the full candidate list when decryption fails.
    """
    cands = disc_key_candidates(image, game)
    if not cands:
        return None, None
    return cands[0]


def keys_ready(image, game=None):
    """True when both disc and common keys resolve for this image."""
    disc, _ = find_disc_key(image, game)
    common, _ = find_common_key(image, game)
    return bool(disc and common)


def import_loose_image(game, image_path, version_id):
    """Keep a dropped game/*.wux at the game/ root under the standard region name.

    Creates the version folder when missing. Returns the image path used for extract.
    """
    import game_versions as gv

    image_path = os.path.abspath(image_path)
    game = os.path.abspath(game)
    if not os.path.isfile(image_path):
        raise gv.GameVersionsError("missing image: %s" % image_path)
    if os.path.dirname(image_path) != game:
        raise gv.GameVersionsError("image must sit directly in game/: %s" % image_path)
    if not is_image_name(image_path):
        raise gv.GameVersionsError("not a .wux/.wud image: %s" % image_path)

    region = region_of_version(version_id)
    if not region:
        raise gv.GameVersionsError(
            "version id %r does not say usa or eu; use usa-base, eu-1.3.0, …" % version_id
        )

    folder = gv.version_dir(game, version_id)
    if not os.path.isdir(folder):
        gv.create_dump(game, version_id)

    ext = os.path.splitext(image_path)[1].lower()
    dest_name = os.path.splitext(standard_image_name(region))[0] + ext
    dest_image = os.path.join(game, dest_name)
    if os.path.abspath(image_path) != os.path.abspath(dest_image):
        if os.path.lexists(dest_image):
            raise gv.GameVersionsError("already have game/%s; remove it or reuse that dump" % dest_name)
        os.rename(image_path, dest_image)

    return dest_image

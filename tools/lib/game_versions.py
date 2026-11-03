"""Multi-version dumps under game/<id>/; game/active names the selected dump.

Tools resolve the live folder with active_dir() (no overlay symlinks).
"""
import os
import shutil
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "recomp"))
import builds

RESERVED = frozenset({
    "code", "content", "meta", "update", "active", "readme.md",
    "downloads", "keys.txt",
})
LEGACY_NAMES = ("code", "content", "meta", "update", "game.wux", "game.wud", "game.key", "common.key")


class GameVersionsError(Exception):
    pass


def game_root(root=None):
    return os.path.abspath(root or os.path.join(ROOT, "game"))


def active_path(game):
    return os.path.join(game, "active")


def read_active(game):
    path = active_path(game)
    if not os.path.isfile(path):
        return None
    with open(path, encoding="utf-8") as f:
        text = f.read().strip()
    return text or None


def write_active(game, version_id):
    with open(active_path(game), "w", encoding="utf-8") as f:
        f.write(version_id + "\n")


def recommended_id(build):
    return {
        "USA": "usa-1.3.0",
        "USA_V0": "usa-base",
        "EU": "eu-1.3.0",
        "EU_V0": "eu-base",
    }.get(build.name, build.name.lower().replace("_", "-"))


def version_dir(game, version_id):
    return os.path.join(game, version_id)


def rpx_path(folder):
    return os.path.join(folder, "code", "red-pro2.rpx")


def image_path(folder, game=None):
    """Disc image for this version folder: a root game/*.wux matched by region, or None.

    A leftover game.wux inside the version folder is still accepted until cleaned up.
    """
    import game_keys

    if game is None:
        game = os.path.dirname(os.path.abspath(folder))
    version_id = os.path.basename(os.path.abspath(folder))
    root_image = game_keys.image_for_version(game, version_id)
    if root_image:
        return root_image
    for name in ("game.wux", "game.wud"):
        path = os.path.join(folder, name)
        if os.path.isfile(path):
            return path
    return None


def is_version_dir(path):
    return os.path.isdir(path) and os.path.isfile(rpx_path(path))


def is_dump_dir(path, game=None):
    """A version folder with an extracted RPX and/or a matching disc image ready to extract."""
    if not os.path.isdir(path):
        return False
    return os.path.isfile(rpx_path(path)) or image_path(path, game) is not None


def flat_layout(game):
    """True when game/code is a real directory with red-pro2.rpx (legacy single dump)."""
    code = os.path.join(game, "code")
    return os.path.isdir(code) and not os.path.islink(code) and os.path.isfile(rpx_path(game))


def list_versions(game):
    """Extracted dumps only (have code/red-pro2.rpx)."""
    import game_keys

    if not os.path.isdir(game):
        return []
    found = []
    for name in sorted(os.listdir(game)):
        if name.lower() in RESERVED or game_keys.is_image_name(name):
            continue
        path = version_dir(game, name)
        if is_version_dir(path):
            found.append(name)
    return found


def list_dumps(game):
    """Version folders with an RPX and/or a matching root disc image."""
    import game_keys

    if not os.path.isdir(game):
        return []
    found = []
    for name in sorted(os.listdir(game)):
        if name.lower() in RESERVED or game_keys.is_image_name(name):
            continue
        path = version_dir(game, name)
        if is_dump_dir(path, game):
            found.append(name)
    return found


def describe_version(game, version_id):
    import game_keys

    path = version_dir(game, version_id)
    rpx = rpx_path(path)
    image = image_path(path, game)
    has_rpx = os.path.isfile(rpx)
    build = builds.identify(rpx) if has_rpx else None
    if build:
        label = build.issue_label()
    elif has_rpx:
        label = "unknown RPX"
    elif image:
        label = "disc (not extracted)"
    else:
        label = "empty"
    has_keys = bool(image and game_keys.keys_ready(image, game))
    return {
        "id": version_id,
        "path": path,
        "build": build,
        "label": label,
        "extracted": has_rpx,
        "has_wux": image is not None,
        "has_key": has_keys,
        "image": image,
    }


def ensure_version_dirs(folder):
    for name in ("code", "content", "meta"):
        os.makedirs(os.path.join(folder, name), exist_ok=True)


def create_dump(game, version_id):
    """Create game/<id>/ for a new dump. Does not activate."""
    if version_id.lower() in RESERVED:
        raise GameVersionsError("%r is a reserved name" % version_id)
    if not version_id or version_id in (".", "..") or "/" in version_id or "\\" in version_id:
        raise GameVersionsError("bad version id %r" % version_id)
    folder = version_dir(game, version_id)
    if os.path.lexists(folder):
        raise GameVersionsError("game/%s already exists" % version_id)
    os.makedirs(folder)
    ensure_version_dirs(folder)
    return folder


def clear_legacy_overlays(game):
    """Remove leftover top-level overlay symlinks from the old layout."""
    for name in LEGACY_NAMES:
        path = os.path.join(game, name)
        if os.path.islink(path):
            os.unlink(path)


def active_dir(root=None):
    """Absolute path of the dump tools should use (game/<active>/ or legacy flat game/)."""
    game = game_root(root)
    if flat_layout(game):
        return game
    versions = list_versions(game)
    aid = read_active(game)
    if aid:
        path = version_dir(game, aid)
        if os.path.isdir(path):
            return path
        raise GameVersionsError("game/active is %r but game/%s is missing; run nsmbu game" % (aid, aid))
    if len(versions) == 1:
        return version_dir(game, versions[0])
    if not versions:
        return game
    raise GameVersionsError(
        "no active dump; run nsmbu game (have: %s)" % ", ".join(versions)
    )


def activate(game, version_id):
    """Record game/active. Does not run recomp."""
    if version_id.lower() in RESERVED:
        raise GameVersionsError("%r is a reserved name" % version_id)
    folder = version_dir(game, version_id)
    if not os.path.isdir(folder):
        raise GameVersionsError("missing version folder: game/%s" % version_id)
    ensure_version_dirs(folder)
    clear_legacy_overlays(game)
    write_active(game, version_id)
    return describe_version(game, version_id)


def migrate_flat(game, version_id=None):
    """Move a legacy flat game/{code,content,meta,…} into game/<id>/ then activate it."""
    if not flat_layout(game):
        raise GameVersionsError("no flat game/code/red-pro2.rpx to migrate")
    dumps = list_dumps(game)
    if dumps:
        raise GameVersionsError(
            "game/ has both a flat code/ tree and version folders (%s); "
            "move or remove one layout before switching"
            % ", ".join(dumps)
        )
    build = builds.identify(rpx_path(game))
    if version_id is None:
        version_id = recommended_id(build) if build else "usa-1.3.0"
    if version_id.lower() in RESERVED:
        raise GameVersionsError("%r is a reserved name" % version_id)
    dest = version_dir(game, version_id)
    if os.path.lexists(dest):
        raise GameVersionsError("game/%s already exists" % version_id)
    os.makedirs(dest)
    for name in LEGACY_NAMES:
        src = os.path.join(game, name)
        if not os.path.lexists(src):
            continue
        if os.path.islink(src):
            raise GameVersionsError("game/%s is a symlink; cannot migrate" % name)
        shutil.move(src, os.path.join(dest, name))
    return activate(game, version_id)


def resolve_for_select(game, version_id=None, interactive=False, ask_migrate=None):
    """Pick and activate a version. ask_migrate() -> bool when a flat dump can be migrated."""
    dumps = list_dumps(game)
    extracted = list_versions(game)
    flat = flat_layout(game)

    if flat and dumps:
        raise GameVersionsError(
            "game/ has both a flat code/ tree and version folders (%s); "
            "move the flat files into a version folder (or remove the folders) then retry"
            % ", ".join(dumps)
        )

    if flat and not dumps:
        if version_id is not None:
            return migrate_flat(game, version_id)
        if not interactive:
            raise GameVersionsError(
                "game/ still uses a flat code/ layout; run `nsmbu game` in a terminal to migrate, "
                "or `nsmbu game <id>` to migrate into that folder"
            )
        if ask_migrate is None or not ask_migrate():
            raise GameVersionsError("migration cancelled")
        return migrate_flat(game)

    if not dumps:
        raise GameVersionsError(
            "no version folders under game/ "
            "(drop a .wux into game/ and run nsmbu game, or add game/usa-1.3.0/ with code/red-pro2.rpx)"
        )

    if version_id is not None:
        folder = version_dir(game, version_id)
        if version_id not in dumps and not os.path.isdir(folder):
            raise GameVersionsError(
                "unknown version %r (have: %s)" % (version_id, ", ".join(dumps) or "(none)")
            )
        return activate(game, version_id)

    if not interactive:
        raise GameVersionsError(
            "usage: nsmbu game <id>  (non-interactive; ids: %s)"
            % ", ".join(dumps or extracted)
        )

    return None

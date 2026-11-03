#!/usr/bin/env python3
"""Activate / create / recomp game dumps (walkthrough UI lives in the nsmbu CLI)."""
import json
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "tools", "lib"))
import game_versions as gv


def say(msg=""):
    print(msg)


def run_recomp(dump):
    rpx = gv.rpx_path(dump)
    if not os.path.isfile(rpx):
        say("no %s yet; extract the dump first" % os.path.relpath(rpx, ROOT))
        return False
    gen = os.path.join(ROOT, "build", "gen")
    os.makedirs(gen, exist_ok=True)
    say("recompiling %s -> build/gen ..." % os.path.relpath(rpx, ROOT))
    subprocess.run(
        [sys.executable, os.path.join(ROOT, "recomp", "recomp.py"), rpx, gen],
        check=True,
        cwd=ROOT,
    )
    return True


def list_state(game):
    import game_keys

    active = gv.read_active(game)
    versions = []
    for version_id in gv.list_dumps(game):
        info = gv.describe_version(game, version_id)
        versions.append(
            {
                "id": version_id,
                "label": info["label"],
                "active": version_id == active,
                "extracted": info["extracted"],
                "has_wux": info["has_wux"],
                "has_key": info["has_key"],
            }
        )
    loose = []
    for path in game_keys.list_loose_images(game):
        loose.append(
            {
                "path": path,
                "name": os.path.basename(path),
                "has_key": game_keys.keys_ready(path, game),
                "has_common": game_keys.find_common_key(path, game)[0] is not None,
                "title_id": game_keys.guess_title_id(path),
            }
        )
    common, common_src = game_keys.find_common_key(game=game)
    return {
        "flat": gv.flat_layout(game),
        "active": active,
        "versions": versions,
        "loose_images": loose,
        "has_common_key": common is not None,
        "common_key_src": common_src,
    }


def main(argv=None):
    argv = list(sys.argv[1:] if argv is None else argv)
    game = gv.game_root()
    os.makedirs(game, exist_ok=True)

    if argv == ["--list"]:
        print(json.dumps(list_state(game)))
        return 0

    try:
        if argv and argv[0] == "--create":
            if len(argv) != 2 or argv[1].startswith("-"):
                sys.exit("usage: game_select.py --create <id>")
            path = gv.create_dump(game, argv[1])
            print(path)
            return 0

        if argv and argv[0] == "--import":
            import game_keys

            if len(argv) != 3 or argv[1].startswith("-") or argv[2].startswith("-"):
                sys.exit("usage: game_select.py --import <image> <id>")
            image = os.path.abspath(argv[1])
            version_id = argv[2]
            dest = game_keys.import_loose_image(game, image, version_id)
            selected = gv.activate(game, version_id)
            say("imported %s -> game/%s" % (os.path.basename(image), os.path.basename(dest)))
            say("active: game/%s (%s)" % (selected["id"], selected["label"]))
            print(dest)
            return 0

        if argv and argv[0] == "--activate":
            if len(argv) != 2 or argv[1].startswith("-"):
                sys.exit("usage: game_select.py --activate <id>")
            selected = gv.activate(game, argv[1])
            say("active: game/%s (%s)" % (selected["id"], selected["label"]))
            return 0

        if argv and argv[0] == "--migrate":
            version_id = argv[1] if len(argv) > 1 else None
            if version_id and version_id.startswith("-"):
                sys.exit("usage: game_select.py --migrate [id]")
            selected = gv.migrate_flat(game, version_id)
            say("active: game/%s (%s)" % (selected["id"], selected["label"]))
            return 0

        if argv == ["--recomp"]:
            dump = gv.active_dir(game)
            if not run_recomp(dump):
                return 1
            say("recomp done; run `nsmbu build` if the binary must match the new gen")
            return 0

        if len(argv) == 1 and not argv[0].startswith("-"):
            selected = gv.resolve_for_select(game, version_id=argv[0], interactive=False)
            if selected is None:
                raise gv.GameVersionsError("usage: nsmbu game <id>")
            say("active: game/%s (%s)" % (selected["id"], selected["label"]))
            return 0

        if argv:
            sys.exit(
                "usage: game_select.py --list | --create <id> | --import <image> <id> | "
                "--activate <id> | --migrate [id] | --recomp | <id>"
            )
        sys.exit("usage: nsmbu game [id]")
    except gv.GameVersionsError as e:
        sys.exit(str(e))
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except subprocess.CalledProcessError as e:
        sys.exit(e.returncode or 1)

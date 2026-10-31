#!/usr/bin/env python3
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools", "recomp"))
sys.path.insert(0, os.path.join(ROOT, "tools", "lib"))
import builds
import game_dump
import game_keys
import game_versions as gv


def check_update(game):
    update = game_dump.update_root(game)
    known = ", ".join("%s %s (v%d)" % (b.name, b.title_id, b.title_version) for b in builds.all_builds())
    print("game: %s" % game)
    print("known builds: %s" % known)
    print("update folder: %s/ (optional for a known base-disc RPX)" % game_dump.UPDATE_DIRNAME)
    print("")
    print("== update (1.3.0) ==")
    errors, rpx = game_dump.folder_layout_errors(update, game_dump.UPDATE_DIRNAME)
    if not os.path.isdir(update):
        print("no update folder; will use the base disc if its RPX is a known build")
        return False
    if rpx:
        e, _ = game_dump.check_rpx(
            rpx,
            "%s/code/red-pro2.rpx" % game_dump.UPDATE_DIRNAME,
            require_update_title=True,
        )
        errors.extend(e)
    if errors:
        print("")
        print("check failed:")
        for e in errors:
            print("  %s" % e)
        sys.exit(
            "fix game/%s/ (code/, content/, meta/) or remove it to use a known base disc alone"
            % game_dump.UPDATE_DIRNAME
        )
    print("")
    print("update check ok")
    return True


def nsmbu_extract_path(root):
    name = "nsmbu-extract.exe" if sys.platform == "win32" else "nsmbu-extract"
    for sub in ("debug", "release", ""):
        base = os.path.join(root, "build", sub) if sub else os.path.join(root, "build")
        path = os.path.join(base, name)
        if os.path.isfile(path):
            return path
    sys.exit("%s not found under build/; run `nsmbu build` first" % name)


def run_disc_extract(image, disc, common, out):
    blob = ("disc %s\ncommon %s\n" % (disc.hex(), common.hex())).encode()
    subprocess.run(
        [nsmbu_extract_path(ROOT), "--keys-stdin", "extract", image, out],
        input=blob,
        check=True,
        cwd=ROOT,
    )


def main():
    game = game_dump.game_root()
    root = gv.game_root()
    image = gv.image_path(game, root)
    if not image:
        sys.exit(
            "no disc image for the active dump (%s); put "
            "`New Super Mario Bros. U (USA).wux` or `(Europe).wux` under game/ with keys.txt"
            % game
        )

    disc, disc_src = game_keys.find_disc_key(image, root)
    common, common_src = game_keys.find_common_key(image, root)
    if not disc:
        sys.exit(
            "disc key not found for %s; add a title-id or disc= line in game/keys.txt" % image
        )
    if not common:
        sys.exit(
            "Wii U common key not found; set WIIU_COMMON_KEY or put common= in game/keys.txt"
        )
    print("disc image: %s" % os.path.relpath(image, root))
    print("disc key: %s" % (disc_src or "?"))
    print("common key: %s" % (common_src or "?"))

    have_update = check_update(game)

    print("")
    print("extracting base disc into %s ..." % game)
    run_disc_extract(image, disc, common, game)

    if have_update:
        print("applying %s onto %s ..." % (game_dump.UPDATE_DIRNAME, game))
        game_dump.apply_update(game)

    rpx = os.path.join(game, "code", "red-pro2.rpx")
    digest = builds.file_sha256(rpx)
    build = builds.by_sha256(digest)
    if not build:
        known = ", ".join("%s %s..." % (b.name, b.sha256[:16]) for b in builds.all_builds())
        sys.exit(
            "code/red-pro2.rpx is %s... (need one of: %s)%s"
            % (
                digest[:16],
                known,
                "" if have_update else "; put the matching 1.3.0 update under game/%s/" % game_dump.UPDATE_DIRNAME,
            )
        )
    if have_update:
        print("extract ok: base disc + %s -> %s (title version %d)" % (
            game_dump.UPDATE_DIRNAME, build.name, build.title_version))
    else:
        print("extract ok: base disc -> %s (title version %d)" % (build.name, build.title_version))


if __name__ == "__main__":
    main()

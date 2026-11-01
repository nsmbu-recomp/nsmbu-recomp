#!/usr/bin/env python3
import os
import re
import shutil
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "recomp"))
sys.path.insert(0, os.path.join(ROOT, "tools", "lib"))
import builds
import game_versions as gv

UPDATE_DIRNAME = "update"


def game_root(path=None):
    """Active dump folder (game/<id>/), or `path` when given."""
    if path:
        return os.path.abspath(path)
    return gv.active_dir()


def update_root(game):
    return os.path.join(game, UPDATE_DIRNAME)


def expected_title_version(title, build):
    if build:
        return build.title_version
    if not title:
        return None
    base = "00050000" + title[8:] if title[:8] in ("00050000", "0005000e") else title
    other = builds.by_title(base)
    return other.title_version if other else None


def read_title_version(folder):
    for rel, base in (("code/app.xml", 16), ("meta/meta.xml", 10)):
        path = os.path.join(folder, *rel.split("/"))
        try:
            with open(path, "rb") as f:
                data = f.read(65536)
        except OSError:
            continue
        tid = re.search(rb"<title_id[^>]*>\s*([0-9A-Fa-f]{16})\s*<", data)
        ver = re.search(rb"<title_version[^>]*>\s*([0-9A-Fa-f]+)\s*<", data)
        title = tid.group(1).decode().lower() if tid else None
        version = None
        if ver:
            try:
                version = int(ver.group(1), base)
            except ValueError:
                version = None
        if title is not None or version is not None:
            return title, version, rel
    return None, None, None


def title_matches_build(title, build):
    if not title:
        return True
    if title == build.title_id:
        return True
    if title == "0005000e" + build.title_id[8:]:
        return True
    return False


def folder_layout_errors(folder, label):
    errors = []
    rpx = os.path.join(folder, "code", "red-pro2.rpx")
    content = os.path.join(folder, "content")
    meta = os.path.join(folder, "meta", "meta.xml")
    if not os.path.isdir(folder):
        errors.append("missing folder: %s" % label)
        return errors, None
    if not os.path.isfile(rpx):
        errors.append("missing file: %s/code/red-pro2.rpx" % label)
    if not os.path.isdir(content):
        errors.append("missing folder: %s/content/" % label)
    if not os.path.isfile(meta):
        errors.append("missing file: %s/meta/meta.xml" % label)
    return errors, rpx if os.path.isfile(rpx) else None


def check_rpx(rpx_path, label, require_update_title=False):
    errors = []
    digest = builds.file_sha256(rpx_path)
    build = builds.by_sha256(digest)
    title, version, where = read_title_version(os.path.dirname(os.path.dirname(rpx_path)))
    print("%s: size %d  sha256 %s" % (label, os.path.getsize(rpx_path), digest))
    if title is not None or version is not None:
        print("  metadata (%s): title %s  version %s" % (
            where or "?",
            title or "?",
            version if version is not None else "?",
        ))
    if not build:
        known = ", ".join("%s %s..." % (b.name, b.sha256[:16]) for b in builds.all_builds())
        errors.append(
            "%s is not a known RPX (sha256 %s...; need one of: %s)"
            % (label, digest[:16], known)
        )
        return errors, None
    print("  match: %s (title %s, version %d)" % (build.name, build.title_id, build.title_version))
    if not title_matches_build(title, build):
        errors.append(
            "title id %s in %s does not match build %s (%s)"
            % (title, where, build.name, build.title_id)
        )
    if require_update_title:
        update_tid = "0005000e" + build.title_id[8:]
        if title and title != update_tid and title != build.title_id:
            errors.append(
                "%s title id must be %s or %s (got %s)"
                % (label, update_tid, build.title_id, title)
            )
        need = expected_title_version(title, build)
        if version is not None and need is not None and version != need:
            errors.append(
                "%s title version must be %d (got %s)"
                % (label, need, version)
            )
    return errors, build


def apply_update(game):
    src = update_root(game)
    for name in ("code", "content", "meta"):
        src_part = os.path.join(src, name)
        dst_part = os.path.join(game, name)
        if not os.path.isdir(src_part):
            raise SystemExit("update is incomplete: missing %s/%s" % (UPDATE_DIRNAME, name))
        os.makedirs(dst_part, exist_ok=True)
        shutil.copytree(src_part, dst_part, dirs_exist_ok=True)

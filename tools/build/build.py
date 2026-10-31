#!/usr/bin/env python3
import argparse
import glob
import os
import re
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "lib"))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "recomp"))
import builds
import dev_game_icon
import game_versions as gv
import slot

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

def default_build_dir(release, no_debug):
    if release or no_debug:
        return os.path.join(ROOT, "build", "release")
    return os.path.join(ROOT, "build", "debug")

def debug_compile_flags(install_flags, sanitizer=False):
    flags = install_flags
    if sanitizer:
        flags = f"{flags} -fsanitize=address,undefined -fno-omit-frame-pointer"
    return flags

def debug_link_flags(link_dir, sanitizer=False):
    flags = f"-L{link_dir}"
    if sanitizer:
        flags = f"{flags} -fsanitize=address,undefined"
    return flags

def libstdcxx_libdir():
    for ver in range(20, 10, -1):
        hits = glob.glob(f"/usr/lib/gcc/*-linux-gnu/{ver}/libstdc++.so")
        if hits:
            return os.path.dirname(hits[0])
    return None

def gcc_install_flags(libdir):
    return [f"--gcc-install-dir={libdir}"]

def clang_pair(libdir):
    cxx_flags = gcc_install_flags(libdir)
    os.makedirs(os.path.join(ROOT, ".tmp"), exist_ok=True)
    for base in ("clang-19", "clang-18", "clang-17", "clang"):
        cc = shutil.which(base)
        if not cc:
            continue
        if base == "clang":
            cxx = shutil.which("clang++")
        else:
            cxx = shutil.which(base.replace("clang", "clang++", 1))
        if not cxx:
            continue
        with tempfile.TemporaryDirectory(dir=os.path.join(ROOT, ".tmp")) as tmp:
            src = os.path.join(tmp, "t.cpp")
            exe = os.path.join(tmp, "t")
            with open(src, "w", encoding="utf-8") as handle:
                handle.write("int main() { return 0; }\n")
            link = ["-L" + libdir, "-lstdc++"] if libdir else []
            r = subprocess.run(
                [cxx, *cxx_flags, src, "-o", exe, *link],
                capture_output=True,
                text=True,
            )
            if r.returncode == 0:
                return cc, cxx
    return None, None

def gen_is_stub(gen):
    report = os.path.join(gen, "report.txt")
    if not os.path.isfile(report):
        return True
    with open(report, encoding="utf-8", errors="replace") as handle:
        return handle.readline().startswith("placeholder from stubgen.py")

def gen_sources_newer(table, sources):
    if not os.path.isfile(table):
        return True
    table_mtime = os.path.getmtime(table)
    for path in sources:
        try:
            newer = os.path.getmtime(path) > table_mtime
        except OSError:
            continue
        if newer:
            return True
    return False

def link_compile_commands(build_dir):
    src = os.path.join(build_dir, "compile_commands.json")
    if not os.path.isfile(src):
        return
    tmp = os.path.join(ROOT, ".tmp")
    os.makedirs(tmp, exist_ok=True)
    dst = os.path.join(tmp, "compile_commands.json")
    if os.path.lexists(dst):
        os.remove(dst)
    os.symlink(os.path.relpath(src, tmp), dst)

def cmake_cache_get(cache_path, key):
    needle = f"{key}:"
    try:
        with open(cache_path, encoding="utf-8", errors="replace") as handle:
            for line in handle:
                if line.startswith(needle):
                    return line.split("=", 1)[1].strip()
    except OSError:
        return None
    return None

def cache_value_matches(key, have, want):
    if have is None:
        return False
    if key.endswith("_COMPILER"):
        try:
            return os.path.realpath(have) == os.path.realpath(want)
        except OSError:
            pass
    return have == want

def cmake_define_args(extra, bundled_default, setup_gui):
    defines = {}
    if bundled_default and not any(a.startswith("-DNSMBU_BUNDLED_DEPS") for a in extra):
        defines["NSMBU_BUNDLED_DEPS"] = "ON"
    if setup_gui and not any(a.startswith("-DNSMBU_SETUP_GUI") for a in extra):
        defines["NSMBU_SETUP_GUI"] = "ON"
    for arg in extra:
        if arg.startswith("-D") and "=" in arg[2:]:
            name, value = arg[2:].split("=", 1)
            defines[name] = value
    return defines

def cmake_configure_needed(build_dir, source_dir, cache_entries, define_entries):
    cache = os.path.join(build_dir, "CMakeCache.txt")
    if not os.path.isfile(cache):
        return True
    home = cmake_cache_get(cache, "CMAKE_HOME_DIRECTORY")
    if home is None or os.path.normpath(home) != os.path.normpath(source_dir):
        return True
    for key, want in cache_entries.items():
        if not cache_value_matches(key, cmake_cache_get(cache, key), want):
            return True
    for key, want in define_entries.items():
        if not cache_value_matches(key, cmake_cache_get(cache, key), want):
            return True
    return False

def run_cmake(argv, cwd):
    cmd = list(argv)
    if os.environ.get("NSMBU_VERBOSE_BUILD") != "1":
        cmd.insert(1, "--log-level=WARNING")
    subprocess.run(cmd, check=True, cwd=cwd)

def gen_build_name(gen):
    table = os.path.join(gen, "table.c")
    if not os.path.isfile(table):
        return None
    with open(table, encoding="utf-8", errors="replace") as f:
        text = f.read(4096)
    m = re.search(r'g_guest_build_name\[\] = "([^"]+)"', text)
    return m.group(1) if m else None


def ensure_gen_dir():
    gen = os.path.join(ROOT, "build", "gen")
    try:
        rpx = os.path.join(gv.active_dir(), "code", "red-pro2.rpx")
    except gv.GameVersionsError:
        rpx = ""
    os.makedirs(gen, exist_ok=True)
    if rpx and os.path.isfile(rpx):
        table = os.path.join(gen, "table.c")
        build = builds.identify(rpx) if os.path.isfile(rpx) else None
        want = build.name if build else None
        have = gen_build_name(gen)
        stale = (
            gen_is_stub(gen)
            or not os.path.isfile(table)
            or os.path.getmtime(rpx) > os.path.getmtime(table)
            or (want is not None and have is not None and want != have)
            or gen_sources_newer(table, builds.hook_files())
        )
        if stale:
            subprocess.run(
                [sys.executable, os.path.join(ROOT, "tools", "recomp", "recomp.py"), rpx, gen],
                check=True,
                cwd=ROOT,
            )
        return
    if glob.glob(os.path.join(gen, "code_*.c")) and not gen_is_stub(gen):
        return
    subprocess.run(
        [sys.executable, os.path.join(ROOT, "tools", "recomp", "stubgen.py"), gen],
        check=True,
        cwd=ROOT,
    )

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("-j", "--jobs", type=int, default=os.cpu_count() or 4)
    parser.add_argument("--release", action="store_true")
    parser.add_argument("--no-debug", action="store_true")
    parser.add_argument("--sanitizer", action="store_true")
    parser.add_argument("--target", default="nsmbu", help="CMake target to build (default: nsmbu)")
    parser.add_argument(
        "--dir",
        default=None,
        help="CMake build directory (default: build/debug; build/release with --release or --no-debug)",
    )
    parser.add_argument("cmake_args", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    _, args.cmake_args = slot.take_slot(args.cmake_args)
    if args.cmake_args and args.cmake_args[0] == "--":
        args.cmake_args = args.cmake_args[1:]
    build_dir = (
        os.path.abspath(args.dir)
        if args.dir
        else default_build_dir(args.release, args.no_debug)
    )
    debug_build = not args.release and not args.no_debug
    ensure_gen_dir()
    if debug_build:
        dev_game_icon.refresh_android_local_icon()
    libdir = libstdcxx_libdir()
    if not libdir:
        sys.exit(
            "build needs libstdc++ for Clang (install libstdc++-14-dev or another gcc libstdc++ dev package)"
        )
    cc, cxx = clang_pair(libdir)
    if not cc:
        sys.exit("build needs a working clang++ on PATH (clang-18 or clang)")
    install = " ".join(gcc_install_flags(libdir))
    compile_flags = debug_compile_flags(install, args.sanitizer)
    link_flags = debug_link_flags(libdir, args.sanitizer)
    cmake = [
        "cmake",
        "-S",
        ROOT,
        "-B",
        build_dir,
        f"-DCMAKE_C_COMPILER={cc}",
        f"-DCMAKE_CXX_COMPILER={cxx}",
        f"-DCMAKE_C_FLAGS={compile_flags}",
        f"-DCMAKE_CXX_FLAGS={compile_flags}",
        f"-DCMAKE_EXE_LINKER_FLAGS={link_flags}",
        f"-DCMAKE_SHARED_LINKER_FLAGS={link_flags}",
        f"-DCMAKE_MODULE_LINKER_FLAGS={link_flags}",
    ]
    if args.release:
        cmake.append("-DCMAKE_BUILD_TYPE=Release")
    elif debug_build:
        cmake.append("-DCMAKE_BUILD_TYPE=Debug")
    else:
        cmake.append("-DCMAKE_BUILD_TYPE=RelWithDebInfo")
    extra = list(args.cmake_args)
    if not any(a.startswith("-DNSMBU_BUNDLED_DEPS") for a in extra):
        cmake.append("-DNSMBU_BUNDLED_DEPS=ON")
    if args.target == "nsmbu-setup" and not any(a.startswith("-DNSMBU_SETUP_GUI") for a in extra):
        cmake.append("-DNSMBU_SETUP_GUI=ON")
    cmake.extend(extra)
    build_type = "Release" if args.release else "Debug" if debug_build else "RelWithDebInfo"
    cache_entries = {
        "CMAKE_C_COMPILER": cc,
        "CMAKE_CXX_COMPILER": cxx,
        "CMAKE_C_FLAGS": compile_flags,
        "CMAKE_CXX_FLAGS": compile_flags,
        "CMAKE_EXE_LINKER_FLAGS": link_flags,
        "CMAKE_SHARED_LINKER_FLAGS": link_flags,
        "CMAKE_MODULE_LINKER_FLAGS": link_flags,
        "CMAKE_BUILD_TYPE": build_type,
    }
    define_entries = cmake_define_args(
        extra,
        bundled_default=not any(a.startswith("-DNSMBU_BUNDLED_DEPS") for a in extra),
        setup_gui=args.target == "nsmbu-setup"
        and not any(a.startswith("-DNSMBU_SETUP_GUI") for a in extra),
    )
    if cmake_configure_needed(build_dir, ROOT, cache_entries, define_entries):
        run_cmake(cmake, ROOT)
    link_compile_commands(build_dir)
    subprocess.run(
        ["cmake", "--build", build_dir, "--target", args.target, "-j", str(args.jobs)],
        check=True,
        cwd=ROOT,
    )

if __name__ == "__main__":
    main()

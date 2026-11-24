"""Durable setup worker entry point for the Android service.

The host supplies validated, installed tools and privately imported inputs. This
worker builds a candidate; only AndroidGame may validate/load and activate it.
"""
import argparse
import fcntl
import json
import os
from pathlib import Path
import resource
import sys
import threading
import time
from types import SimpleNamespace

from setup_adapter import Adapter, Paused, atomic_json

KEY_FILE_LIMIT = 4096


def private_input(root, name):
    path = (root / name).resolve()
    if not path.is_relative_to(root) or path == root:
        raise ValueError("Imported input must remain inside the setup job")
    return path


def process_resources():
    """Kernel high-water marks for this worker and its reaped native children.

    These are process-lifetime maxima, not a sum or a per-job concurrent peak.
    Android/Linux report KiB; macOS reports bytes for host verification.
    """
    own = resource.getrusage(resource.RUSAGE_SELF)
    children = resource.getrusage(resource.RUSAGE_CHILDREN)
    scale = 1 if sys.platform == "darwin" else 1024
    return {"scope": "process lifetime; reaped children only; maxima are not additive",
            "worker_peak_rss_bytes": int(own.ru_maxrss * scale),
            "largest_reaped_child_peak_rss_bytes": int(children.ru_maxrss * scale),
            "worker_cpu_seconds": own.ru_utime + own.ru_stime,
            "reaped_children_cpu_seconds": children.ru_utime + children.ru_stime}


def image_keys(setup, files):
    """The disc and common keys from key file bytes; one Cemu keys.txt may name both."""
    keys = setup.Keys()
    for name, data in files.items():
        value = setup.key_from_data(data, name) if len(data) <= KEY_FILE_LIMIT else None
        if not value:
            raise ValueError("Invalid " + name + " key file: " + setup.key_data_problem(data, name))
        setattr(keys, name, value)
    for name in ("disc", "common"):
        if getattr(keys, name) is None:
            value = next((found for found in (setup.named_key_from_data(other, name)
                                              for other in files.values()) if found), None)
            if not value:
                what = "Wii U common key" if name == "common" else "disc key"
                raise ValueError("The " + what + " is needed: choose its key file")
            setattr(keys, name, value)
    return keys


def run(configuration, package=None, native_run=None, adapter_factory=Adapter, key_files=None):
    configuration = Path(configuration).resolve()
    root = configuration.parent
    package = Path(package).resolve() if package else Path(__file__).resolve().parents[2]
    with (root / "worker.lock").open("a+b") as lock:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            raise RuntimeError("This setup job is already running") from None
        state_lock = threading.Lock()
        state = {"schema": 1, "state": "running", "started": time.time(),
                 "resources_before": process_resources()}

        def publish(event):
            with state_lock:
                state["last_event"] = event
                state["resources_latest"] = process_resources()
                if event.get("stage") and event.get("state") in ("complete", "reused") and "source" not in event:
                    state.setdefault("stages", {})[event["stage"]] = {
                        **event, "resources": state["resources_latest"]}
                state["updated"] = time.time()
                atomic_json(root / "state.json", state)

        def paused():
            return (root / "pause.json").exists() or (root / "manual-pause.json").exists()

        try:
            job = json.loads(configuration.read_text())
            if job.get("schema") != 1:
                raise ValueError("Unsupported setup job version")
            jobs = job.get("jobs", 1)
            if type(jobs) is not int or not 1 <= jobs <= 4:
                raise ValueError("Compilation concurrency must be one to four")
            if not isinstance(job.get("port_revision"), str) or not job["port_revision"]:
                raise ValueError("A release pipeline identity is required")
            adapter = adapter_factory(package, root / "checkpoints", job["port_revision"], publish, paused)
            adapter.boundary()
            source = job["input"]
            path = private_input(root, source["path"])
            update = private_input(root, source["update"]) if source.get("update") else None
            if source["kind"] == "folder":
                if not adapter.setup.valid_game_folder(str(path)):
                    missing = [name for name, present in (
                        ("code/red-pro2.rpx", (path / "code" / "red-pro2.rpx").is_file()),
                        ("content", (path / "content").is_dir()),
                        ("meta/meta.xml", (path / "meta" / "meta.xml").is_file())) if not present]
                    raise ValueError("Imported game folder is incomplete (missing " + ", ".join(missing) +
                                     "). Choose the folder that contains code, content and meta.")
                game = adapter.prepare_folder(path, update)
            elif source["kind"] in ("archive", "image"):
                extractor = job["extractor"]
                keys = None
                if source["kind"] == "image":
                    files = {name: value for name, value in (key_files or {}).items() if value is not None}
                    for name in ("disc", "common"):
                        if name not in files and source.get(name + "_key"):
                            files[name] = private_input(root, source[name + "_key"]).read_bytes()[:KEY_FILE_LIMIT + 1]
                    keys = image_keys(adapter.setup, files)
                    files = None
                if native_run is None:
                    import nsmbu_native
                    native_run = nsmbu_native.run
                game = adapter.extract(path, source["kind"], extractor["path"],
                                       extractor["identity"], native_run, keys, update)
                if source["kind"] == "image":
                    keys = None
                    if key_files is not None:
                        key_files.clear()
            else:
                raise ValueError("Unsupported imported input kind")
            generated = adapter.translate(game)
            compiler = job["compiler"]
            toolchain = SimpleNamespace(cc=compiler["cc"], cxx=compiler["cxx"], ar=compiler["ar"],
                                        env=compiler.get("env"), rsp=compiler.get("rsp", True))
            manifest = json.loads((package / "sdk/manifest.json").read_text())
            if native_run is None:
                import nsmbu_native
                native_run = nsmbu_native.run
            objects = adapter.compile(toolchain, manifest, generated, compiler["identity"], native_run, jobs=jobs)
            library = adapter.link(toolchain, manifest, objects, compiler["identity"], native_run)
            adapter.boundary()
            result = {"schema": 1, "library": str(library), "game": str(game),
                      "port_revision": job["port_revision"], "compiler_identity": compiler["identity"]}
            atomic_json(root / "ready.json", result)
            state["state"] = "ready_to_activate"
            publish({"stage": "activate", "state": "ready"})
            return result
        except Paused:
            state["state"] = "paused"
            publish({"state": "paused", "restart_incomplete_stage": True})
            return None
        except Exception as failure:
            state["state"] = "failed"
            state["error"] = str(failure)[:2048]
            publish({"state": "failed"})
            raise


def read_key_descriptors(descriptors):
    """Read SAF regular files or pipes into bounded memory; never create a key copy."""
    from contextlib import ExitStack
    with ExitStack() as stack:
        files = {}
        for name, fd in descriptors.items():
            if fd is not None:
                stream = stack.enter_context(os.fdopen(os.dup(fd), "rb"))
                data = stream.read(KEY_FILE_LIMIT + 1)
                if len(data) > KEY_FILE_LIMIT:
                    raise ValueError("The selected %s key file is larger than %d bytes." % (name, KEY_FILE_LIMIT))
                files[name] = data
        return files


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--job", required=True, type=Path)
    parser.add_argument("--disc-key-fd", type=int)
    parser.add_argument("--common-key-fd", type=int)
    args = parser.parse_args(argv)
    return run(args.job, key_files=read_key_descriptors({
        "disc": args.disc_key_fd, "common": args.common_key_fd}))

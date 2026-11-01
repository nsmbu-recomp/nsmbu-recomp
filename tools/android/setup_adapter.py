"""Durable Android setup stages around the unchanged desktop installer.

One dedicated embedded-Python worker owns an Adapter. Native extraction/compiler
execution and the Android service/UI are supplied separately; this module never
selects a desktop toolchain and never invokes the installer's destructive install().
"""
from contextlib import contextmanager, redirect_stdout, redirect_stderr
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import runpy
import shutil
import sys
import time
import threading
from types import SimpleNamespace
import uuid
from native_process import ProcessBridge


class Paused(Exception):
    """The host requested a pause at a durable stage boundary."""


def digest(path, checkpoint=lambda: None):
    result = hashlib.sha256()
    with Path(path).open("rb") as stream:
        while True:
            checkpoint()
            block = stream.read(1024 * 1024)
            if not block:
                break
            result.update(block)
    return result.hexdigest()


def inventory(root, checkpoint=lambda: None):
    return {str(path.relative_to(root)): digest(path, checkpoint)
            for path in sorted(root.rglob("*")) if path.is_file()}


def atomic_json(path, value):
    temporary = path.with_suffix(".tmp")
    try:
        with temporary.open("w", encoding="utf-8") as stream:
            json.dump(value, stream, sort_keys=True)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
        directory = os.open(path.parent, os.O_RDONLY)
        try:
            os.fsync(directory)
        finally:
            os.close(directory)
    finally:
        temporary.unlink(missing_ok=True)


class Adapter:
    def __init__(self, package, storage, port_revision, emit=lambda event: None,
                 pause_requested=lambda: False):
        self.package = Path(package).resolve()
        self.storage = Path(storage).resolve()
        self.storage.mkdir(parents=True, exist_ok=True)
        self.port_revision = port_revision
        self.emit = emit
        self.pause_requested = pause_requested
        spec = importlib.util.spec_from_file_location(
            "nsmbu_android_installer", self.package / "installer/setup.py")
        self.setup = importlib.util.module_from_spec(spec)
        installer_path = str(self.package / "installer")
        if installer_path not in sys.path:
            sys.path.insert(0, installer_path)
        spec.loader.exec_module(self.setup)

    def boundary(self):
        if self.pause_requested():
            self.emit({"state": "paused"})
            raise Paused()

    def hash_file(self, path):
        return digest(path, self.boundary)

    def hash_tree(self, root):
        self.boundary()
        return inventory(root, self.boundary)

    def run_native(self, stage, runner, command, env):
        requested = threading.Event()
        def cancel():
            if self.pause_requested():
                requested.set()
            return requested.is_set()
        try:
            return runner(command, env=env, cancel=cancel)
        except InterruptedError:
            if not requested.is_set():
                raise
            self.emit({"stage": stage, "state": "paused", "restart_incomplete_command": True})
            raise Paused() from None

    @contextmanager
    def translation_control(self):
        """Arm Python line interruption only after a pause is requested.

        No translator tracing runs in the ordinary case. The polling thread
        enables monitoring on a pending pause; only translator code on this
        worker thread may raise, so its existing finally blocks can unwind.
        """
        monitoring = sys.monitoring
        for tool in (3, 4, 5, 2, 1, 0):
            try:
                monitoring.use_tool_id(tool, "nsmbu-translation-pause")
                break
            except ValueError:
                continue
        else:
            raise RuntimeError("No Python monitoring slot available for pausable translation")
        owner = threading.get_ident()
        stopped = threading.Event()
        failure = []
        prefix = str(self.package / "tools/recomp") + os.sep
        rpx = str(self.package / "tools/rpx/rpx.py")

        def interrupt(code, line):
            if threading.get_ident() != owner:
                return
            if not (code.co_filename.startswith(prefix) or code.co_filename == rpx):
                return
            monitoring.set_events(tool, 0)
            if failure:
                raise failure[0]
            self.emit({"stage": "translate", "state": "paused", "restart_on_resume": True})
            raise Paused()

        def watch():
            while not stopped.is_set():
                try:
                    requested = self.pause_requested()
                except BaseException as error:
                    failure.append(error)
                    requested = True
                if requested:
                    monitoring.set_events(tool, monitoring.events.LINE)
                    return
                stopped.wait(.1)

        watcher = threading.Thread(target=watch, name="android-translation-pause", daemon=True)
        try:
            monitoring.register_callback(tool, monitoring.events.LINE, interrupt)
            watcher.start()
            try:
                yield
            finally:
                stopped.set()
                watcher.join()
            if failure:
                raise failure[0]
            self.boundary()
        finally:
            monitoring.set_events(tool, 0)
            monitoring.register_callback(tool, monitoring.events.LINE, None)
            monitoring.free_tool_id(tool)

    def require_working_space(self):
        if shutil.disk_usage(self.storage).free < (1 << 30):
            raise self.setup.SetupError(
                "Not enough storage: free space to restore at least 1 GiB working reserve, "
                "then retry setup. Verified checkpoints are kept.")

    def update_identity(self, update_dir):
        """Validate an optional 1.3.0 update folder; its verified inventory joins the identity."""
        if update_dir is None:
            return None
        update_dir = Path(update_dir).resolve()
        self.setup.check_update_folder(str(update_dir))
        return self.hash_tree(update_dir)

    def extract(self, image, kind, extractor, extractor_identity, native_run, keys=None, update_dir=None):
        """Run unchanged installer extraction in an isolated durable generation.

        Interrupted extraction restarts; only complete, fully hashed trees are
        reused. No key bytes, key digest or stdin content enter the checkpoint.
        Earlier completed generations are retained through failures and updates.
        update_dir: the optional 1.3.0 update folder extract_game applies.
        """
        self.boundary()
        if kind not in ("archive", "image") or not extractor_identity:
            raise ValueError("dump kind and validated extractor identity required")
        image = Path(image).resolve()
        self.emit({"stage": "extract", "state": "checking_input"})
        identity = hashlib.sha256(json.dumps({
            "image": self.hash_file(image), "kind": kind, "extractor": extractor_identity,
            "installer": self.hash_file(self.package / "installer/setup.py"),
            "update": self.update_identity(update_dir),
        }, sort_keys=True).encode()).hexdigest()
        stage = self.storage / ("extraction-" + identity)
        game, marker = stage / "game", stage / "complete.json"
        try:
            record = json.loads(marker.read_text())
            outputs = self.hash_tree(game)
            if record["identity"] == identity and outputs and outputs == record["outputs"]:
                self.setup.check_game_version(str(game))
                if self.setup.valid_game_folder(str(game)):
                    self.emit({"stage": "extract", "state": "reused", **record["metrics"]})
                    return game
        except (OSError, ValueError, KeyError, TypeError):
            pass
        self.boundary()
        stage.mkdir(exist_ok=True)
        marker.unlink(missing_ok=True)
        for leftover in stage.glob("pending-*"):
            shutil.rmtree(leftover)
        pending = stage / ("pending-" + uuid.uuid4().hex)
        pending.mkdir()
        original = self.setup.subprocess, self.setup.extractor, self.setup.GUI, self.setup.LOG
        self.setup.subprocess = ProcessBridge(native_run, self.pause_requested,
                                             {"LD_LIBRARY_PATH": str(Path(extractor).parent)})
        self.setup.extractor = lambda: str(extractor)
        self.setup.GUI = SimpleNamespace(emit=lambda event: self.emit({"stage": "extract", "state": "progress", **event}))
        self.setup.LOG = self.setup.Log()
        started = time.monotonic()
        try:
            self.setup.LOG.open(str(stage / "extract.log"))
            self.emit({"stage": "extract", "state": "running", "restart_if_interrupted": True})
            with (stage / "output.log").open("w", encoding="utf-8") as log:
                with redirect_stdout(log), redirect_stderr(log):
                    title = None
                    if kind == "archive":
                        problem, error, info = self.setup.archive_info(str(image))
                        if problem and problem != "wrong_title":
                            raise self.setup.SetupError("cannot read the Cemu archive: " + error)
                        title, _ = self.setup.archive_choice(info)
                    else:
                        if keys is None or not keys.disc or not keys.common:
                            raise self.setup.SetupError("Disc and common keys are required")
                        problem, error, info = self.setup.disc_info(str(image), keys)
                        if problem:
                            raise self.setup.SetupError(error or problem)
                        self.setup.check_title(info.get("title_id", ""))
                    self.boundary()
                    self.setup.extract_game(str(image), keys, info, str(pending), title,
                                            update_dir=str(Path(update_dir).resolve()) if update_dir else None)
            ready = pending / "game"
            outputs = self.hash_tree(ready)
            if not outputs:
                raise RuntimeError("extraction produced no files")
            for path in sorted(ready.rglob("*"), reverse=True) + [ready]:
                self.boundary()
                fd = os.open(path, os.O_RDONLY)
                try: os.fsync(fd)
                finally: os.close(fd)
            if game.exists():
                shutil.rmtree(game)
            os.replace(ready, game)
            metrics = {"seconds": time.monotonic() - started,
                       "bytes": sum(path.stat().st_size for path in game.rglob("*") if path.is_file()),
                       "files": len(outputs)}
            atomic_json(marker, {"identity": identity, "outputs": outputs, "metrics": metrics})
        except InterruptedError:
            if self.pause_requested():
                self.emit({"stage": "extract", "state": "paused", "restart_on_resume": True})
                raise Paused()
            raise
        finally:
            if self.setup.LOG.f:
                self.setup.LOG.f.close()
            self.setup.subprocess, self.setup.extractor, self.setup.GUI, self.setup.LOG = original
            shutil.rmtree(pending, ignore_errors=True)
        self.emit({"stage": "extract", "state": "complete", **metrics})
        self.boundary()
        return game

    def prepare_folder(self, folder, update_dir=None):
        """An extracted game folder as is, or a durable merged copy with the 1.3.0 update applied."""
        self.boundary()
        folder = Path(folder).resolve()
        if update_dir is None:
            self.setup.require_playable_game_folder(str(folder))
            return folder
        identity = hashlib.sha256(json.dumps({
            "folder": self.hash_tree(folder), "update": self.update_identity(update_dir),
            "installer": self.hash_file(self.package / "installer/setup.py"),
        }, sort_keys=True).encode()).hexdigest()
        stage = self.storage / ("merged-" + identity)
        game, marker = stage / "game", stage / "complete.json"
        try:
            record = json.loads(marker.read_text())
            if record["identity"] == identity and record["outputs"] == self.hash_tree(game):
                self.emit({"stage": "extract", "state": "reused", **record["metrics"]})
                return game
        except (OSError, ValueError, KeyError, TypeError):
            pass
        self.require_working_space()
        stage.mkdir(exist_ok=True)
        marker.unlink(missing_ok=True)
        for leftover in stage.glob("pending-*"):
            shutil.rmtree(leftover)
        pending = stage / ("pending-" + uuid.uuid4().hex)
        started = time.monotonic()
        self.emit({"stage": "extract", "state": "running", "restart_if_interrupted": True})
        try:
            shutil.copytree(folder, pending)
            self.boundary()
            self.setup.apply_update_to(str(pending), str(Path(update_dir).resolve()))
            self.setup.check_game_version(str(pending))
            self.setup.require_playable_game_folder(str(pending))
            outputs = self.hash_tree(pending)
            if game.exists():
                shutil.rmtree(game)
            os.replace(pending, game)
            metrics = {"seconds": time.monotonic() - started, "files": len(outputs),
                       "bytes": sum(path.stat().st_size for path in game.rglob("*") if path.is_file())}
            atomic_json(marker, {"identity": identity, "outputs": outputs, "metrics": metrics})
        finally:
            shutil.rmtree(pending, ignore_errors=True)
        self.emit({"stage": "extract", "state": "complete", **metrics})
        self.boundary()
        return game

    def translate(self, game):
        """Validate the supported dump, resume verified C, or run setup.recompile.

        A paused or killed translation restarts its incomplete generation;
        completed translation is reused only after hashing every output file.
        The NSMBU_RECOMP_* switches setup.recompile passes to recomp.py are part
        of the identity, so a different translation keeps its own generation.
        """
        self.boundary()
        game = Path(game).resolve()
        self.setup.check_game_version(str(game))
        inputs = {"adapter": self.hash_file(Path(__file__)),
                  "rpx": self.hash_file(game / "code/red-pro2.rpx"),
                  "installer": self.hash_file(self.package / "installer/setup.py"),
                  "rpx_reader": self.hash_file(self.package / "tools/rpx/rpx.py"),
                  "translator": self.hash_tree(self.package / "tools/recomp"),
                  "switches": {name: value for name, value in os.environ.items() if name.startswith("NSMBU_RECOMP_")}}
        inputs["translator"] = {name: value for name, value in inputs["translator"].items()
                                if Path(name).suffix in (".py", ".txt", ".json")}
        identity = hashlib.sha256(json.dumps(inputs, sort_keys=True).encode()).hexdigest()
        stage = self.storage / ("translation-" + identity)
        state = stage / "complete.json"
        generated = stage / "gen"
        try:
            record = json.loads(state.read_text())
            outputs = self.hash_tree(generated)
            if record["identity"] == identity and outputs and outputs == record["outputs"]:
                self.emit({"stage": "translate", "state": "reused", **record["metrics"]})
                return generated
        except (OSError, ValueError, KeyError, TypeError):
            pass
        self.require_working_space()
        stage.mkdir(exist_ok=True)
        state.unlink(missing_ok=True)
        for leftover in stage.glob("pending-*"):
            shutil.rmtree(leftover)
        pending = stage / ("pending-" + uuid.uuid4().hex)
        started = time.monotonic()
        self.emit({"stage": "translate", "state": "running"})
        original = self.setup.subprocess
        adapter = self

        class Lines(io.TextIOBase):
            def __init__(self):
                self.lines, self.partial = [], ""

            def writable(self):
                return True

            def write(self, text):
                self.partial += text
                while "\n" in self.partial:
                    line, self.partial = self.partial.split("\n", 1)
                    self.lines.append(line + "\n")
                    parts = line.split(None, 3)
                    if len(parts) >= 3 and parts[0] == "progress" and parts[1].isdigit() and parts[2].isdigit():
                        adapter.emit({"stage": "translate", "state": "progress", "done": int(parts[1]),
                                      "total": int(parts[2]), "detail": parts[3] if len(parts) > 3 else ""})
                return len(text)

        def embedded(command, env=None, stdout=None, stderr=None, text=None, bufsize=None):
            expected = self.package / "tools/recomp/recomp.py"
            if len(command) != 5 or Path(command[1]).resolve() != expected or command[4] != "--progress":
                raise RuntimeError("unexpected subprocess requested by translation")
            switches = {name: value for name, value in (env or {}).items() if name.startswith("NSMBU_RECOMP_")}
            argv, search, saved = sys.argv, list(sys.path), {name: os.environ.get(name) for name in switches}
            output, code = Lines(), 0
            try:
                os.environ.update(switches)
                sys.argv = command[1:]
                sys.path.insert(0, str(expected.parent))
                with redirect_stdout(output), self.translation_control():
                    try:
                        runpy.run_path(str(expected), run_name="__main__")
                    except SystemExit as exit:
                        if exit.code not in (None, 0):
                            code = exit.code if isinstance(exit.code, int) else 1
                            if not isinstance(exit.code, int):
                                output.write(str(exit.code) + "\n")
            finally:
                sys.argv = argv
                sys.path[:] = search
                for name, value in saved.items():
                    if value is None:
                        os.environ.pop(name, None)
                    else:
                        os.environ[name] = value
            if output.partial:
                output.lines.append(output.partial)
            return SimpleNamespace(stdout=iter(output.lines), wait=lambda: code, returncode=code)

        self.setup.subprocess = SimpleNamespace(Popen=embedded, PIPE=original.PIPE, STDOUT=original.STDOUT)
        try:
            with (stage / "translation.log").open("w", encoding="utf-8") as log:
                with redirect_stdout(log), redirect_stderr(log):
                    count = self.setup.recompile(str(game), str(pending))
            outputs = self.hash_tree(pending)
            if not outputs or not count:
                raise RuntimeError("translation produced no source files")
            if generated.exists():
                shutil.rmtree(generated)
            os.replace(pending, generated)
            metrics = {"source_files": count, "seconds": time.monotonic() - started,
                       "bytes": sum(path.stat().st_size for path in generated.rglob("*") if path.is_file())}
            atomic_json(state, {"identity": identity, "outputs": outputs, "metrics": metrics})
        finally:
            self.setup.subprocess = original
            if pending.exists():
                shutil.rmtree(pending)
        self.emit({"stage": "translate", "state": "complete", **metrics})
        self.boundary()
        return generated

    def compile(self, toolchain, manifest, generated, toolchain_identity, native_run,
                jobs=1, header_roots=None):
        """Call unchanged compile_gamecode with durable per-object checkpoints.

        native_run(command, env=..., cancel=...) returns (returncode, combined_output_bytes).
        toolchain_identity identifies the validated compiler/sysroot bundle.
        The host supplies all SDK header roots; system headers belong to that
        bundle. A pause cancels active native tools and retains verified objects.
        """
        self.boundary()
        if jobs < 1 or not toolchain_identity:
            raise ValueError("positive jobs and a validated toolchain identity required")
        generated = Path(generated).resolve()
        roots = [Path(path).resolve() for path in
                 (header_roots if header_roots is not None else [self.package / "sdk"])]
        identity = hashlib.sha256(json.dumps({
            "port_revision": self.port_revision,
            "installer": self.hash_file(self.package / "installer/setup.py"),
            "generated": self.hash_tree(generated),
            "headers": {str(path): self.hash_tree(path) for path in roots},
            "toolchain": toolchain_identity,
            "command": toolchain.cc, "environment": toolchain.env,
            "flags": manifest["gamecode_cflags"],
        }, sort_keys=True).encode()).hexdigest()
        stage = self.storage / ("compile-" + identity)
        objects = stage / "obj"
        objects.mkdir(parents=True, exist_ok=True)
        for leftover in objects.glob("*.pending-*"):
            leftover.unlink()
        for leftover in objects.glob("pending-*"):
            shutil.rmtree(leftover)
        event_lock = threading.Lock()
        stats = {"compiled": 0, "reused": 0}
        started = time.monotonic()
        sources = sorted(generated.glob("code_*.c")) + [generated / "table.c", generated / "imports.c"]
        sizes = {path.name: path.stat().st_size for path in sources if path.is_file()}
        total = len(sources)
        bytes_done = {"compiled": 0, "reused": 0}

        def estimate():
            elapsed = time.monotonic() - started
            remaining = sum(sizes.values()) - bytes_done["compiled"] - bytes_done["reused"]
            if stats["compiled"] < 3 or not bytes_done["compiled"] or elapsed <= 0:
                return None
            return round(remaining * elapsed / bytes_done["compiled"])

        def completed(state, source):
            with event_lock:
                stats[state] += 1
                bytes_done[state] += sizes.get(source.name, 0)
                event = {"stage": "compile", "state": state, "source": source.name,
                         "total": total, "jobs": jobs, **stats}
                eta = estimate()
                if eta is not None:
                    event["eta_seconds"] = eta
                self.emit(event)

        self.emit({"stage": "compile", "state": "started", "total": total, "jobs": jobs, **stats})

        def run(command, env=None, stdout=None, stderr=None):
            with event_lock:
                self.boundary()
            if "-c" not in command or "-o" not in command:
                raise RuntimeError("unexpected compiler command from shared installer")
            output_index = command.index("-o") + 1
            destination = Path(command[output_index])
            source = Path(command[command.index("-c") + 1])
            if destination.parent != objects or source.parent != generated:
                raise RuntimeError("compiler paths outside this stage")
            marker = destination.with_suffix(destination.suffix + ".json")
            try:
                record = json.loads(marker.read_text())
                if record["identity"] == identity and record["sha256"] == self.hash_file(destination):
                    completed("reused", source)
                    return SimpleNamespace(returncode=0, stdout=b"")
            except (OSError, ValueError, TypeError, KeyError):
                pass
            self.require_working_space()
            marker.unlink(missing_ok=True)
            temporary = objects / ("pending-" + uuid.uuid4().hex)
            temporary.mkdir()
            pending = temporary / destination.name
            command = list(command)
            command[output_index] = str(pending)
            try:
                code, output = self.run_native("compile", native_run, command, env)
                if code:
                    return SimpleNamespace(returncode=code, stdout=output)
                if not pending.is_file() or pending.stat().st_size == 0:
                    raise RuntimeError("compiler produced no object")
                with pending.open("rb") as stream:
                    os.fsync(stream.fileno())
                os.replace(pending, destination)
                atomic_json(marker, {"identity": identity, "sha256": self.hash_file(destination)})
                completed("compiled", source)
                return SimpleNamespace(returncode=0, stdout=output)
            finally:
                shutil.rmtree(temporary)

        original = self.setup.subprocess
        self.setup.subprocess = SimpleNamespace(run=run, PIPE=original.PIPE, STDOUT=original.STDOUT)
        try:
            with (stage / "compile.log").open("w", encoding="utf-8") as log:
                with redirect_stdout(log), redirect_stderr(log):
                    result = self.setup.compile_gamecode(toolchain, manifest, str(generated), str(objects), jobs)
        finally:
            self.setup.subprocess = original
        self.emit({"stage": "compile", "state": "complete", **stats,
                   "seconds": time.monotonic() - started,
                   "bytes": sum(Path(path).stat().st_size for path in result)})
        self.boundary()
        return result

    def link(self, toolchain, manifest, objects, toolchain_identity, native_run,
             sdk_roots=None):
        """Checkpoint unchanged setup.link_game without publishing an active game.

        A validated generation is returned for the host to ABI-check/load and
        activate. Failed linking never replaces an earlier generation.
        """
        self.boundary()
        if not objects or not toolchain_identity:
            raise ValueError("objects and a validated toolchain identity required")
        objects = [str(Path(path).resolve()) for path in objects]
        roots = [Path(path).resolve() for path in
                 (sdk_roots if sdk_roots is not None else [self.package / "sdk"])]
        identity = hashlib.sha256(json.dumps({
            "port_revision": self.port_revision,
            "installer": self.hash_file(self.package / "installer/setup.py"),
            "objects": [{"path": path, "sha256": self.hash_file(Path(path))} for path in objects],
            "sdk": {str(path): self.hash_tree(path) for path in roots},
            "toolchain": toolchain_identity,
            "cxx": toolchain.cxx, "ar": toolchain.ar,
            "environment": toolchain.env, "response_files": toolchain.rsp,
            "link": manifest["link"],
        }, sort_keys=True).encode()).hexdigest()
        stage = self.storage / ("link-" + identity)
        stage.mkdir(exist_ok=True)
        library, marker = stage / "libgame.so", stage / "complete.json"
        try:
            record = json.loads(marker.read_text())
            if record["identity"] == identity and record["sha256"] == self.hash_file(library):
                self.emit({"stage": "link", "state": "reused", "bytes": library.stat().st_size})
                return library
        except (OSError, ValueError, KeyError, TypeError):
            pass
        self.require_working_space()
        marker.unlink(missing_ok=True)
        for leftover in stage.glob("pending-*"):
            shutil.rmtree(leftover)
        pending = stage / ("pending-" + uuid.uuid4().hex)
        pending.mkdir()
        candidate = pending / "libgame.so"
        started = time.monotonic()
        original = self.setup.run_logged

        def run(command, cwd=None, env=None, what="command"):
            self.boundary()
            if cwd is not None:
                raise RuntimeError("unexpected working directory from shared linker")
            self.require_working_space()
            code, output = self.run_native("link", native_run, command, env)
            if code:
                raise self.setup.SetupError(what + " failed:\n" + output.decode("utf-8", "replace"))
            return output.decode("utf-8", "replace")

        self.setup.run_logged = run
        try:
            with (stage / "link.log").open("w", encoding="utf-8") as log:
                with redirect_stdout(log), redirect_stderr(log):
                    self.setup.link_game(toolchain, manifest, objects, str(pending), str(candidate))
            if candidate.stat().st_size == 0:
                raise RuntimeError("linker produced an empty library")
            with candidate.open("rb") as stream:
                os.fsync(stream.fileno())
            candidate.chmod(0o400)
            os.replace(candidate, library)
            atomic_json(marker, {"identity": identity, "sha256": self.hash_file(library)})
        finally:
            self.setup.run_logged = original
            shutil.rmtree(pending)
        self.emit({"stage": "link", "state": "complete",
                   "seconds": time.monotonic() - started, "bytes": library.stat().st_size})
        self.boundary()
        return library

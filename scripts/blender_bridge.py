"""Export a saved Blender scene and reimport it through the engine's public CLI."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import time


EXPORT_SCRIPT = '''import bpy, sys
destination = sys.argv[sys.argv.index("--") + 1]
if any(obj.type == "ARMATURE" for obj in bpy.context.scene.objects):
    raise RuntimeError("The Blender bridge currently reimports static models; export rigged models through the skeletal importer.")
result = bpy.ops.export_scene.gltf(filepath=destination, export_format="GLB")
if "FINISHED" not in result:
    raise RuntimeError("Blender did not finish the glTF export")
'''


def fingerprint(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while block := stream.read(1024 * 1024):
            digest.update(block)
    return digest.hexdigest()


def executable(value: str, label: str) -> str:
    found = shutil.which(value)
    if found is None:
        raise ValueError(f"{label} was not found: {value}. Install it or pass its executable path.")
    return found


def emit(status: str, **fields) -> None:
    print(json.dumps(dict(schema="21kb.blender-bridge/v1", status=status, **fields)), flush=True)


def run(command: list[str], timeout: float, log: Path, cancel_file: Path | None = None) -> None:
    # Logs are files so a verbose exporter cannot exhaust the watcher's memory.
    with log.open("wb") as output:
        process = subprocess.Popen(command, stdout=output, stderr=subprocess.STDOUT,
            creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
        try:
            deadline = time.monotonic() + timeout
            while True:
                if cancel_file and cancel_file.exists():
                    raise KeyboardInterrupt
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise RuntimeError(f"Command timed out after {timeout:g}s; see {log}")
                try:
                    code = process.wait(timeout=min(0.25, remaining))
                    break
                except subprocess.TimeoutExpired:
                    pass
        except BaseException:
            process.kill()
            process.wait()
            raise
    if code:
        raise RuntimeError(f"Command failed with exit code {code}; see {log}")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project", type=Path, required=True)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--cli", default="kb_cli")
    parser.add_argument("--blender", default="blender")
    parser.add_argument("--destination", default="/Game/Models")
    parser.add_argument("--on-conflict", choices=["report", "keep-local", "overwrite"], default="report")
    parser.add_argument("--watch", action="store_true")
    parser.add_argument("--poll-seconds", type=float, default=1)
    parser.add_argument("--timeout-seconds", type=float, default=180)
    parser.add_argument("--max-updates", type=int, default=0, help="Stop watching after this many successful imports; zero is unlimited")
    parser.add_argument("--cancel-file", type=Path)
    args = parser.parse_args(argv)
    try:
        import math
        if not math.isfinite(args.poll_seconds) or not 0.1 <= args.poll_seconds <= 60:
            raise ValueError("--poll-seconds must be between 0.1 and 60")
        if not math.isfinite(args.timeout_seconds) or args.timeout_seconds <= 0 or args.max_updates < 0:
            raise ValueError("Timeout must be positive and max updates cannot be negative")
        project, source = args.project.resolve(), args.source.resolve()
        if not project.is_dir() or not source.is_file() or source.suffix.lower() != ".blend":
            raise ValueError("Pass an existing project directory and a saved .blend file")
        cli, blender = executable(args.cli, "kb_cli"), executable(args.blender, "Blender")
        key = hashlib.sha256((str(source) + "\n" + args.destination).encode("utf-8")).hexdigest()[:16]
        cache = project / "Saved" / "BlenderBridge" / key
        cache.mkdir(parents=True, exist_ok=True)
        lock = cache / "watch.lock"
        try:
            handle = lock.open("x", encoding="utf-8")
        except FileExistsError:
            raise ValueError(f"This source already has a bridge session. If it stopped unexpectedly, remove {lock}.") from None
        with handle:
            handle.write(str(os.getpid()))
        try:
            binding_file = cache / "binding.json"
            binding = json.loads(binding_file.read_text(encoding="utf-8")) if binding_file.exists() else None
            if binding is not None and (not isinstance(binding, dict) or not isinstance(binding.get("asset"), str)):
                raise ValueError(f"Invalid bridge binding: {binding_file}")
            observed = None
            observed_stamp = None
            updates = 0
            last_error = None
            while True:
                if args.cancel_file and args.cancel_file.exists():
                    emit("cancelled", updates=updates)
                    return 0
                try:
                    stat = source.stat()
                    stamp = (stat.st_size, stat.st_mtime_ns)
                    if args.watch and stamp == observed_stamp:
                        time.sleep(args.poll_seconds)
                        continue
                    current = fingerprint(source)
                    observed_stamp = stamp
                    if current != observed:
                        # Wait for a saved file to settle before Blender opens it.
                        time.sleep(args.poll_seconds)
                        if fingerprint(source) != current:
                            observed_stamp = None
                            continue
                        observed = current
                        with tempfile.TemporaryDirectory(prefix="export-", dir=cache) as scratch:
                            export = Path(scratch) / (source.stem + ".glb")
                            script = Path(scratch) / "export.py"
                            script.write_text(EXPORT_SCRIPT, encoding="utf-8")
                            run([blender, "--background", "--factory-startup", "--disable-autoexec",
                                "--threads", "2", str(source), "--python-exit-code", "1", "--python", str(script),
                                "--", str(export)], args.timeout_seconds, cache / "blender.log", args.cancel_file)
                            if fingerprint(source) != current:
                                observed = None
                                observed_stamp = None
                                continue
                            if not export.is_file() or export.stat().st_size < 12:
                                raise RuntimeError("Blender did not produce a GLB file")
                            staged = cache / export.name
                            os.replace(export, staged)
                        import_command = [cli, "import", "--project", str(project),
                            "--import-materials", "yes", "--import-textures", "yes", "--on-conflict", args.on_conflict]
                        import_command += ["--reimport", binding["asset"]] if binding else ["--destination", args.destination]
                        run([*import_command, str(staged)], args.timeout_seconds, cache / "import.log", args.cancel_file)
                        if binding is None:
                            imported = (cache / "import.log").read_text(encoding="utf-8", errors="replace")
                            match = re.search(r"^(?:created|reused|updated) (/Game/[^\r\n]+) \(RenderMesh\)", imported, re.MULTILINE)
                            if match is None:
                                raise RuntimeError(f"The importer did not identify a static mesh; see {cache / 'import.log'}")
                            binding = dict(asset=match.group(1))
                            temporary = binding_file.with_suffix(".tmp")
                            temporary.write_text(json.dumps(binding), encoding="utf-8")
                            temporary.replace(binding_file)
                        updates += 1
                        last_error = None
                        emit("imported", updates=updates, sourceHash=current, exportHash=fingerprint(staged), log=str(cache / "import.log"))
                        if not args.watch or (args.max_updates and updates >= args.max_updates):
                            return 0
                except (OSError, RuntimeError) as error:
                    if str(error) != last_error:
                        emit("failed", diagnostic=str(error))
                        last_error = str(error)
                    if not args.watch:
                        return 1
                time.sleep(args.poll_seconds)
        finally:
            lock.unlink(missing_ok=True)
    except (OSError, ValueError) as error:
        emit("failed", diagnostic=str(error))
        return 1
    except KeyboardInterrupt:
        emit("cancelled")
        return 0


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env python3
"""Create a relocatable dedicated-server runtime distribution."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile



def package(args):
    if args.binary.is_symlink() or any(path.is_symlink() for path in args.runtime_library):
        raise ValueError("runtime inputs must not be symbolic links")
    binary = args.binary.resolve(strict=True)
    output = args.output.resolve()
    if output.exists():
        raise ValueError("the package output must not already exist")
    if binary.suffix.lower() not in (".exe", ""):
        raise ValueError("the server binary must be a native executable")
    platform = "windows" if binary.suffix.lower() == ".exe" else "linux"
    files = [binary, *(path.resolve(strict=True) for path in args.runtime_library)]
    for library in files[1:]:
        if library.suffix.lower() not in (".dll", ".dylib") and ".so" not in library.name:
            raise ValueError("runtime-library must name a native shared library")
    names = set()
    for path in files:
        if not path.is_file() or path.is_symlink() or path.name.casefold() in names:
            raise ValueError("runtime inputs must be unique regular files")
        names.add(path.name.casefold())
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="ServerPackage", dir=output.parent) as temporary:
        root = Path(temporary) / "runtime"
        root.mkdir()
        for path in files:
            shutil.copy2(path, root / path.name)
        # The server shares engine libraries; preserve their notices even when
        # a particular project does not activate every optional subsystem.
        subprocess.run([os.sys.executable, str(args.engine_root / "scripts/third_party_notices.py"),
            "--engine-root", str(args.engine_root), "--product", "engine", "--platform", platform,
            "--product-name", "Dedicated Server", "--version", args.version, "--out", str(root)], check=True)
        (root / "README.txt").write_text(
            "Dedicated server runtime\n\n"
            "Run the executable with --project <project-directory> --keys <operator-key-file>.\n"
            "Use --help for session, WebSocket, checkpoint and shutdown options.\n"
            "Deploy project content and project-specific native modules separately.\n"
            "This runtime distribution does not contain credentials, checkpoints or project content.\n"
            "Windows builds require the matching Microsoft Visual C++ runtime.\n"
            "Linux builds require the system libraries used by the selected binary.\n",
            encoding="utf-8")
        manifest = {"schema": "21kb.server-runtime/v1", "platform": platform,
            "executable": binary.name, "version": args.version, "files": {}}
        for path in sorted(root.rglob("*")):
            if path.is_file():
                manifest["files"][path.relative_to(root).as_posix()] = {
                    "sha256": hashlib.sha256(path.read_bytes()).hexdigest(), "size": path.stat().st_size}
        (root / "server.manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
        # Exercise the relocated executable with the caller's supplied native
        # process suite before exposing a completed distribution.
        if args.process_tests:
            subprocess.run([str(args.process_tests.resolve(strict=True)), "--server", str(root / binary.name)],
                cwd=root, check=True, timeout=240)
        root.rename(output)
    return output


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--engine-root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--runtime-library", type=Path, action="append", default=[])
    parser.add_argument("--process-tests", type=Path)
    parser.add_argument("--version", default="1.0")
    options = parser.parse_args()
    options.engine_root = options.engine_root.resolve(strict=True)
    print(package(options))

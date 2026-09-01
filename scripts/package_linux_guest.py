#!/usr/bin/env python3
"""Linux-side player builder used by the packaging bridge."""

from __future__ import annotations

import argparse
import gzip
import hashlib
import json
import os
import re
import shutil
import stat
import subprocess
import sys
import tarfile
import tempfile
import time
from pathlib import Path, PurePosixPath
from typing import BinaryIO

from package_contract import PackagingError, engine_source_fingerprint, verify_unit


class GuestError(RuntimeError):
    pass


class _GuestFileLock:
    def __init__(self, path: Path) -> None:
        self._path = path
        self._stream: BinaryIO | None = None

    def __enter__(self) -> "_GuestFileLock":
        import fcntl

        self._path.parent.mkdir(parents=True, exist_ok=True)
        stream = self._path.open("a+b")
        fcntl.flock(stream.fileno(), fcntl.LOCK_EX)
        self._stream = stream
        return self

    def __exit__(self, _type: object, _value: object, _traceback: object) -> None:
        import fcntl

        stream = self._stream
        if stream is not None:
            fcntl.flock(stream.fileno(), fcntl.LOCK_UN)
            stream.close()
            self._stream = None


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while block := stream.read(1024 * 1024):
            digest.update(block)
    return digest.hexdigest()


def _files(root: Path) -> list[tuple[str, Path]]:
    root = root.resolve(strict=True)
    result: list[tuple[str, Path]] = []
    folded: set[str] = set()
    for directory, directories, names in os.walk(root, followlinks=False):
        base = Path(directory)
        for name in directories:
            child = base / name
            if child.is_symlink():
                raise GuestError(f"symbolic link is not an accepted build input: {child}")
        for name in names:
            child = base / name
            if child.is_symlink() or not child.is_file():
                raise GuestError(f"non-regular file is not an accepted build input: {child}")
            relative = child.relative_to(root).as_posix()
            if relative.casefold() in folded:
                raise GuestError(f"case-insensitive path collision: {relative}")
            folded.add(relative.casefold())
            result.append((relative, child))
    result.sort(key=lambda item: item[0].encode("utf-8"))
    return result


def _engine_hash(root: Path) -> str:
    try:
        return engine_source_fingerprint(root)
    except (OSError, PackagingError) as error:
        raise GuestError(f"engine build inputs could not be fingerprinted: {error}") from error


def _run(arguments: list[str | Path], cwd: Path, timeout: int) -> str:
    values = [str(value) for value in arguments]
    result = subprocess.run(
        values,
        cwd=cwd,
        stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        encoding="utf-8",
        errors="replace",
        timeout=timeout,
        check=False,
        shell=False,
    )
    sys.stdout.write(result.stdout)
    sys.stdout.flush()
    if result.returncode != 0:
        raise GuestError(f"command failed with exit code {result.returncode}: {values[0]}")
    return result.stdout


def _extract_input(archive_path: Path, destination: Path) -> None:
    with tarfile.open(archive_path, "r:gz") as archive:
        members = archive.getmembers()
        names: set[str] = set()
        folded: set[str] = set()
        for member in members:
            relative = PurePosixPath(member.name)
            if relative.is_absolute() or any(part in ("", ".", "..") for part in relative.parts):
                raise GuestError("input archive contains an unsafe path")
            if not member.isfile() and not member.isdir():
                raise GuestError("input archive contains a link or special file")
            if member.name in names or member.name.casefold() in folded:
                raise GuestError("input archive contains duplicate paths")
            names.add(member.name)
            folded.add(member.name.casefold())
        for member in members:
            target = destination.joinpath(*PurePosixPath(member.name).parts)
            if member.isdir():
                target.mkdir(parents=True, exist_ok=True)
                continue
            target.parent.mkdir(parents=True, exist_ok=True)
            source = archive.extractfile(member)
            if source is None:
                raise GuestError(f"input archive payload is unreadable: {member.name}")
            with source, target.open("xb") as output:
                shutil.copyfileobj(source, output)
            target.chmod(member.mode & 0o777)


def _find_game(build: Path, configuration: str) -> Path:
    candidates = (build / "bin/kb_game_linux", build / f"bin/{configuration}/kb_game_linux", build / "kb_game_linux")
    for candidate in candidates:
        if candidate.is_file():
            return candidate.resolve(strict=True)
    raise GuestError("Linux player build did not produce kb_game_linux")


def _build_linux_player(engine: Path, configuration: str, expected_hash: str, cmake: str) -> Path:
    build_root = engine / "build-linux-package"
    with _GuestFileLock(build_root / ".package.lock"):
        if _engine_hash(engine) != expected_hash:
            raise GuestError("Linux build machine engine inputs differ from the packaging request")
        build = build_root / expected_hash / configuration
        build.mkdir(parents=True, exist_ok=True)
        _run([
            cmake,
            "-S", engine,
            "-B", build,
            "-G", "Ninja",
            f"-DCMAKE_BUILD_TYPE={configuration}",
            "-DKB_BUILD_EDITOR=OFF",
            "-DKB_BUILD_RENDERER=ON",
            "-DKB_BUILD_PACKAGED_GAME_HOST=ON",
            "-DKB_BUILD_PROVIDER_MODULES_AS_DLL=OFF",
            "-DKB_BUILD_GRAPH_SHADERC=OFF",
            "-DKB_GENERATE_RENDERER_SHADERS=OFF",
            "-DBUILD_TESTING=OFF",
        ], engine, 1800)
        _run([cmake, "--build", build, "--target", "kb_game_linux", "--parallel"], engine, 3600)
        if _engine_hash(engine) != expected_hash:
            raise GuestError("Linux build machine engine inputs changed during the build")
        return _find_game(build, configuration)


def _create_archive(source: Path, destination: Path) -> None:
    temporary = destination.with_suffix(destination.suffix + ".tmp")
    temporary.unlink(missing_ok=True)
    with temporary.open("wb") as output:
        with gzip.GzipFile(filename="", mode="wb", fileobj=output, mtime=0) as compressed:
            with tarfile.open(fileobj=compressed, mode="w", format=tarfile.PAX_FORMAT) as archive:
                for relative, path in _files(source):
                    info = archive.gettarinfo(str(path), arcname=relative)
                    info.uid = info.gid = 0
                    info.uname = info.gname = ""
                    info.mtime = 0
                    with path.open("rb") as stream:
                        archive.addfile(info, stream)
    os.replace(temporary, destination)


def _load_json(path: Path) -> dict[str, object]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise GuestError(f"invalid package metadata: {path.name}") from error
    if not isinstance(value, dict):
        raise GuestError(f"package metadata is not an object: {path.name}")
    return value


def _verify_package_unit(root: Path) -> dict[str, object]:
    try:
        receipt = verify_unit(root)
    except PackagingError as error:
        raise GuestError(str(error)) from error
    if receipt.get("target") != "Linux.x64" or receipt.get("configuration") not in ("Development", "Release"):
        raise GuestError("package is not an exact Linux player unit")
    return receipt


def _verify_linux_runtime(root: Path, executable_name: str, receipt: dict[str, object]) -> Path:
    player = root / executable_name
    if not player.is_file() or not os.access(player, os.X_OK):
        raise GuestError("published Linux player is not executable")
    pack = root / "Game.kbpack"
    build_receipt = _load_json(root / "linux-build.receipt.json")
    inputs = receipt.get("inputs")
    engine_hash = inputs.get("engineSha256") if isinstance(inputs, dict) else None
    expected = {
        "schema": 1,
        "configuration": "Debug" if receipt["configuration"] == "Development" else "Release",
        "engineSha256": engine_hash,
        "executableSha256": _sha256(player),
        "assetPackSha256": _sha256(pack),
        "firstFrame": True,
    }
    if build_receipt != expected:
        raise GuestError("Linux runtime receipt does not match the published player")
    if player.read_bytes()[:20][0:4] != b"\x7fELF":
        raise GuestError("published Linux player is not an ELF executable")
    return player


def _visible_display_environment(display: str) -> dict[str, str]:
    match = re.fullmatch(r":([0-9]{1,5})(?:\.([0-9]{1,5}))?", display)
    if match is None:
        raise GuestError("X11 display is invalid")
    server_number = int(match.group(1))
    server = f":{server_number}"
    socket_path = Path(f"/tmp/.X11-unix/X{server_number}")
    try:
        socket_state = socket_path.lstat()
    except OSError as error:
        raise GuestError(f"X11 display socket is unavailable: {error}") from error
    if not stat.S_ISSOCK(socket_state.st_mode):
        raise GuestError("X11 display socket is not a local socket")

    try:
        xorg = subprocess.run(
            ["pgrep", "-xo", "Xorg"],
            check=False,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=10,
        )
    except (OSError, subprocess.TimeoutExpired) as error:
        raise GuestError(f"X11 display process could not be inspected: {error}") from error
    pid = xorg.stdout.strip()
    if xorg.returncode != 0 or not pid.isdigit():
        raise GuestError("X11 display process is unavailable")
    try:
        command_bytes = Path(f"/proc/{pid}/cmdline").read_bytes()
    except OSError as error:
        raise GuestError(f"X11 display process could not be inspected: {error}") from error
    if not command_bytes.endswith(b"\0"):
        raise GuestError("X11 display process has an invalid command line")
    command = command_bytes.split(b"\0")[:-1]
    if not command or any(not token for token in command):
        raise GuestError("X11 display process has an invalid command line")
    server_token = server.encode("ascii")
    auth_indexes = [index for index, token in enumerate(command) if token == b"-auth"]
    local_only = any(
        command[index] == b"-nolisten" and command[index + 1] == b"tcp"
        for index in range(len(command) - 1)
    )
    if (command.count(server_token) != 1 or b"-noreset" not in command or b"-ac" in command or
            not local_only or len(auth_indexes) != 1 or auth_indexes[0] + 1 >= len(command)):
        raise GuestError("X11 display process does not use the required authenticated local configuration")
    authority_text = os.fsdecode(command[auth_indexes[0] + 1])
    authority_posix = PurePosixPath(authority_text)
    if not authority_text or not authority_posix.is_absolute():
        raise GuestError("X11 authority path is invalid")
    authority = Path(str(authority_posix))
    try:
        authority_state = authority.lstat()
    except OSError as error:
        raise GuestError(f"X11 authority is unavailable: {error}") from error
    if (not stat.S_ISREG(authority_state.st_mode) or authority_state.st_uid != os.getuid() or
            stat.S_IMODE(authority_state.st_mode) != 0o600):
        raise GuestError("X11 authority must be a user-owned regular file with mode 0600")

    environment = os.environ.copy()
    environment["DISPLAY"] = display
    environment["XAUTHORITY"] = str(authority)
    return environment


def _remove_launch_child(launch_root: Path, candidate: Path) -> None:
    resolved_root = launch_root.resolve(strict=True)
    if candidate.parent != launch_root or candidate.is_symlink():
        raise GuestError("Linux launch cleanup target is not a direct child of the state root")
    resolved_candidate = candidate.resolve(strict=True)
    if resolved_candidate.parent != resolved_root or not resolved_candidate.is_dir():
        raise GuestError("Linux launch cleanup target is not a direct child of the state root")
    shutil.rmtree(resolved_candidate)


def _stop_unrecorded_process(process: subprocess.Popen[bytes]) -> None:
    if process.poll() is not None:
        return
    process.terminate()
    try:
        process.wait(timeout=10)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=10)


def _recorded_launch_is_alive(pid_file: Path) -> bool:
    if not pid_file.exists() and not pid_file.is_symlink():
        return False
    if pid_file.is_symlink() or not pid_file.is_file():
        raise GuestError("Linux launch PID record is not a regular file")
    text = pid_file.read_text(encoding="ascii")
    if re.fullmatch(r"[1-9][0-9]*\n?", text) is None:
        raise GuestError("Linux launch PID record is invalid")
    try:
        os.kill(int(text), 0)
    except ProcessLookupError:
        return False
    except PermissionError:
        return True
    return True


def _write_pid_atomically(pid_file: Path, pid: int) -> None:
    descriptor, temporary_text = tempfile.mkstemp(
        dir=pid_file.parent,
        prefix=f".{pid_file.name}.",
        suffix=".tmp",
        text=True,
    )
    temporary = Path(temporary_text)
    try:
        with os.fdopen(descriptor, "w", encoding="ascii", newline="\n") as stream:
            stream.write(f"{pid}\n")
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, pid_file)
    except BaseException:
        try:
            temporary.unlink(missing_ok=True)
        except OSError:
            pass
        raise


def _deploy_and_launch_runtime(
    runtime: Path,
    launch_root: Path,
    identity: str,
    executable_name: str,
    receipt: dict[str, object],
    display: str,
) -> None:
    environment = _visible_display_environment(display)
    deployed = launch_root / identity
    candidate = launch_root / f".{identity}.candidate-{os.getpid()}"
    pid_file = launch_root / f"{identity}.pid"
    if _recorded_launch_is_alive(pid_file):
        raise GuestError("this Linux package is already running")
    if candidate.exists() or candidate.is_symlink():
        _remove_launch_child(launch_root, candidate)
    shutil.copytree(runtime, candidate, symlinks=False)
    if deployed.exists() or deployed.is_symlink():
        _remove_launch_child(launch_root, deployed)
    os.replace(candidate, deployed)
    deployed_player = _verify_linux_runtime(deployed, executable_name, receipt)
    smoke = subprocess.run(
        [deployed_player, "--frames=1"], cwd=deployed, env=environment,
        stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        text=True, encoding="utf-8", errors="replace", timeout=180, check=False,
    )
    if (smoke.returncode != 0 or "frames=1" not in smoke.stdout or
            "rendered=1" not in smoke.stdout or "shutdown=clean" not in smoke.stdout):
        raise GuestError("published Linux player did not prove a clean first frame on the selected display")
    log_path = launch_root / f"{identity}.log"
    if log_path.is_symlink() or (log_path.exists() and not log_path.is_file()):
        raise GuestError("Linux launch log is not a regular file")
    log = log_path.open("ab", buffering=0)
    process: subprocess.Popen[bytes] | None = None
    try:
        process = subprocess.Popen(
            [deployed_player], cwd=deployed, env=environment, stdin=subprocess.DEVNULL,
            stdout=log, stderr=subprocess.STDOUT, start_new_session=True, close_fds=True,
        )
        time.sleep(2.0)
        if process.poll() is not None:
            raise GuestError(f"published Linux player exited during launch with code {process.returncode}")
        _write_pid_atomically(pid_file, process.pid)
    except BaseException:
        if process is not None:
            _stop_unrecorded_process(process)
        raise
    finally:
        log.close()


def _launch_package(args: argparse.Namespace) -> None:
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_. -]{0,79}", args.executable_name):
        raise GuestError("executable name is invalid")
    if not re.fullmatch(r":[0-9]{1,5}(?:\.[0-9]{1,5})?", args.display):
        raise GuestError("X11 display is invalid")
    with tempfile.TemporaryDirectory(prefix="21kb-linux-launch-") as temporary_text:
        temporary = Path(temporary_text)
        package = temporary / "package"
        package.mkdir()
        _extract_input(args.launch_archive.resolve(strict=True), package)
        receipt = _verify_package_unit(package)
        runtime = package
        if receipt["configuration"] == "Release":
            archives = list(package.glob("*-linux-x64.tar.gz"))
            if len(archives) != 1:
                raise GuestError("Linux Release package does not contain exactly one runtime archive")
            runtime = temporary / "runtime"
            runtime.mkdir()
            _extract_input(archives[0], runtime)
        player = _verify_linux_runtime(runtime, args.executable_name, receipt)
        launch_root = Path.home() / ".local" / "state" / "21kb" / "package-runs"
        launch_root.mkdir(parents=True, exist_ok=True, mode=0o700)
        if launch_root.is_symlink():
            raise GuestError("Linux launch state root must not be a symbolic link")
        launch_root = launch_root.resolve(strict=True)
        identity = str(receipt["manifestSha256"])
        with _GuestFileLock(launch_root / f"{identity}.lock"):
            _deploy_and_launch_runtime(
                runtime, launch_root, identity, args.executable_name, receipt, args.display)


def main() -> int:
    parser = argparse.ArgumentParser()
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--archive", type=Path)
    mode.add_argument("--launch-archive", type=Path)
    parser.add_argument("--engine-root", type=Path)
    parser.add_argument("--configuration", choices=("Debug", "Release"))
    parser.add_argument("--executable-name", required=True)
    parser.add_argument("--engine-fingerprint")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--display", default=":0")
    args = parser.parse_args()
    try:
        if args.launch_archive is not None:
            _launch_package(args)
            return 0
        if args.engine_root is None or args.configuration is None or args.engine_fingerprint is None or args.output is None:
            raise GuestError("Linux build mode requires engine root, configuration, fingerprint, and output")
        if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_. -]{0,79}", args.executable_name):
            raise GuestError("executable name is invalid")
        engine = args.engine_root.resolve(strict=True)
        cmake = shutil.which("cmake")
        ninja = shutil.which("ninja")
        xvfb = shutil.which("xvfb-run")
        if cmake is None or ninja is None or xvfb is None:
            raise GuestError("Linux build machine requires cmake, ninja, and xvfb-run")
        game = _build_linux_player(engine, args.configuration, args.engine_fingerprint, cmake)
        with tempfile.TemporaryDirectory(prefix="21kb-linux-package-") as temporary_text:
            temporary = Path(temporary_text)
            incoming = temporary / "incoming"
            incoming.mkdir()
            _extract_input(args.archive.resolve(strict=True), incoming)
            pack = incoming / "Game.kbpack"
            if not pack.is_file():
                raise GuestError("Linux package input does not contain Game.kbpack")
            stage = temporary / "result"
            stage.mkdir()
            player = stage / args.executable_name
            shutil.copy2(game, player)
            player.chmod(player.stat().st_mode | 0o111)
            shutil.copy2(pack, stage / "Game.kbpack")
            licenses = stage / "Licenses"
            licenses.mkdir()
            license_sources = {
                "bgfx.rst": engine / "third_party/bgfx.cmake/bgfx/docs/license.rst",
                "bx.txt": engine / "third_party/bgfx.cmake/bx/LICENSE",
                "bimg.txt": engine / "third_party/bgfx.cmake/bimg/LICENSE",
                "flecs.txt": engine / "third_party/flecs/LICENSE",
                "jolt.txt": engine / "third_party/jolt/LICENSE",
                "lua.txt": engine / "third_party/licenses/lua-5.4.8.txt",
                "miniaudio.txt": engine / "third_party/miniaudio/LICENSE",
                "ufbx.txt": engine / "third_party/ufbx/LICENSE",
            }
            for name, source in license_sources.items():
                if not source.is_file():
                    raise GuestError(f"required license is missing: {source}")
                shutil.copy2(source, licenses / name)
            (stage / "THIRD_PARTY_NOTICES.txt").write_text(
                "This product includes bgfx, bx, bimg, Flecs, Jolt Physics, Lua, miniaudio and ufbx.\n"
                "Their license texts are included in the Licenses directory.\n",
                encoding="utf-8",
                newline="\n",
            )
            if player.read_bytes()[:4] != b"\x7fELF":
                raise GuestError("built Linux player does not contain an ELF header")
            dependencies = _run(["ldd", player], stage, 120)
            if "not found" in dependencies:
                raise GuestError("Linux player has unresolved shared-library dependencies")
            smoke = _run([xvfb, "-a", player, "--frames=1"], stage, 180)
            if "frames=1" not in smoke or "rendered=1" not in smoke or "shutdown=clean" not in smoke:
                raise GuestError("Linux player did not prove a clean first frame")
            receipt = {
                "schema": 1,
                "configuration": args.configuration,
                "engineSha256": args.engine_fingerprint,
                "executableSha256": _sha256(player),
                "assetPackSha256": _sha256(stage / "Game.kbpack"),
                "firstFrame": True,
            }
            (stage / "linux-build.receipt.json").write_text(
                json.dumps(receipt, sort_keys=True, separators=(",", ":")) + "\n",
                encoding="utf-8",
                newline="\n",
            )
            _create_archive(stage, args.output.absolute())
        return 0
    except (GuestError, OSError, subprocess.TimeoutExpired, tarfile.TarError) as error:
        print(f"Linux package failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())

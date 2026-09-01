#!/usr/bin/env python3
"""Strict, dependency-free primitives used by the game package finalizer."""

from __future__ import annotations

import hashlib
import json
import os
import secrets
import shutil
import signal
import stat
import subprocess
import sys
import queue
import threading
import time
from dataclasses import dataclass
from pathlib import Path, PurePosixPath
from typing import Callable, Iterable, Mapping, Sequence


MANIFEST_NAME = "package.manifest.json"
RECEIPT_NAME = "package.receipt.json"
_ENVELOPE_FILES = frozenset((MANIFEST_NAME, RECEIPT_NAME))
_MAX_PROCESS_OUTPUT_BYTES = 8 * 1024 * 1024
ENGINE_BUILD_INPUTS = (
    "CMakeLists.txt",
    "CMake",
    "sources/engine",
    "sources/game",
    "sources/plugins",
    "sources/renderer",
    "platform/android/app/build.gradle",
    "platform/android/app/src",
    "platform/android/build.gradle",
    "platform/android/gradle",
    "platform/android/gradle.properties",
    "platform/android/gradlew",
    "platform/android/gradlew.bat",
    "platform/android/settings.gradle",
    "third_party/bgfx.cmake",
    "third_party/flecs",
    "third_party/jolt",
    "third_party/licenses/lua-5.4.8.txt",
    "third_party/miniaudio",
    "third_party/ufbx",
    "scripts/package_contract.py",
    "scripts/package_game.py",
    "scripts/package_linux_guest.py",
    "scripts/windows_pe_resources.py",
)
_SOURCE_TEXT_SUFFIXES = frozenset((
    ".bat", ".bazel", ".bf", ".build", ".c", ".c3", ".camal", ".cap", ".cc",
    ".clang-format", ".cmake", ".cpp", ".cs", ".css", ".csv", ".d", ".doxygen",
    ".drawio", ".editorconfig", ".exp", ".fd", ".flecs", ".frag", ".gitattributes",
    ".gitignore", ".gliffy", ".glsl", ".gradle", ".gyp", ".h", ".hlsl", ".hpp",
    ".hpp11", ".html", ".idl", ".in", ".inc", ".inl", ".java", ".js", ".json",
    ".kbmat", ".kbvfx", ".kt", ".kts", ".l", ".ll", ".lua", ".md", ".metal",
    ".mk", ".mm", ".natvis", ".ninja", ".obj", ".plist", ".pro", ".properties",
    ".py", ".rst", ".sc", ".sh", ".sources", ".svg", ".tof", ".toml", ".txt",
    ".vert", ".webplate", ".xml", ".y", ".yaml", ".yml", ".yy", ".zig",
))
_SOURCE_TEXT_NAMES = frozenset((
    ".clang-format", ".editorconfig", ".gitattributes", ".gitignore", "BUILD",
    "CMakeLists.txt", "CODEOWNERS", "COPYING", "Doxyfile", "gradlew", "LICENSE",
    "README", "updateGrammar", "WORKSPACE",
))


class PackagingError(RuntimeError):
    pass


def _clean_protocol_text(value: object) -> str:
    return str(value).replace("\r", " ").replace("\n", " ").replace("|", "/").strip()


def emit_stage(stage: str, progress: int, message: object) -> None:
    print(f"STAGE|{stage}|{max(0, min(100, progress))}|{_clean_protocol_text(message)}", flush=True)


def emit_diagnostic(level: str, message: object) -> None:
    print(f"DIAGNOSTIC|{level}|{_clean_protocol_text(message)}", flush=True)


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while block := stream.read(1024 * 1024):
            digest.update(block)
    return digest.hexdigest()


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def source_file_fingerprint(path: Path) -> tuple[int, str]:
    """Return a checkout-independent size/hash for a declared source-text file."""
    if path.name not in _SOURCE_TEXT_NAMES and path.suffix.lower() not in _SOURCE_TEXT_SUFFIXES:
        return path.stat().st_size, sha256_file(path)
    data = path.read_bytes()
    if b"\0" in data:
        return len(data), sha256_bytes(data)
    try:
        data.decode("utf-8")
    except UnicodeDecodeError:
        return len(data), sha256_bytes(data)
    canonical = data.replace(b"\r\n", b"\n").replace(b"\r", b"\n")
    return len(canonical), sha256_bytes(canonical)


def source_tree_fingerprint(root: Path) -> str:
    digest = hashlib.sha256()
    for relative, path in regular_files(root):
        size, file_digest = source_file_fingerprint(path)
        digest.update(relative.encode("utf-8"))
        digest.update(b"\0")
        digest.update(str(size).encode("ascii"))
        digest.update(b"\0")
        digest.update(bytes.fromhex(file_digest))
    return digest.hexdigest()


def engine_source_fingerprint(root: Path) -> str:
    root = root.resolve(strict=True)
    digest = hashlib.sha256()
    for relative in ENGINE_BUILD_INPUTS:
        path = (root / relative).resolve(strict=True)
        if path.is_dir():
            value = source_tree_fingerprint(path)
        elif path.is_file():
            size, file_digest = source_file_fingerprint(path)
            value = hashlib.sha256(f"{size}\0{file_digest}".encode("ascii")).hexdigest()
        else:
            raise PackagingError(f"fingerprinted input is not a file or directory: {path}")
        digest.update(relative.encode("utf-8"))
        digest.update(b"\0")
        digest.update(value.encode("ascii"))
        digest.update(b"\0")
    return digest.hexdigest()


def canonical_json_bytes(value: object) -> bytes:
    return (json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":")) + "\n").encode("utf-8")


def write_json(path: Path, value: object) -> None:
    path.write_bytes(canonical_json_bytes(value))


def _is_reparse(path: Path) -> bool:
    try:
        attributes = path.lstat().st_file_attributes
    except AttributeError:
        return False
    return bool(attributes & stat.FILE_ATTRIBUTE_REPARSE_POINT)


def _validate_relative_path(text: str) -> PurePosixPath:
    path = PurePosixPath(text)
    if not text or "\\" in text or path.is_absolute() or any(part in ("", ".", "..") for part in path.parts):
        raise PackagingError(f"invalid package-relative path: {text!r}")
    if any("\x00" in part for part in path.parts):
        raise PackagingError("package-relative path contains a NUL byte")
    return path


def regular_files(
    root: Path,
    *,
    excluded: Iterable[str] = (),
    excluded_top_level: Iterable[str] = (),
) -> list[tuple[str, Path]]:
    root = root.resolve(strict=True)
    if not root.is_dir() or root.is_symlink() or _is_reparse(root):
        raise PackagingError(f"package root must be a regular directory: {root}")
    excluded_set = frozenset(excluded)
    excluded_roots = frozenset(name.casefold() for name in excluded_top_level)
    if any(not name or "/" in name or "\\" in name or name in (".", "..") for name in excluded_roots):
        raise PackagingError("excluded top-level directory name is invalid")
    files: list[tuple[str, Path]] = []
    folded: dict[str, str] = {}
    for directory, names, file_names in os.walk(root, topdown=True, followlinks=False):
        directory_path = Path(directory)
        if directory_path == root and excluded_roots:
            names[:] = [name for name in names if name.casefold() not in excluded_roots]
        for name in tuple(names):
            child = directory_path / name
            if child.is_symlink() or _is_reparse(child):
                raise PackagingError(f"symbolic links and reparse points are not package inputs: {child}")
        for name in file_names:
            child = directory_path / name
            if child.is_symlink() or _is_reparse(child) or not child.is_file():
                raise PackagingError(f"package input is not a regular file: {child}")
            relative = child.relative_to(root).as_posix()
            _validate_relative_path(relative)
            if relative in excluded_set:
                continue
            folded_name = relative.casefold()
            previous = folded.get(folded_name)
            if previous is not None and previous != relative:
                raise PackagingError(f"case-insensitive package path collision: {previous!r} and {relative!r}")
            folded[folded_name] = relative
            files.append((relative, child))
    files.sort(key=lambda item: item[0].encode("utf-8"))
    return files


def copy_tree_exact(source: Path, destination: Path, *, excluded_top_level: Iterable[str] = ()) -> None:
    source = source.resolve(strict=True)
    if destination.exists():
        raise PackagingError(f"copy destination already exists: {destination}")
    destination.mkdir(parents=True)
    try:
        for relative, source_file in regular_files(source, excluded_top_level=excluded_top_level):
            target = destination.joinpath(*PurePosixPath(relative).parts)
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source_file, target)
    except BaseException:
        remove_tree(destination, allowed_parent=destination.parent)
        raise


def create_manifest(root: Path) -> dict[str, object]:
    entries = [
        {"path": relative, "size": path.stat().st_size, "sha256": sha256_file(path)}
        for relative, path in regular_files(root, excluded=_ENVELOPE_FILES)
    ]
    if not entries:
        raise PackagingError("a package must contain at least one payload file")
    return {"schema": 1, "files": entries}


def seal_unit(root: Path, receipt_fields: Mapping[str, object]) -> None:
    root = root.resolve(strict=True)
    for envelope in _ENVELOPE_FILES:
        existing = root / envelope
        if existing.exists():
            raise PackagingError(f"reserved package file already exists: {existing}")
    manifest = create_manifest(root)
    manifest_bytes = canonical_json_bytes(manifest)
    (root / MANIFEST_NAME).write_bytes(manifest_bytes)
    receipt = {"schema": 1, **receipt_fields, "manifestSha256": sha256_bytes(manifest_bytes)}
    (root / RECEIPT_NAME).write_bytes(canonical_json_bytes(receipt))
    verify_unit(root)


def _load_object(path: Path) -> dict[str, object]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise PackagingError(f"invalid JSON file {path.name}: {error}") from error
    if not isinstance(value, dict):
        raise PackagingError(f"JSON file must contain an object: {path.name}")
    return value


def verify_unit(root: Path) -> dict[str, object]:
    root = root.resolve(strict=True)
    manifest_path = root / MANIFEST_NAME
    receipt_path = root / RECEIPT_NAME
    if not manifest_path.is_file() or not receipt_path.is_file():
        raise PackagingError("package manifest or receipt is missing")
    manifest = _load_object(manifest_path)
    receipt = _load_object(receipt_path)
    if manifest.get("schema") != 1 or receipt.get("schema") != 1:
        raise PackagingError("unsupported package manifest or receipt schema")
    if receipt.get("manifestSha256") != sha256_file(manifest_path):
        raise PackagingError("package receipt does not match its manifest")
    raw_entries = manifest.get("files")
    if not isinstance(raw_entries, list) or not raw_entries:
        raise PackagingError("package manifest contains no payload files")
    expected: dict[str, tuple[int, str]] = {}
    folded: set[str] = set()
    for entry in raw_entries:
        if not isinstance(entry, dict):
            raise PackagingError("package manifest entry is not an object")
        relative = entry.get("path")
        size = entry.get("size")
        digest = entry.get("sha256")
        if not isinstance(relative, str) or not isinstance(size, int) or size < 0:
            raise PackagingError("package manifest entry has an invalid path or size")
        if not isinstance(digest, str) or len(digest) != 64 or any(c not in "0123456789abcdef" for c in digest):
            raise PackagingError(f"package manifest entry has an invalid hash: {relative!r}")
        parsed = _validate_relative_path(relative)
        folded_name = relative.casefold()
        if relative in expected or folded_name in folded or relative in _ENVELOPE_FILES:
            raise PackagingError(f"duplicate or reserved package manifest path: {relative!r}")
        folded.add(folded_name)
        expected[relative] = (size, digest)
        resolved = root.joinpath(*parsed.parts)
        if resolved.is_symlink() or _is_reparse(resolved) or not resolved.is_file():
            raise PackagingError(f"manifest payload is missing or not a regular file: {relative}")
        if resolved.stat().st_size != size or sha256_file(resolved) != digest:
            raise PackagingError(f"manifest payload failed integrity verification: {relative}")
    actual = {relative for relative, _ in regular_files(root, excluded=_ENVELOPE_FILES)}
    if actual != set(expected):
        added = sorted(actual - set(expected))
        missing = sorted(set(expected) - actual)
        raise PackagingError(f"package file set differs from manifest; additional={added}, missing={missing}")
    return receipt


class FileLock:
    def __init__(self, path: Path, *, timeout_seconds: float = 0.0) -> None:
        self.path = path
        self.timeout_seconds = timeout_seconds
        self._stream = None

    def __enter__(self) -> "FileLock":
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self._stream = self.path.open("a+b")
        self._stream.seek(0, os.SEEK_END)
        if self._stream.tell() == 0:
            self._stream.write(b"\0")
            self._stream.flush()
        deadline = time.monotonic() + self.timeout_seconds
        while True:
            try:
                self._acquire()
                break
            except OSError as error:
                if time.monotonic() >= deadline:
                    self._stream.close()
                    self._stream = None
                    raise PackagingError(f"package resource is busy: {self.path}") from error
                time.sleep(0.05)
        self._stream.seek(0)
        self._stream.truncate()
        self._stream.write(f"pid={os.getpid()}\n".encode("ascii"))
        self._stream.flush()
        return self

    def _acquire(self) -> None:
        assert self._stream is not None
        if os.name == "nt":
            import msvcrt
            self._stream.seek(0)
            msvcrt.locking(self._stream.fileno(), msvcrt.LK_NBLCK, 1)
        else:
            import fcntl
            fcntl.flock(self._stream.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)

    def __exit__(self, _type: object, _value: object, _traceback: object) -> None:
        if self._stream is None:
            return
        if os.name == "nt":
            import msvcrt
            self._stream.seek(0)
            msvcrt.locking(self._stream.fileno(), msvcrt.LK_UNLCK, 1)
        else:
            import fcntl
            fcntl.flock(self._stream.fileno(), fcntl.LOCK_UN)
        self._stream.close()
        self._stream = None


def remove_tree(path: Path, *, allowed_parent: Path) -> None:
    path = path.absolute()
    allowed_parent = allowed_parent.resolve(strict=True)
    if path.parent.resolve(strict=True) != allowed_parent or path == allowed_parent:
        raise PackagingError(f"refusing to remove a path outside the expected directory: {path}")
    if path.exists():
        if path.is_symlink() or _is_reparse(path):
            raise PackagingError(f"refusing to recursively remove a link or reparse point: {path}")
        shutil.rmtree(path)


def atomic_publish(candidate: Path, destination: Path) -> None:
    candidate = candidate.resolve(strict=True)
    destination = destination.absolute()
    destination.parent.mkdir(parents=True, exist_ok=True)
    verify_unit(candidate)
    if candidate.parent.resolve(strict=True) != destination.parent.resolve(strict=True):
        raise PackagingError("atomic publication candidate must be on the destination volume")
    backup = destination.with_name(f".{destination.name}.previous-{secrets.token_hex(6)}")
    moved_old = False
    try:
        if destination.exists():
            if destination.is_symlink() or _is_reparse(destination) or not destination.is_dir():
                raise PackagingError(f"existing package destination is not a regular directory: {destination}")
            verify_unit(destination)
            destination.rename(backup)
            moved_old = True
        candidate.rename(destination)
        verify_unit(destination)
    except BaseException:
        if destination.exists() and not candidate.exists():
            destination.rename(candidate)
        if moved_old and backup.exists() and not destination.exists():
            backup.rename(destination)
        raise
    if moved_old:
        remove_tree(backup, allowed_parent=destination.parent)


@dataclass(frozen=True)
class ProcessResult:
    output: str
    elapsed_seconds: float


def _terminate_process_tree(process: subprocess.Popen[str]) -> None:
    if process.poll() is not None:
        return
    if os.name == "nt":
        subprocess.run(
            ["taskkill.exe", "/PID", str(process.pid), "/T", "/F"],
            stdin=subprocess.DEVNULL,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            check=False,
            shell=False,
        )
    else:
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass


def terminate_process_tree(process: subprocess.Popen[str]) -> None:
    _terminate_process_tree(process)


def run_checked(
    arguments: Sequence[os.PathLike[str] | str],
    *,
    cwd: Path,
    env: Mapping[str, str] | None = None,
    timeout_seconds: float,
    on_line: Callable[[str], None] | None = None,
) -> ProcessResult:
    argv = [os.fspath(value) for value in arguments]
    if not argv or not Path(argv[0]).is_file():
        raise PackagingError(f"required executable was not found: {argv[0] if argv else '<empty>'}")
    if any("\x00" in value for value in argv):
        raise PackagingError("process argument contains a NUL byte")
    creation_flags = subprocess.CREATE_NEW_PROCESS_GROUP if os.name == "nt" else 0
    started = time.monotonic()
    process = subprocess.Popen(
        argv,
        cwd=cwd,
        env=dict(env) if env is not None else None,
        stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        encoding="utf-8",
        errors="replace",
        bufsize=1,
        shell=False,
        creationflags=creation_flags,
        start_new_session=os.name != "nt",
    )
    output: list[str] = []
    total = 0
    lines: queue.Queue[str | None] = queue.Queue()

    def read_output() -> None:
        assert process.stdout is not None
        try:
            for item in process.stdout:
                lines.put(item)
        finally:
            lines.put(None)

    reader = threading.Thread(target=read_output, name="package-process-output", daemon=True)
    reader.start()
    try:
        output_closed = False
        while True:
            if time.monotonic() - started > timeout_seconds:
                raise TimeoutError
            try:
                line = lines.get(timeout=0.05)
            except queue.Empty:
                line = ""
            if line is None:
                output_closed = True
            elif line:
                encoded_size = len(line.encode("utf-8", errors="replace"))
                total += encoded_size
                if total > _MAX_PROCESS_OUTPUT_BYTES:
                    raise PackagingError(f"process output exceeded {_MAX_PROCESS_OUTPUT_BYTES} bytes: {argv[0]}")
                output.append(line)
                if on_line is not None:
                    on_line(line.rstrip("\r\n"))
            if output_closed and process.poll() is not None:
                break
        exit_code = process.wait(timeout=1)
    except TimeoutError as error:
        _terminate_process_tree(process)
        process.wait()
        reader.join(timeout=1)
        if process.stdout is not None:
            process.stdout.close()
        raise PackagingError(f"process timed out after {timeout_seconds:g}s: {argv[0]}") from error
    except BaseException:
        _terminate_process_tree(process)
        process.wait()
        reader.join(timeout=1)
        if process.stdout is not None:
            process.stdout.close()
        raise
    reader.join(timeout=1)
    if process.stdout is not None:
        process.stdout.close()
    joined = "".join(output)
    if exit_code != 0:
        tail = joined[-4096:].replace("\r", "").strip()
        raise PackagingError(f"process failed with exit code {exit_code}: {argv[0]}\n{tail}")
    return ProcessResult(joined, time.monotonic() - started)


def tool_fingerprint(path: Path) -> dict[str, object]:
    resolved = path.resolve(strict=True)
    if not resolved.is_file():
        raise PackagingError(f"tool is not a regular file: {resolved}")
    return {"path": str(resolved), "size": resolved.stat().st_size, "sha256": sha256_file(resolved)}


def tree_fingerprint(root: Path) -> str:
    digest = hashlib.sha256()
    for relative, path in regular_files(root):
        digest.update(relative.encode("utf-8"))
        digest.update(b"\0")
        digest.update(str(path.stat().st_size).encode("ascii"))
        digest.update(b"\0")
        digest.update(bytes.fromhex(sha256_file(path)))
    return digest.hexdigest()


def make_job_directory(root: Path) -> Path:
    root.mkdir(parents=True, exist_ok=True)
    job = root / f"job-{os.getpid()}-{secrets.token_hex(8)}"
    job.mkdir(mode=0o700)
    return job


if __name__ == "__main__":
    print("package_contract is a library; run package_game.py", file=sys.stderr)
    raise SystemExit(2)

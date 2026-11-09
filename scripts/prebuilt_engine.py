"""Integrity checks for an installed Windows engine toolchain."""
from __future__ import annotations

import hashlib
import json
import re
from pathlib import Path, PurePosixPath

from package_contract import PackagingError, sha256_file

MANIFEST = "prebuilt-engine.json"
SCHEMA = "21kb.prebuilt-engine/v1"
TARGETS = {
    **{name: f"bin/{name}.exe" for name in (
        "kb_cli", "kb_editor", "kb_cooker", "kb_game",
        "kb_runtime_asset_pack_validator", "kb_authenticode_signer")},
    **{name: f"bin/{name}.dll" for name in (
        "kb_physics_jolt_plugin", "kb_audio_miniaudio_plugin", "kb_basic_lighting_plugin",
        "kb_ai_plugin", "kb_21kb_particle_plugin", "kb_world2d_plugin")},
}
REQUIRED_FILES = {*TARGETS.values(), "bin/phonon.dll", "bin/shaderc.exe", "bin/dxcompiler.dll",
                  "sources/renderer/shaders/varying.def.sc", "scripts/package_game.py"}
REQUIRED_FILES.update(str(Path(relative).with_suffix(".pdb")).replace("\\", "/")
                      for target, relative in TARGETS.items() if target == "kb_game" or relative.endswith(".dll"))


def _unique_object(items):
    value = {}
    for key, item in items:
        if key in value:
            raise ValueError(f"duplicate manifest key: {key}")
        value[key] = item
    return value


def load(root: Path) -> dict:
    path = root / MANIFEST
    try:
        if path.stat().st_size > 4 * 1024 * 1024:
            raise ValueError("manifest exceeds 4 MiB")
        value = json.loads(path.read_text(encoding="utf-8"), object_pairs_hook=_unique_object)
        if (not isinstance(value, dict) or value.get("schema") != SCHEMA or
                value.get("target") != "Windows.x64" or value.get("engineConfiguration") != "Release"):
            raise ValueError("unsupported prebuilt engine schema, target or configuration")
        files = value.get("files")
        if not isinstance(files, dict) or not REQUIRED_FILES.issubset(files) or len(files) > 10000:
            raise ValueError("manifest is missing required toolchain files")
        for relative, digest in files.items():
            parsed = PurePosixPath(relative)
            if (not relative or "\\" in relative or ":" in relative or parsed.is_absolute() or
                    parsed.as_posix() != relative or any(part in (".", "..") for part in parsed.parts) or
                    not isinstance(digest, str) or re.fullmatch(r"[0-9a-f]{64}", digest) is None):
                raise ValueError(f"invalid prebuilt file entry: {relative!r}")
        return value
    except (OSError, ValueError, TypeError) as error:
        raise PackagingError(f"prebuilt engine manifest is invalid: {error}") from error


def verify_file(root: Path, relative: str, digest: str) -> None:
    path = root / relative
    try:
        if not path.resolve(strict=True).is_relative_to(root.resolve(strict=True)) or not path.is_file():
            raise ValueError("file escapes the installation")
        if sha256_file(path) != digest:
            raise ValueError("SHA-256 mismatch")
    except (OSError, ValueError) as error:
        raise PackagingError(f"prebuilt engine file is invalid: {relative}: {error}") from error


def fingerprint(root: Path) -> str:
    value = load(root)
    for relative, digest in value["files"].items():
        verify_file(root, relative, digest)
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(",", ":")).encode()).hexdigest()


def verify_targets(root: Path, build_root: Path, targets) -> None:
    if root.resolve() != build_root.resolve():
        raise PackagingError("prebuilt engine build directory must be the installed engine root")
    value = load(root)
    for target in targets:
        if target not in TARGETS:
            raise PackagingError(f"prebuilt engine does not contain target {target}")
        relative = TARGETS[target]
        verify_file(root, relative, value["files"][relative])

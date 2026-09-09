#!/usr/bin/env python3
"""Build, cook, finalize, verify, and atomically publish a playable game."""

from __future__ import annotations

import argparse
import gzip
import hashlib
import html
import http.server
import json
import os
import re
import secrets
import shlex
import shutil
import socket
import subprocess
import sys
import tarfile
import tempfile
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
import zipfile
from contextlib import contextmanager
from dataclasses import dataclass
from pathlib import Path, PurePosixPath
from typing import Iterable, Iterator, Mapping, Sequence

from package_contract import (
    FileLock,
    PACKAGE_TARGETS,
    PackagingError,
    atomic_publish,
    canonical_json_bytes,
    copy_tree_exact,
    engine_source_fingerprint,
    emit_diagnostic,
    emit_stage,
    make_job_directory,
    payload_manifest_sha256,
    regular_files,
    remove_tree,
    run_checked,
    runtime_first_frame_observation,
    seal_unit,
    sha256_file,
    tool_fingerprint,
    terminate_process_tree,
    verify_unit,
)
from windows_pe_resources import WindowsResourceError, apply_windows_resources


@dataclass(frozen=True)
class FirstFrameResult:
    probe: str
    observed: str
    payload_manifest_sha256: str

    def receipt_fields(self) -> dict[str, object]:
        return {
            "schema": 1,
            "probe": self.probe,
            "observed": self.observed,
            "payloadManifestSha256": self.payload_manifest_sha256,
        }


@dataclass(frozen=True)
class StageResult:
    tools: tuple[Path, ...]
    first_frame: FirstFrameResult | None = None
    running_android_adb: Path | None = None


def _first_frame_result(target: str, stage: Path) -> FirstFrameResult:
    probe, observed = runtime_first_frame_observation(target)
    return FirstFrameResult(probe, observed, payload_manifest_sha256(stage))


TARGETS = PACKAGE_TARGETS
CONFIGURATIONS = {"Development": "Debug", "Release": "Release"}
_SAFE_EXECUTABLE_NAME = re.compile(r"[A-Za-z0-9][A-Za-z0-9_. -]{0,79}\Z")
_ANDROID_APPLICATION_ID = re.compile(r"[A-Za-z][A-Za-z0-9_]*(?:\.[A-Za-z][A-Za-z0-9_]*)+\Z")
_ANDROID_ALIAS = re.compile(r"[A-Za-z0-9_.-]{1,128}\Z")
_PROJECT_TRANSIENT_ROOTS = frozenset((".cache", "Saved", "Build", "Dist", "Packages", ".git"))


def _is_windows_reserved_device_name(value: str) -> bool:
    base = value.split(".", 1)[0].casefold()
    return base in {"con", "prn", "aux", "nul", "conin$", "conout$"} or (
        len(base) == 4 and base[:3] in {"com", "lpt"} and base[3] in "123456789"
    )


def _is_safe_executable_name(value: str, target: str) -> bool:
    return bool(_SAFE_EXECUTABLE_NAME.fullmatch(value)) and not value.endswith((".", " ")) and (
        target != "Windows.x64" or not _is_windows_reserved_device_name(value)
    )


def _required_executable(name: str) -> Path:
    found = shutil.which(name)
    if found is None:
        raise PackagingError(f"required tool is not installed or not on PATH: {name}")
    path = Path(found).resolve(strict=True)
    if not path.is_file():
        raise PackagingError(f"resolved tool is not a regular file: {path}")
    return path


def _ninja_for_cmake(cmake: Path) -> Path:
    suffix = ".exe" if os.name == "nt" else ""
    adjacent = cmake.parent.parent / "Ninja" / f"ninja{suffix}"
    if adjacent.is_file():
        return adjacent.resolve(strict=True)
    return _required_executable("ninja")


def _existing_file(path: Path, label: str) -> Path:
    resolved = path.expanduser().resolve(strict=True)
    if not resolved.is_file() or resolved.is_symlink():
        raise PackagingError(f"{label} must be a regular file: {resolved}")
    return resolved


def _find_project_file(value: Path) -> Path:
    resolved = value.expanduser().resolve(strict=True)
    if resolved.is_dir():
        resolved /= "Project.21kbproject"
    return _existing_file(resolved, "project descriptor")


def _validate_text(value: str, label: str, maximum: int) -> str:
    normalized = value.strip()
    if not normalized or len(normalized) > maximum or any(ord(c) < 32 for c in normalized):
        raise PackagingError(f"{label} must contain 1 to {maximum} printable characters")
    return normalized


def _project_png_icon(project_file: Path, value: Path) -> Path:
    if ".." in value.parts:
        raise PackagingError("application icon must be a normal path inside the project")
    project_root = project_file.parent.resolve(strict=True)
    candidate = value if value.is_absolute() else project_root / value
    icon = _existing_file(candidate, "application icon")
    try:
        icon.relative_to(project_root)
    except ValueError as error:
        raise PackagingError("application icon must be inside the project") from error
    with icon.open("rb") as stream:
        signature = stream.read(8)
    if icon.suffix.casefold() != ".png" or signature != b"\x89PNG\r\n\x1a\n":
        raise PackagingError("application icon must be a readable PNG file")
    return icon


def _validate_package_work_roots(project_file: Path, build_root: Path, output: Path) -> None:
    project_root = project_file.parent.resolve(strict=True)
    for label, candidate in (("build directory", build_root), ("package output", output)):
        resolved = candidate.resolve(strict=False)
        try:
            resolved.relative_to(project_root)
        except ValueError:
            continue
        raise PackagingError(f"{label} must be outside the project")


def _validate_arguments(args: argparse.Namespace) -> None:
    args.project = _find_project_file(args.project)
    args.engine_root = args.engine_root.expanduser().resolve(strict=True)
    if not (args.engine_root / "CMakeLists.txt").is_file() or not (args.engine_root / "sources").is_dir():
        raise PackagingError(f"engine root is incomplete: {args.engine_root}")
    args.build_root = args.build_root.expanduser().absolute()
    args.output = args.output.expanduser().absolute()
    _validate_package_work_roots(args.project, args.build_root, args.output)
    args.build_root.mkdir(parents=True, exist_ok=True)
    if args.output.name in ("", ".", ".."):
        raise PackagingError("output must name a package directory")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.product_name = _validate_text(args.product_name, "product name", 128)
    args.publisher = _validate_text(args.publisher, "publisher", 128)
    args.version = _validate_text(args.version, "version", 64)
    if not _is_safe_executable_name(args.executable_name, args.target):
        raise PackagingError("executable name contains unsupported characters")
    if args.application_icon is not None:
        args.application_icon = _project_png_icon(args.project, args.application_icon)
    if args.target.startswith("Android."):
        if not _ANDROID_APPLICATION_ID.fullmatch(args.android_application_id):
            raise PackagingError("Android application ID is invalid")
        if args.android_version_code <= 0 or args.android_version_code > 2_100_000_000:
            raise PackagingError("Android version code must be between 1 and 2100000000")
        if not 28 <= args.android_min_sdk <= args.android_target_sdk <= 36:
            raise PackagingError("Android SDK levels must satisfy 28 <= minimum <= target <= 36")
        if args.android_label is not None:
            args.android_label = _validate_text(args.android_label, "Android application label", 128)
        if args.configuration == "Release":
            if args.android_keystore is None:
                raise PackagingError("Android Release requires a keystore")
            args.android_keystore = _existing_file(args.android_keystore, "Android keystore")
            if args.android_signing_broker is not None:
                args.android_signing_broker = _existing_file(args.android_signing_broker, "Android signing broker")
            if not _ANDROID_ALIAS.fullmatch(args.android_key_alias):
                raise PackagingError("Android signing key alias is invalid")


def _copy_project_snapshot(project_file: Path, destination: Path) -> Path:
    source_root = project_file.parent.resolve(strict=True)
    destination.mkdir(parents=True)
    copied_project = False
    for relative, source in regular_files(source_root, excluded_top_level=_PROJECT_TRANSIENT_ROOTS):
        parts = PurePosixPath(relative).parts
        target = destination.joinpath(*parts)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)
        copied_project |= source == project_file
    if not copied_project:
        raise PackagingError("the project descriptor was excluded from its snapshot")
    return destination / project_file.relative_to(source_root)


def _project_source_fingerprint(project_file: Path) -> str:
    root = project_file.parent.resolve(strict=True)
    digest = hashlib.sha256()
    for relative, path in regular_files(root, excluded_top_level=_PROJECT_TRANSIENT_ROOTS):
        digest.update(relative.encode("utf-8"))
        digest.update(b"\0")
        digest.update(str(path.stat().st_size).encode("ascii"))
        digest.update(b"\0")
        digest.update(bytes.fromhex(sha256_file(path)))
    return digest.hexdigest()


def _engine_fingerprint(root: Path) -> str:
    return engine_source_fingerprint(root)


def _build_tool_path(build_root: Path, configuration: str, name: str) -> Path:
    suffix = ".exe" if os.name == "nt" else ""
    candidates = (
        build_root / "bin" / configuration / f"{name}{suffix}",
        build_root / "bin" / f"{name}{suffix}",
        build_root / configuration / f"{name}{suffix}",
        build_root / "renderer" / configuration / f"{name}{suffix}",
        build_root / "game" / configuration / f"{name}{suffix}",
    )
    for candidate in candidates:
        if candidate.is_file():
            return candidate.resolve(strict=True)
    raise PackagingError(f"build did not produce {name} for {configuration} under {build_root}")


def _build_targets(cmake: Path, build_root: Path, configuration: str, targets: Sequence[str], engine_root: Path) -> None:
    run_checked(
        [cmake, "--build", build_root, "--config", configuration, "--target", *targets, "--parallel"],
        cwd=engine_root,
        timeout_seconds=3600,
        on_line=lambda line: emit_diagnostic("Info", line),
    )


def _ensure_host_tools(args: argparse.Namespace, cmake: Path) -> tuple[Path, Path]:
    _build_targets(
        cmake,
        args.build_root,
        CONFIGURATIONS[args.configuration],
        ("kb_cooker", "kb_runtime_asset_pack_validator"),
        args.engine_root,
    )
    configuration = CONFIGURATIONS[args.configuration]
    return (
        _build_tool_path(args.build_root, configuration, "kb_cooker"),
        _build_tool_path(args.build_root, configuration, "kb_runtime_asset_pack_validator"),
    )


def _cook(args: argparse.Namespace, snapshot_project: Path, job: Path, cooker: Path, validator: Path) -> Path:
    output = job / "cook" / "Game.kbpack"
    output.parent.mkdir(parents=True)
    cache = args.build_root / "package-cache" / args.target
    cache.mkdir(parents=True, exist_ok=True)
    command: list[Path | str] = [
        cooker,
        "--project", snapshot_project,
        "--target", args.target,
        "--output", output,
        "--engine-root", args.engine_root,
        "--cache", cache,
    ]
    if args.target == "Windows.x64":
        command.extend(("--runtime-modules-output", output.parent / "RuntimeModules"))
    shaderc = _find_optional_build_tool(args.build_root, CONFIGURATIONS[args.configuration], "shaderc")
    if shaderc is not None:
        command.extend(("--shaderc", shaderc))
    run_checked(
        command,
        cwd=job,
        timeout_seconds=3600,
        on_line=lambda line: emit_diagnostic("Info", line),
    )
    run_checked(
        [validator, args.target, output],
        cwd=job,
        timeout_seconds=300,
        on_line=lambda line: emit_diagnostic("Info", line),
    )
    return output


def _find_optional_build_tool(build_root: Path, configuration: str, name: str) -> Path | None:
    suffix = ".exe" if os.name == "nt" else ""
    matches = [
        build_root / "bin" / configuration / f"{name}{suffix}",
        build_root / "bin" / f"{name}{suffix}",
        build_root / "third_party" / "bgfx.cmake" / "cmake" / "bgfx" / configuration / f"{name}{suffix}",
    ]
    matches.extend(build_root.glob(f"**/{configuration}/{name}{suffix}"))
    return next((path.resolve(strict=True) for path in matches if path.is_file()), None)


def _stage_licenses(engine_root: Path, stage: Path, target: str) -> None:
    sources = {
        "bgfx.rst": engine_root / "third_party/bgfx.cmake/bgfx/docs/license.rst",
        "bx.txt": engine_root / "third_party/bgfx.cmake/bx/LICENSE",
        "bimg.txt": engine_root / "third_party/bgfx.cmake/bimg/LICENSE",
        "flecs.txt": engine_root / "third_party/flecs/LICENSE",
        "jolt.txt": engine_root / "third_party/jolt/LICENSE",
        "lua.txt": engine_root / "third_party/licenses/lua-5.4.8.txt",
        "miniaudio.txt": engine_root / "third_party/miniaudio/LICENSE",
        "ufbx.txt": engine_root / "third_party/ufbx/LICENSE",
    }
    included = ["bgfx", "bx", "bimg", "Flecs", "Jolt Physics", "Lua", "miniaudio", "ufbx"]
    if target.startswith("Android."):
        sources["androidx-apache-2.0.txt"] = engine_root / "third_party/bgfx.cmake/bgfx/3rdparty/spirv-tools/LICENSE"
        included.append("AndroidX AppCompat, Games Activity, and their AndroidX dependencies")
    if target == "WebGPU.wasm32":
        sources["dawn.txt"] = engine_root / "third_party/bgfx.cmake/bgfx/3rdparty/dawn/LICENSE"
        included.append("Dawn WebGPU")
    licenses = stage / "Licenses"
    licenses.mkdir()
    for name, source in sources.items():
        if not source.is_file():
            raise PackagingError(f"required third-party license is missing: {source}")
        shutil.copy2(source, licenses / name)
    (stage / "THIRD_PARTY_NOTICES.txt").write_text(
        "This product includes " + ", ".join(included) + ".\n"
        "Their license texts are included in the Licenses directory.\n",
        encoding="utf-8",
        newline="\n",
    )


def _stage_windows(args: argparse.Namespace, cmake: Path, pack: Path, stage: Path, job: Path) -> StageResult:
    configuration = CONFIGURATIONS[args.configuration]
    plugin_targets = (
        "kb_physics_jolt_plugin",
        "kb_audio_miniaudio_plugin",
        "kb_basic_lighting_plugin",
        "kb_21kb_particle_plugin",
    )
    _build_targets(cmake, args.build_root, configuration, ("kb_game", *plugin_targets), args.engine_root)
    game = _build_tool_path(args.build_root, configuration, "kb_game")
    destination = stage / f"{args.executable_name}.exe"
    shutil.copy2(game, destination)
    try:
        apply_windows_resources(
            destination,
            product_name=args.product_name,
            publisher=args.publisher,
            version=args.version,
            executable_name=args.executable_name,
            development=args.configuration == "Development",
            icon=args.application_icon,
        )
    except WindowsResourceError as error:
        raise PackagingError(str(error)) from error
    shutil.copy2(pack, stage / "Game.kbpack")
    custom_modules = pack.parent / "RuntimeModules"
    if custom_modules.is_dir():
        copy_tree_exact(custom_modules, stage / "RuntimeModules")
    plugin_paths: list[Path] = []
    for target in plugin_targets:
        expected = f"{target}.dll"
        candidates = [
            args.build_root / target.removeprefix("kb_").removesuffix("_plugin") / configuration / expected,
            args.build_root / "21kb_particle" / configuration / expected,
            game.parent / expected,
        ]
        plugin = next((path.resolve(strict=True) for path in candidates if path.is_file()), None)
        if plugin is None:
            raise PackagingError(f"Windows player provider was not produced: {expected}")
        shutil.copy2(plugin, stage / expected)
        plugin_paths.append(plugin)
    _stage_licenses(args.engine_root, stage, args.target)
    if destination.read_bytes()[:2] != b"MZ":
        raise PackagingError("Windows player does not contain a valid PE header")
    smoke = job / "windows-first-frame"
    copy_tree_exact(stage, smoke)
    try:
        output = run_checked(
            [smoke / destination.name, "--frames=1"], cwd=smoke, timeout_seconds=120
        ).output
        if "frames=1" not in output or "shutdown=clean" not in output or "rendered=1" not in output:
            raise PackagingError("Windows player did not prove one rendered frame and clean shutdown")
    finally:
        if smoke.exists():
            remove_tree(smoke, allowed_parent=job)
    return StageResult((game, *plugin_paths), _first_frame_result(args.target, stage))


def _android_sdk() -> Path:
    value = os.environ.get("ANDROID_SDK_ROOT") or os.environ.get("ANDROID_HOME")
    if value:
        root = Path(value).expanduser().resolve(strict=True)
    elif os.name == "nt" and os.environ.get("LOCALAPPDATA"):
        root = (Path(os.environ["LOCALAPPDATA"]) / "Android" / "Sdk").resolve(strict=True)
    else:
        raise PackagingError("Android SDK location is not configured")
    if not (root / "build-tools").is_dir():
        raise PackagingError(f"Android SDK has no build-tools installation: {root}")
    return root


def _latest_android_build_tools(sdk: Path) -> Path:
    def version_key(path: Path) -> tuple[int, ...]:
        numbers = re.findall(r"\d+", path.name)
        return tuple(int(value) for value in numbers)
    candidates = sorted((path for path in (sdk / "build-tools").iterdir() if path.is_dir()), key=version_key, reverse=True)
    if not candidates:
        raise PackagingError("Android SDK has no build-tools version")
    return candidates[0]


def _android_tool(root: Path, name: str) -> Path:
    suffix = ".exe" if os.name == "nt" else ""
    path = root / f"{name}{suffix}"
    if not path.is_file():
        raise PackagingError(f"Android build tool is missing: {path}")
    return path.resolve(strict=True)


def _java() -> Path:
    java_home = os.environ.get("JAVA_HOME")
    if java_home:
        candidate = Path(java_home) / "bin" / ("java.exe" if os.name == "nt" else "java")
        if candidate.is_file():
            return candidate.resolve(strict=True)
    return _required_executable("java")


def _apksigner_jar(build_tools: Path) -> Path:
    path = build_tools / "lib" / "apksigner.jar"
    if not path.is_file():
        raise PackagingError(f"Android APK signer library is missing: {path}")
    return path.resolve(strict=True)


def _apksigner_command(build_tools: Path, arguments: Sequence[Path | str]) -> list[Path | str]:
    return [
        _java(), "-classpath", _apksigner_jar(build_tools),
        "com.android.apksigner.ApkSignerTool", *arguments,
    ]


def _verify_android_apk(
    apk: Path,
    *,
    profile: str,
    application_id: str,
    version_name: str,
    version_code: int,
    application_label: str,
    min_sdk: int,
    target_sdk: int,
    expects_custom_icon: bool,
    build_tools: Path,
    expected_debuggable: bool,
) -> None:
    if not apk.is_file() or apk.read_bytes()[:4] != b"PK\x03\x04":
        raise PackagingError("Android package is not a readable APK")
    try:
        with zipfile.ZipFile(apk, "r") as archive:
            names = archive.namelist()
            if len(names) != len(set(names)) or len({name.casefold() for name in names}) != len(names):
                raise PackagingError("Android APK contains duplicate or case-colliding entries")
            required = {"assets/game.kbpack", "lib/arm64-v8a/libkb_game_android.so"}
            required.update({
                "assets/THIRD_PARTY_NOTICES.txt",
                "assets/Licenses/bgfx.rst",
                "assets/Licenses/bimg.txt",
                "assets/Licenses/bx.txt",
                "assets/Licenses/flecs.txt",
                "assets/Licenses/jolt.txt",
                "assets/Licenses/lua.txt",
                "assets/Licenses/miniaudio.txt",
                "assets/Licenses/ufbx.txt",
                "assets/Licenses/androidx-apache-2.0.txt",
            })
            if not required.issubset(names):
                raise PackagingError(f"Android APK is missing runtime entries: {sorted(required - set(names))}")
            abis = {PurePosixPath(name).parts[1] for name in names if name.startswith("lib/") and len(PurePosixPath(name).parts) >= 3}
            if abis != {"arm64-v8a"}:
                raise PackagingError(f"Android APK contains an unexpected ABI set: {sorted(abis)}")
            native = archive.read("lib/arm64-v8a/libkb_game_android.so")
            if profile.encode("ascii") not in native:
                raise PackagingError("Android native host does not contain the requested exact profile identity")
    except zipfile.BadZipFile as error:
        raise PackagingError("Android APK central directory is invalid") from error
    zipalign = _android_tool(build_tools, "zipalign")
    run_checked(
        _apksigner_command(build_tools, ["verify", "--verbose", "--print-certs", "-Werr", apk]),
        cwd=apk.parent,
        timeout_seconds=120,
    )
    run_checked([zipalign, "-c", "-P", "16", "4", apk], cwd=apk.parent, timeout_seconds=120)
    aapt = _android_tool(build_tools, "aapt2")
    badging = run_checked([aapt, "dump", "badging", apk], cwd=apk.parent, timeout_seconds=120).output
    if f"package: name='{application_id}'" not in badging:
        raise PackagingError("Android APK application ID differs from package settings")
    if f"versionCode='{version_code}'" not in badging or f"versionName='{version_name}'" not in badging:
        raise PackagingError("Android APK version differs from package settings")
    if f"minSdkVersion:'{min_sdk}'" not in badging or f"targetSdkVersion:'{target_sdk}'" not in badging:
        raise PackagingError("Android APK SDK contract differs from package settings")
    labels = re.findall(r"(?m)^application-label(?:-[^:]*)?:'(.*)'$", badging)
    if application_label not in labels:
        raise PackagingError("Android APK label differs from package settings")
    if "native-code: 'arm64-v8a'" not in badging:
        raise PackagingError("Android APK badging does not declare exactly arm64-v8a")
    if expects_custom_icon and "kb_custom_launcher" not in badging:
        raise PackagingError("Android APK does not reference the selected application icon")
    debuggable = " application-debuggable" in badging or "application-debuggable" in badging
    if debuggable != expected_debuggable:
        raise PackagingError("Android APK debuggability differs from the requested configuration")
    if profile not in ("Android.ASTC.arm64", "Android.ETC2.arm64"):
        raise PackagingError("Android package profile is not an exact texture-family target")


def _request_android_signature(
    broker: Path | None,
    keystore: Path,
    alias: str,
    aligned: Path,
    signed: Path,
    apksigner_jar: Path,
    job: Path,
) -> None:
    request = job / "android-signing-request.json"
    response = job / "android-signing-response.json"
    session = secrets.token_hex(16)
    request.write_bytes(canonical_json_bytes({
        "schema": 1,
        "session": session,
        "keystore": str(keystore),
        "keyAlias": alias,
        "inputApk": str(aligned),
        "outputApk": str(signed),
        "java": str(_java()),
        "apksignerJar": str(apksigner_jar),
    }))
    if broker is not None:
        run_checked([broker, "--request", request, "--response", response], cwd=job, timeout_seconds=300)
    else:
        print(f"SIGNING_REQUEST|{request.resolve(strict=True)}|{response.absolute()}", flush=True)
        deadline = time.monotonic() + 180.0
        while not response.is_file():
            if time.monotonic() >= deadline:
                raise PackagingError("Android signing broker did not answer within 180 seconds")
            time.sleep(0.05)
    if not response.is_file():
        raise PackagingError("Android signing broker did not create a response")
    try:
        value = json.loads(response.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise PackagingError("Android signing broker returned an invalid response") from error
    if value != {"schema": 1, "session": session, "succeeded": True} or not signed.is_file():
        raise PackagingError("Android signing broker refused or did not create the signed APK")


def _android_adb() -> Path:
    adb = _android_sdk() / "platform-tools" / ("adb.exe" if os.name == "nt" else "adb")
    if not adb.is_file():
        raise PackagingError(f"Android platform tool is missing: {adb}")
    return adb.resolve(strict=True)


def _android_force_stop(adb: Path, application_id: str, cwd: Path) -> None:
    run_checked([adb, "shell", "am", "force-stop", application_id], cwd=cwd, timeout_seconds=30)


def _try_android_force_stop(adb: Path, application_id: str, cwd: Path) -> None:
    try:
        _android_force_stop(adb, application_id, cwd)
    except (OSError, PackagingError) as error:
        emit_diagnostic("Warning", f"Android runtime cleanup failed: {error}")


def _verify_android_first_frame(args: argparse.Namespace, apk: Path, adb: Path) -> None:
    try:
        run_checked([adb, "install", "-r", apk], cwd=apk.parent, timeout_seconds=300)
        _android_force_stop(adb, args.android_application_id, apk.parent)
        run_checked([adb, "logcat", "-c"], cwd=apk.parent, timeout_seconds=30)
        run_checked(
            [adb, "shell", "monkey", "-p", args.android_application_id, "1"],
            cwd=apk.parent,
            timeout_seconds=60,
        )
        expected = runtime_first_frame_observation(args.target)[1]
        deadline = time.monotonic() + 60.0
        latest = ""
        while time.monotonic() < deadline:
            latest = run_checked(
                [
                    adb, "logcat", "-d", "-v", "brief", "21kb:I", "AndroidRuntime:E",
                    "libc:F", "DEBUG:F", "*:S",
                ],
                cwd=apk.parent,
                timeout_seconds=30,
            ).output
            if expected in latest:
                return
            if (
                "required-texture-capability=missing" in latest
                or "FATAL EXCEPTION" in latest
                or "Fatal signal" in latest
            ):
                raise PackagingError(f"Android launch failed before its first frame: {latest[-2000:].strip()}")
            time.sleep(0.25)
        raise PackagingError(
            f"Android launch did not prove the requested profile and first frame: {latest[-2000:].strip()}"
        )
    except BaseException:
        _try_android_force_stop(adb, args.android_application_id, apk.parent)
        raise


def _stage_android(args: argparse.Namespace, pack: Path, stage: Path, job: Path) -> StageResult:
    sdk = _android_sdk()
    build_tools = _latest_android_build_tools(sdk)
    gradle_root = args.engine_root / "platform/android"
    gradle_wrapper = gradle_root / "gradle/wrapper/gradle-wrapper.jar"
    if not gradle_wrapper.is_file():
        raise PackagingError("Android Gradle wrapper library is missing")
    flavor = "astc" if args.target == "Android.ASTC.arm64" else "etc2"
    build_type = "Debug" if args.configuration == "Development" else "Release"
    task = f"assemble{flavor.capitalize()}{build_type}"
    validator = _build_tool_path(args.build_root, CONFIGURATIONS[args.configuration], "kb_runtime_asset_pack_validator")
    legal_assets = job / "android-legal-assets"
    legal_assets.mkdir()
    _stage_licenses(args.engine_root, legal_assets, args.target)
    command: list[Path | str] = [
        _java(), "-classpath", gradle_wrapper, "org.gradle.wrapper.GradleWrapperMain",
        task,
        f"-PkbAssetPack={pack}",
        f"-PkbRuntimeAssetPackValidator={validator}",
        f"-PkbTargetProfile={args.target}",
        f"-PkbApplicationId={args.android_application_id}",
        f"-PkbVersionCode={args.android_version_code}",
        f"-PkbVersionName={args.version}",
        f"-PkbApplicationLabel={args.android_label or args.product_name}",
        f"-PkbLegalAssets={legal_assets}",
        f"-PkbMinSdk={args.android_min_sdk}",
        f"-PkbTargetSdk={args.android_target_sdk}",
        "--no-daemon",
    ]
    if args.application_icon is not None:
        command.append(f"-PkbApplicationIcon={args.application_icon}")
    run_checked(
        command,
        cwd=gradle_root,
        timeout_seconds=3600,
        on_line=lambda line: emit_diagnostic("Info", line),
    )
    output_directory = args.engine_root / "platform/android/app/build/outputs/apk" / flavor / build_type.casefold()
    apks = sorted(output_directory.glob("*.apk"))
    if len(apks) != 1:
        raise PackagingError(f"Android build produced {len(apks)} APK files instead of one in {output_directory}")
    source_apk = apks[0]
    destination = stage / f"{args.executable_name}-{flavor}.apk"
    if args.configuration == "Release":
        aligned = job / "android-aligned.apk"
        signed = job / "android-signed.apk"
        zipalign = _android_tool(build_tools, "zipalign")
        run_checked([zipalign, "-f", "-P", "16", "4", source_apk, aligned], cwd=job, timeout_seconds=120)
        assert args.android_keystore is not None
        _request_android_signature(
            args.android_signing_broker,
            args.android_keystore,
            args.android_key_alias,
            aligned,
            signed,
            _apksigner_jar(build_tools),
            job,
        )
        _verify_android_apk(
            signed,
            profile=args.target,
            application_id=args.android_application_id,
            version_name=args.version,
            version_code=args.android_version_code,
            application_label=args.android_label or args.product_name,
            min_sdk=args.android_min_sdk,
            target_sdk=args.android_target_sdk,
            expects_custom_icon=args.application_icon is not None,
            build_tools=build_tools,
            expected_debuggable=False,
        )
        shutil.copy2(signed, destination)
    else:
        shutil.copy2(source_apk, destination)
    _verify_android_apk(
        destination,
        profile=args.target,
        application_id=args.android_application_id,
        version_name=args.version,
        version_code=args.android_version_code,
        application_label=args.android_label or args.product_name,
        min_sdk=args.android_min_sdk,
        target_sdk=args.android_target_sdk,
        expects_custom_icon=args.application_icon is not None,
        build_tools=build_tools,
        expected_debuggable=args.configuration == "Development",
    )
    tools = (
        _java(),
        gradle_wrapper.resolve(strict=True),
        _apksigner_jar(build_tools),
        _android_tool(build_tools, "zipalign"),
    )
    if not args.launch:
        return StageResult(tools)
    adb = _android_adb()
    _verify_android_first_frame(args, destination, adb)
    try:
        return StageResult((*tools, adb), _first_frame_result(args.target, stage), adb)
    except BaseException:
        _try_android_force_stop(adb, args.android_application_id, stage)
        raise


def _emsdk_root(args: argparse.Namespace) -> Path:
    value = args.emsdk or (Path(os.environ["EMSDK"]) if os.environ.get("EMSDK") else None)
    if value is None:
        raise PackagingError("Web packaging requires --emsdk or the EMSDK environment variable")
    root = value.expanduser().resolve(strict=True)
    toolchain = root / "upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake"
    if not toolchain.is_file():
        raise PackagingError(f"Emscripten CMake toolchain is missing: {toolchain}")
    return root


def _chrome() -> Path:
    candidates: list[Path] = []
    for name in ("chrome", "google-chrome", "chromium", "msedge"):
        found = shutil.which(name)
        if found:
            candidates.append(Path(found))
    if os.name == "nt":
        for variable, relative in (
            ("PROGRAMFILES", "Google/Chrome/Application/chrome.exe"),
            ("PROGRAMFILES(X86)", "Microsoft/Edge/Application/msedge.exe"),
            ("LOCALAPPDATA", "Google/Chrome/Application/chrome.exe"),
        ):
            if os.environ.get(variable):
                candidates.append(Path(os.environ[variable]) / relative)
    for candidate in candidates:
        if candidate.is_file():
            return candidate.resolve(strict=True)
    raise PackagingError("Chrome or Edge is required for browser package verification")


def _verify_web_first_frame(stage: Path, html_name: str, backend: str, job: Path) -> Path:
    chrome = _chrome()

    class QuietHandler(http.server.SimpleHTTPRequestHandler):
        def log_message(self, _format: str, *args: object) -> None:
            del args

    handler = lambda *values: QuietHandler(*values, directory=str(stage))  # noqa: E731
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), handler)
    thread = threading.Thread(target=server.serve_forever, name="package-web-smoke", daemon=True)
    thread.start()
    profile = job / f"chrome-{backend.casefold()}"
    expected_backend = backend.casefold()
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as reservation:
        reservation.bind(("127.0.0.1", 0))
        debug_port = reservation.getsockname()[1]
    page_url = (
        f"http://127.0.0.1:{server.server_port}/"
        f"{urllib.parse.quote(html_name, safe='')}"
    )
    creation_flags = subprocess.CREATE_NEW_PROCESS_GROUP if os.name == "nt" else 0
    browser_backend_flags = (
        [
            "--enable-unsafe-webgpu",
        ]
        if backend == "WEBGPU"
        else [
            "--enable-unsafe-swiftshader",
            "--use-gl=angle",
            "--use-angle=swiftshader-webgl",
        ]
    )
    browser_log = job / f"chrome-{backend.casefold()}.log"
    try:
        browser_log_stream = browser_log.open("wb")
    except BaseException:
        server.shutdown()
        server.server_close()
        thread.join(timeout=5)
        raise

    def browser_log_tail() -> str:
        try:
            with browser_log.open("rb") as stream:
                stream.seek(0, os.SEEK_END)
                size = stream.tell()
                stream.seek(max(0, size - 4096), os.SEEK_SET)
                return stream.read().decode("utf-8", errors="replace").strip()
        except OSError:
            return ""

    try:
        process = subprocess.Popen(
            [
                chrome,
                "--headless=new",
                "--disable-background-networking",
                "--disable-component-update",
                "--disable-default-apps",
                "--no-first-run",
                "--no-default-browser-check",
                "--enable-logging=stderr",
                *browser_backend_flags,
                f"--remote-debugging-port={debug_port}",
                f"--user-data-dir={profile}",
                page_url,
            ],
            cwd=stage,
            stdin=subprocess.DEVNULL,
            stdout=browser_log_stream,
            stderr=subprocess.STDOUT,
            shell=False,
            close_fds=True,
            creationflags=creation_flags,
            start_new_session=os.name != "nt",
        )
    except BaseException:
        browser_log_stream.close()
        server.shutdown()
        server.server_close()
        thread.join(timeout=5)
        raise
    last_fragment = "browser-not-ready"
    pending_abort_since: float | None = None
    try:
        deadline = time.monotonic() + 120.0
        while time.monotonic() < deadline:
            if process.poll() is not None:
                raise PackagingError(
                    f"{backend} browser exited before its first frame with code {process.returncode}"
                )
            try:
                with urllib.request.urlopen(
                    f"http://127.0.0.1:{debug_port}/json/list", timeout=2.0
                ) as response:
                    targets = json.load(response)
            except (OSError, urllib.error.URLError, json.JSONDecodeError):
                time.sleep(0.1)
                continue
            for target in targets:
                if target.get("type") != "page":
                    continue
                target_url = target.get("url", "")
                if not target_url.startswith(page_url):
                    continue
                fragment = urllib.parse.unquote(urllib.parse.urlsplit(target_url).fragment)
                last_fragment = fragment or "page-loaded"
                if fragment == f"kb-ready-{expected_backend}":
                    return chrome
                if fragment.startswith("kb-error-"):
                    if fragment.startswith("kb-error-abort-"):
                        if pending_abort_since is None:
                            pending_abort_since = time.monotonic()
                        if time.monotonic() - pending_abort_since < 2.0:
                            continue
                    log_tail = browser_log_tail()
                    raise PackagingError(
                        f"{backend} browser smoke reported runtime error: "
                        f"{fragment.removeprefix('kb-error-')}"
                        + (f"; browser log: {log_tail}" if log_tail else "")
                    )
            time.sleep(0.1)
        raise PackagingError(
            f"{backend} browser smoke timed out before its first frame "
            f"(last stage: {last_fragment})"
        )
    finally:
        terminate_process_tree(process)
        process.wait()
        browser_log_stream.close()
        server.shutdown()
        server.server_close()
        thread.join(timeout=5)


def _stage_web(args: argparse.Namespace, cmake: Path, pack: Path, stage: Path, job: Path) -> StageResult:
    emsdk = _emsdk_root(args)
    ninja = _ninja_for_cmake(cmake)
    configuration = CONFIGURATIONS[args.configuration]
    web_build = args.build_root / "packages" / args.target / configuration
    web_build.mkdir(parents=True, exist_ok=True)
    backend = "WEBGPU" if args.target == "WebGPU.wasm32" else "WEBGL"
    web_target = "kb_game_webgpu" if args.target == "WebGPU.wasm32" else "kb_game_webgl"
    run_checked(
        [
            cmake,
            "-S", args.engine_root,
            "-B", web_build,
            "-G", "Ninja",
            f"-DCMAKE_MAKE_PROGRAM={ninja}",
            f"-DCMAKE_TOOLCHAIN_FILE={emsdk / 'upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake'}",
            f"-DCMAKE_BUILD_TYPE={configuration}",
            "-DKB_BUILD_EDITOR=OFF",
            "-DKB_BUILD_RENDERER=ON",
            "-DKB_BUILD_PACKAGED_GAME_HOST=ON",
            "-DKB_BUILD_PROVIDER_MODULES_AS_DLL=OFF",
            "-DKB_BUILD_GRAPH_SHADERC=OFF",
            "-DKB_GENERATE_RENDERER_SHADERS=OFF",
            "-DBUILD_TESTING=OFF",
            "-DBGFX_OPENGLES_VERSION=30",
            f"-DKB_WEB_RENDERER={backend}",
        ],
        cwd=args.engine_root,
        timeout_seconds=1800,
        on_line=lambda line: emit_diagnostic("Info", line),
    )
    _build_targets(cmake, web_build, configuration, (web_target,), args.engine_root)
    candidates = [path for path in (web_build / "bin").glob("Game.*") if path.suffix in (".html", ".js", ".wasm", ".data")]
    by_suffix: dict[str, list[Path]] = {}
    for candidate in candidates:
        by_suffix.setdefault(candidate.suffix, []).append(candidate)
    for required in (".html", ".js", ".wasm"):
        if len(by_suffix.get(required, [])) != 1:
            raise PackagingError(f"{backend} build did not produce exactly one Game{required}")
    for suffix, files in sorted(by_suffix.items()):
        if len(files) != 1:
            raise PackagingError(f"{backend} build produced ambiguous {suffix} artifacts")
        shutil.copy2(files[0], stage / f"{args.executable_name}{suffix}")
    shutil.copy2(pack, stage / "Game.kbpack")
    _stage_licenses(args.engine_root, stage, args.target)
    html_path = stage / f"{args.executable_name}.html"
    javascript_path = stage / f"{args.executable_name}.js"
    html_text = html_path.read_text(encoding="utf-8", errors="strict")
    javascript_text = javascript_path.read_text(encoding="utf-8", errors="strict")
    for suffix in (".js", ".wasm", ".data"):
        html_text = html_text.replace(f"Game{suffix}", f"{args.executable_name}{suffix}")
        javascript_text = javascript_text.replace(f"Game{suffix}", f"{args.executable_name}{suffix}")
    title = html.escape(args.product_name, quote=False)
    if re.search(r"<title>.*?</title>", html_text, flags=re.IGNORECASE | re.DOTALL):
        html_text = re.sub(
            r"<title>.*?</title>", f"<title>{title}</title>", html_text,
            count=1, flags=re.IGNORECASE | re.DOTALL,
        )
    elif "</head>" in html_text:
        html_text = html_text.replace("</head>", f"<title>{title}</title></head>", 1)
    else:
        raise PackagingError("Web HTML shell has no head element for product metadata")
    html_text = html_text.replace(
        "</head>",
        f'<meta name="author" content="{html.escape(args.publisher, quote=True)}">'
        f'<meta name="application-version" content="{html.escape(args.version, quote=True)}"></head>',
        1,
    )
    runtime_error_hook = (
        "<script>"
        "function kbReportRuntimeError(kind,value){"
        "const stage=location.hash.slice(1);"
        "const detail=value&&value.stack?value.stack:String(value);"
        "location.hash='kb-error-'+kind+'-'+encodeURIComponent(detail.slice(0,1536))"
        "+'-after-'+encodeURIComponent(stage.slice(0,384));"
        "}"
        "addEventListener('error',function(event){"
        "kbReportRuntimeError('javascript',event.error||event.message||'unknown');"
        "});"
        "addEventListener('unhandledrejection',function(event){"
        "kbReportRuntimeError('promise',event.reason||'unknown');"
        "});"
        "</script>"
    )
    html_text = html_text.replace("</head>", runtime_error_hook + "</head>", 1)
    module_ready = "      };\n      setStatus('Downloading...');"
    if module_ready not in html_text:
        raise PackagingError("Web HTML shell has no supported runtime module definition")
    html_text = html_text.replace(
        module_ready,
        "      };\n"
        "      Module.onAbort = (reason) => kbReportRuntimeError('abort', new Error(reason));\n"
        "      setStatus('Downloading...');",
        1,
    )
    if args.application_icon is not None:
        icon_suffix = args.application_icon.suffix.casefold()
        if icon_suffix not in (".png", ".ico", ".svg"):
            raise PackagingError("Web application icon must be PNG, ICO, or SVG")
        icon_name = f"ApplicationIcon{icon_suffix}"
        shutil.copy2(args.application_icon, stage / icon_name)
        link = f'<link rel="icon" href="{icon_name}">'
        if "</head>" not in html_text:
            raise PackagingError("Web HTML shell has no head element for the application icon")
        html_text = html_text.replace("</head>", link + "</head>", 1)
    html_path.write_text(html_text, encoding="utf-8", newline="\n")
    javascript_path.write_text(javascript_text, encoding="utf-8", newline="\n")
    if (stage / f"{args.executable_name}.wasm").read_bytes()[:4] != b"\x00asm":
        raise PackagingError(f"{backend} WebAssembly module has an invalid header")
    final_html = html_path.read_text(encoding="utf-8", errors="strict")
    javascript = javascript_path.read_text(encoding="utf-8", errors="strict")
    if f"{args.executable_name}.js" not in final_html or "WebAssembly" not in javascript:
        raise PackagingError(f"{backend} loader does not reference its JavaScript/WebAssembly runtime")
    chrome = _verify_web_first_frame(stage, html_path.name, backend, job)
    return StageResult(
        (ninja, emsdk / "upstream/emscripten/emcc.py", chrome),
        _first_frame_result(args.target, stage),
    )


def _create_deterministic_tar(
    source: Path,
    destination: Path,
    *,
    executable_name: str | None = None,
) -> None:
    with destination.open("wb") as output:
        with gzip.GzipFile(filename="", mode="wb", fileobj=output, mtime=0) as compressed:
            with tarfile.open(fileobj=compressed, mode="w", format=tarfile.PAX_FORMAT) as archive:
                for relative, path in regular_files(source):
                    info = archive.gettarinfo(str(path), arcname=relative)
                    if relative == executable_name:
                        info.mode = 0o755
                    info.uid = info.gid = 0
                    info.uname = info.gname = ""
                    info.mtime = 0
                    with path.open("rb") as stream:
                        archive.addfile(info, stream)


def _stage_linux_local(args: argparse.Namespace, cmake: Path, pack: Path, stage: Path) -> list[Path]:
    if sys.platform != "linux":
        raise PackagingError("local Linux packaging must run on Linux")
    configuration = CONFIGURATIONS[args.configuration]
    linux_build = args.build_root / "packages" / args.target / configuration
    linux_build.mkdir(parents=True, exist_ok=True)
    run_checked(
        [
            cmake,
            "-S", args.engine_root,
            "-B", linux_build,
            f"-DCMAKE_BUILD_TYPE={configuration}",
            "-DKB_BUILD_EDITOR=OFF",
            "-DKB_BUILD_RENDERER=ON",
            "-DKB_BUILD_PACKAGED_GAME_HOST=ON",
            "-DKB_BUILD_PROVIDER_MODULES_AS_DLL=OFF",
            "-DKB_BUILD_GRAPH_SHADERC=OFF",
            "-DBUILD_TESTING=OFF",
        ],
        cwd=args.engine_root,
        timeout_seconds=1800,
        on_line=lambda line: emit_diagnostic("Info", line),
    )
    _build_targets(cmake, linux_build, configuration, ("kb_game_linux",), args.engine_root)
    game = _build_tool_path(linux_build, configuration, "kb_game_linux")
    destination = stage / args.executable_name
    shutil.copy2(game, destination)
    destination.chmod(destination.stat().st_mode | 0o111)
    shutil.copy2(pack, stage / "Game.kbpack")
    _stage_licenses(args.engine_root, stage, args.target)
    if destination.read_bytes()[:4] != b"\x7fELF":
        raise PackagingError("Linux player does not contain a valid ELF header")
    ldd = _required_executable("ldd")
    dependencies = run_checked([ldd, destination], cwd=stage, timeout_seconds=120).output
    if "not found" in dependencies:
        raise PackagingError("Linux player has unresolved shared-library dependencies")
    xvfb = _required_executable("xvfb-run")
    smoke = run_checked([xvfb, "-a", destination, "--frames=1"], cwd=stage, timeout_seconds=180).output
    if "frames=1" not in smoke or "rendered=1" not in smoke or "shutdown=clean" not in smoke:
        raise PackagingError("Linux player did not prove a clean first frame")
    (stage / "linux-build.receipt.json").write_bytes(canonical_json_bytes({
        "schema": 1,
        "configuration": configuration,
        "engineSha256": args.engine_fingerprint,
        "executableSha256": sha256_file(destination),
        "assetPackSha256": sha256_file(stage / "Game.kbpack"),
        "firstFrame": True,
    }))
    return [game, ldd, xvfb]


def _stage_linux_remote(args: argparse.Namespace, pack: Path, stage: Path, job: Path) -> list[Path]:
    host = args.linux_host or os.environ.get("KB_LINUX_PACKAGE_HOST", "")
    user = args.linux_user or os.environ.get("KB_LINUX_PACKAGE_USER", "")
    host_key = args.linux_host_key or os.environ.get("KB_LINUX_PACKAGE_HOST_KEY", "")
    if not host or not user or not host_key:
        raise PackagingError("Linux build machine is not configured; host, user, and pinned host key are required")
    if not re.fullmatch(r"[A-Za-z0-9.-]{1,253}", host) or not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_-]{0,31}", user):
        raise PackagingError("Linux build machine host or user is invalid")
    key_parts = host_key.split()
    if len(key_parts) != 2 or key_parts[0] not in ("ssh-ed25519", "ecdsa-sha2-nistp256", "rsa-sha2-512") or not re.fullmatch(r"[A-Za-z0-9+/=]+", key_parts[1]):
        raise PackagingError("Linux host key must contain an allowlisted key type and base64 public key")
    if not 1 <= args.linux_port <= 65535:
        raise PackagingError("Linux SSH port is invalid")
    linux_root = PurePosixPath(args.linux_engine_root)
    if not linux_root.is_absolute() or any(part in ("", ".", "..") for part in linux_root.parts) or not re.fullmatch(r"[A-Za-z0-9_./-]+", args.linux_engine_root):
        raise PackagingError("Linux engine root must be a safe absolute POSIX path")
    ssh = _required_executable("ssh")
    scp = _required_executable("scp")
    identity_value = args.linux_identity or (
        Path(os.environ["KB_LINUX_PACKAGE_IDENTITY"]) if os.environ.get("KB_LINUX_PACKAGE_IDENTITY") else None
    )
    identity = _existing_file(identity_value, "Linux SSH identity") if identity_value is not None else None
    remote_job = f"/tmp/21kb-package-{secrets.token_hex(12)}"
    source_archive = job / "linux-input.tar.gz"
    transport = job / "linux-transport"
    transport.mkdir()
    shutil.copy2(pack, transport / "Game.kbpack")
    _create_deterministic_tar(transport, source_archive)
    helper = args.engine_root / "scripts/package_linux_guest.py"
    if not helper.is_file():
        raise PackagingError("Linux guest package helper is missing")
    known_hosts = job / "linux-known-hosts"
    known_host_name = host if args.linux_port == 22 else f"[{host}]:{args.linux_port}"
    known_hosts.write_text(f"{known_host_name} {host_key}\n", encoding="ascii", newline="\n")
    ssh_base: list[Path | str] = [
        ssh, "-p", str(args.linux_port), "-o", "BatchMode=yes",
        "-o", "StrictHostKeyChecking=yes", "-o", f"UserKnownHostsFile={known_hosts}",
    ]
    scp_base: list[Path | str] = [
        scp, "-P", str(args.linux_port), "-o", "BatchMode=yes",
        "-o", "StrictHostKeyChecking=yes", "-o", f"UserKnownHostsFile={known_hosts}",
    ]
    if identity is not None:
        ssh_base.extend(("-o", "IdentitiesOnly=yes", "-i", identity))
        scp_base.extend(("-o", "IdentitiesOnly=yes", "-i", identity))
    destination = f"{user}@{host}"
    remote_command = f"mkdir -m 700 {remote_job}"
    run_checked([*ssh_base, destination, remote_command], cwd=job, timeout_seconds=120)
    try:
        contract = args.engine_root / "scripts/package_contract.py"
        if not contract.is_file():
            raise PackagingError("package contract helper is missing")
        run_checked([*scp_base, source_archive, helper, contract, f"{destination}:{remote_job}/"], cwd=job, timeout_seconds=1800)
        command = (
            f"python3 {shlex.quote(remote_job + '/package_linux_guest.py')} "
            f"--archive {shlex.quote(remote_job + '/' + source_archive.name)} "
            f"--engine-root {shlex.quote(args.linux_engine_root)} "
            f"--configuration {CONFIGURATIONS[args.configuration]} "
            f"--executable-name {shlex.quote(args.executable_name)} "
            f"--engine-fingerprint {args.engine_fingerprint} "
            f"--output {shlex.quote(remote_job + '/result.tar.gz')}"
        )
        run_checked([*ssh_base, destination, command], cwd=job, timeout_seconds=3600, on_line=lambda line: emit_diagnostic("Info", line))
        result_archive = job / "linux-result.tar.gz"
        run_checked([*scp_base, f"{destination}:{remote_job}/result.tar.gz", result_archive], cwd=job, timeout_seconds=1800)
        _extract_linux_result(result_archive, stage)
    finally:
        run_checked([*ssh_base, destination, f"rm -rf -- {remote_job}"], cwd=job, timeout_seconds=120)
    executable = stage / args.executable_name
    if not executable.is_file() or executable.read_bytes()[:4] != b"\x7fELF":
        raise PackagingError("Linux guest result does not contain the requested ELF player")
    return [ssh, scp]


def _verify_linux_stage(stage: Path, args: argparse.Namespace) -> None:
    player = stage / args.executable_name
    pack = stage / "Game.kbpack"
    receipt_path = stage / "linux-build.receipt.json"
    if not player.is_file() or not pack.is_file() or not receipt_path.is_file():
        raise PackagingError("Linux builder result is missing the player, asset pack, or receipt")
    header = player.read_bytes()[:20]
    if (len(header) < 20 or header[:4] != b"\x7fELF" or header[4] != 2 or
            header[5] != 1 or int.from_bytes(header[18:20], "little") != 62):
        raise PackagingError("Linux player is not an ELF64 x86-64 executable")
    try:
        receipt = json.loads(receipt_path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise PackagingError("Linux build receipt is invalid") from error
    expected = {
        "schema": 1,
        "configuration": CONFIGURATIONS[args.configuration],
        "engineSha256": args.engine_fingerprint,
        "executableSha256": sha256_file(player),
        "assetPackSha256": sha256_file(pack),
        "firstFrame": True,
    }
    if receipt != expected:
        raise PackagingError("Linux build receipt does not match the returned artifact")


def _extract_linux_result(archive_path: Path, destination: Path) -> None:
    with tarfile.open(archive_path, "r:gz") as archive:
        members = archive.getmembers()
        names: set[str] = set()
        folded: set[str] = set()
        for member in members:
            path = PurePosixPath(member.name)
            if path.is_absolute() or any(part in ("", ".", "..") for part in path.parts):
                raise PackagingError("Linux guest archive contains an unsafe path")
            if not member.isfile() and not member.isdir():
                raise PackagingError("Linux guest archive contains a link or special file")
            if member.name in names or member.name.casefold() in folded:
                raise PackagingError("Linux guest archive contains duplicate paths")
            names.add(member.name)
            folded.add(member.name.casefold())
        for member in members:
            relative = PurePosixPath(member.name)
            target = destination.joinpath(*relative.parts)
            if member.isdir():
                target.mkdir(parents=True, exist_ok=True)
                continue
            target.parent.mkdir(parents=True, exist_ok=True)
            source = archive.extractfile(member)
            if source is None:
                raise PackagingError(f"Linux guest archive payload is unreadable: {member.name}")
            with source, target.open("xb") as output:
                shutil.copyfileobj(source, output)
            target.chmod(member.mode & 0o777)


def _stage_target(args: argparse.Namespace, cmake: Path, pack: Path, stage: Path, job: Path) -> StageResult:
    if TARGETS[args.target].platform == "windows":
        return _stage_windows(args, cmake, pack, stage, job)
    if TARGETS[args.target].platform == "android":
        return _stage_android(args, pack, stage, job)
    if TARGETS[args.target].platform in ("webgl", "webgpu"):
        return _stage_web(args, cmake, pack, stage, job)
    linux_stage = stage
    if args.configuration == "Release":
        linux_stage = job / "linux-release-folder"
        linux_stage.mkdir()
    if sys.platform == "linux":
        tools = _stage_linux_local(args, cmake, pack, linux_stage)
    else:
        tools = _stage_linux_remote(args, pack, linux_stage, job)
    _verify_linux_stage(linux_stage, args)
    if args.configuration == "Release":
        archive = stage / f"{args.executable_name}-linux-x64.tar.gz"
        _create_deterministic_tar(
            linux_stage,
            archive,
            executable_name=args.executable_name,
        )
        _verify_linux_release_archive(archive, args.executable_name)
    return StageResult(tuple(tools), _first_frame_result(args.target, stage))


def _verify_linux_release_archive(archive: Path, executable_name: str) -> None:
    with tarfile.open(archive, "r:gz") as package:
        player_members = [member for member in package.getmembers() if member.name == executable_name]
        if (len(player_members) != 1 or not player_members[0].isfile() or
                player_members[0].mode & 0o777 != 0o755):
            raise PackagingError("Linux Release archive player must be a root executable with mode 0755")
    with tempfile.TemporaryDirectory(prefix="21kb-linux-verify-", dir=archive.parent) as temporary_text:
        extracted = Path(temporary_text)
        _extract_linux_result(archive, extracted)
        player = extracted / executable_name
        if not player.is_file() or player.read_bytes()[:4] != b"\x7fELF":
            raise PackagingError("Linux Release archive does not contain the requested ELF player")
        if not (extracted / "Game.kbpack").is_file() or not (extracted / "linux-build.receipt.json").is_file():
            raise PackagingError("Linux Release archive is missing its asset pack or build receipt")


def _launch_linux(args: argparse.Namespace) -> None:
    receipt = verify_unit(args.output)
    if receipt.get("target") != "Linux.x64" or receipt.get("configuration") != args.configuration:
        raise PackagingError("published package is not the requested exact Linux target")
    helper = args.engine_root / "scripts/package_linux_guest.py"
    if not helper.is_file():
        raise PackagingError("Linux launch helper is missing")
    contract = args.engine_root / "scripts/package_contract.py"
    if not contract.is_file():
        raise PackagingError("package contract helper is missing")
    launch_root = args.build_root / "package-launch-jobs"
    launch_job = make_job_directory(launch_root)
    try:
        archive = launch_job / "published-package.tar.gz"
        _create_deterministic_tar(
            args.output,
            archive,
            executable_name=args.executable_name,
        )
        if sys.platform == "linux":
            run_checked(
                [
                    Path(sys.executable).resolve(strict=True), helper,
                    "--launch-archive", archive,
                    "--executable-name", args.executable_name,
                    "--display", args.linux_display,
                ],
                cwd=launch_job,
                timeout_seconds=300,
                on_line=lambda line: emit_diagnostic("Info", line),
            )
            return
        host = args.linux_host or os.environ.get("KB_LINUX_PACKAGE_HOST", "")
        user = args.linux_user or os.environ.get("KB_LINUX_PACKAGE_USER", "")
        host_key = args.linux_host_key or os.environ.get("KB_LINUX_PACKAGE_HOST_KEY", "")
        if not host or not user or not host_key:
            raise PackagingError("Linux launch machine is not configured; host, user, and pinned host key are required")
        if not re.fullmatch(r"[A-Za-z0-9.-]{1,253}", host) or not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_-]{0,31}", user):
            raise PackagingError("Linux launch machine host or user is invalid")
        key_parts = host_key.split()
        if len(key_parts) != 2 or key_parts[0] not in ("ssh-ed25519", "ecdsa-sha2-nistp256", "rsa-sha2-512") or not re.fullmatch(r"[A-Za-z0-9+/=]+", key_parts[1]):
            raise PackagingError("Linux host key must contain an allowlisted key type and base64 public key")
        if not 1 <= args.linux_port <= 65535:
            raise PackagingError("Linux SSH port is invalid")
        if not re.fullmatch(r":[0-9]{1,5}(?:\.[0-9]{1,5})?", args.linux_display):
            raise PackagingError("Linux X11 display is invalid")
        ssh = _required_executable("ssh")
        scp = _required_executable("scp")
        identity_value = args.linux_identity or (
            Path(os.environ["KB_LINUX_PACKAGE_IDENTITY"]) if os.environ.get("KB_LINUX_PACKAGE_IDENTITY") else None
        )
        identity = _existing_file(identity_value, "Linux SSH identity") if identity_value is not None else None
        known_hosts = launch_job / "linux-known-hosts"
        known_host_name = host if args.linux_port == 22 else f"[{host}]:{args.linux_port}"
        known_hosts.write_text(f"{known_host_name} {host_key}\n", encoding="ascii", newline="\n")
        ssh_base: list[Path | str] = [
            ssh, "-p", str(args.linux_port), "-o", "BatchMode=yes",
            "-o", "StrictHostKeyChecking=yes", "-o", f"UserKnownHostsFile={known_hosts}",
        ]
        scp_base: list[Path | str] = [
            scp, "-P", str(args.linux_port), "-o", "BatchMode=yes",
            "-o", "StrictHostKeyChecking=yes", "-o", f"UserKnownHostsFile={known_hosts}",
        ]
        if identity is not None:
            ssh_base.extend(("-o", "IdentitiesOnly=yes", "-i", identity))
            scp_base.extend(("-o", "IdentitiesOnly=yes", "-i", identity))
        destination = f"{user}@{host}"
        remote_job = f"/tmp/21kb-launch-{secrets.token_hex(12)}"
        run_checked([*ssh_base, destination, f"mkdir -m 700 {remote_job}"], cwd=launch_job, timeout_seconds=120)
        try:
            run_checked([*scp_base, archive, helper, contract, f"{destination}:{remote_job}/"], cwd=launch_job, timeout_seconds=1800)
            command = (
                f"python3 {shlex.quote(remote_job + '/package_linux_guest.py')} "
                f"--launch-archive {shlex.quote(remote_job + '/' + archive.name)} "
                f"--executable-name {shlex.quote(args.executable_name)} "
                f"--display {shlex.quote(args.linux_display)}"
            )
            run_checked(
                [*ssh_base, destination, command], cwd=launch_job, timeout_seconds=300,
                on_line=lambda line: emit_diagnostic("Info", line),
            )
        finally:
            run_checked([*ssh_base, destination, f"rm -rf -- {remote_job}"], cwd=launch_job, timeout_seconds=120)
    finally:
        if launch_job.exists():
            remove_tree(launch_job, allowed_parent=launch_root)


@contextmanager
def _published_launch_copy(args: argparse.Namespace) -> Iterator[Path]:
    verify_unit(args.output)
    root = args.build_root / "package-runs"
    run = make_job_directory(root)
    deployed = run / "package"
    try:
        deployed.mkdir(mode=0o700)
        for relative, source in regular_files(args.output):
            destination = deployed.joinpath(*PurePosixPath(relative).parts)
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, destination)
        verify_unit(deployed)
        yield deployed
    finally:
        if run.exists():
            remove_tree(run, allowed_parent=root)


def _wait_for_launch_start(process: subprocess.Popen[object], description: str) -> None:
    deadline = time.monotonic() + 2.0
    while time.monotonic() < deadline:
        return_code = process.poll()
        if return_code is not None:
            raise PackagingError(f"{description} exited during launch with code {return_code}")
        time.sleep(0.05)


def _launch(args: argparse.Namespace) -> None:
    target = TARGETS[args.target].platform
    if target == "windows":
        with _published_launch_copy(args) as deployed:
            executable = deployed / f"{args.executable_name}.exe"
            try:
                process = subprocess.Popen(
                    [executable], cwd=deployed, stdin=subprocess.DEVNULL,
                    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, shell=False, close_fds=True,
                    creationflags=subprocess.CREATE_NEW_PROCESS_GROUP,
                )
            except OSError as error:
                raise PackagingError(f"published Windows player could not be started: {error}") from error
            try:
                _wait_for_launch_start(process, "published Windows player")
                return_code = process.wait()
                if return_code != 0:
                    raise PackagingError(f"published Windows player exited with code {return_code}")
            finally:
                if process.poll() is None:
                    terminate_process_tree(process)
                    process.wait(timeout=5)
    elif target == "android":
        raise PackagingError("Android launch must be verified before package sealing")
    elif target in ("webgl", "webgpu"):
        with _published_launch_copy(args) as deployed:
            html_path = deployed / f"{args.executable_name}.html"
            if not html_path.is_file():
                raise PackagingError("published web entry point is missing")

            class QuietHandler(http.server.SimpleHTTPRequestHandler):
                def log_message(self, _format: str, *values: object) -> None:
                    del values

            handler = lambda *values: QuietHandler(*values, directory=str(deployed))  # noqa: E731
            try:
                server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), handler)
            except OSError as error:
                raise PackagingError(f"web package server could not be started: {error}") from error
            thread = threading.Thread(target=server.serve_forever, name="package-web-launch", daemon=True)
            thread_started = False
            browser: subprocess.Popen[object] | None = None
            try:
                thread.start()
                thread_started = True
                url = (
                    f"http://127.0.0.1:{server.server_port}/"
                    f"{urllib.parse.quote(html_path.name, safe='')}"
                )
                creation_flags = subprocess.CREATE_NEW_PROCESS_GROUP if os.name == "nt" else 0
                try:
                    browser = subprocess.Popen(
                        [
                            _chrome(),
                            f"--user-data-dir={deployed.parent / 'browser-profile'}",
                            "--no-first-run",
                            "--disable-default-apps",
                            "--disable-background-mode",
                            f"--app={url}",
                        ],
                        cwd=deployed,
                        stdin=subprocess.DEVNULL,
                        stdout=subprocess.DEVNULL,
                        stderr=subprocess.DEVNULL,
                        shell=False,
                        close_fds=True,
                        creationflags=creation_flags,
                        start_new_session=os.name != "nt",
                    )
                except OSError as error:
                    raise PackagingError(f"web browser could not be started: {error}") from error
                _wait_for_launch_start(browser, "web browser")
                return_code = browser.wait()
                if return_code != 0:
                    raise PackagingError(f"web browser exited with code {return_code}")
            finally:
                if browser is not None and browser.poll() is None:
                    terminate_process_tree(browser)
                    browser.wait(timeout=5)
                if thread_started:
                    server.shutdown()
                server.server_close()
                if thread_started:
                    thread.join(timeout=5)
    else:
        _launch_linux(args)


def _receipt(
    args: argparse.Namespace,
    project_fingerprint: str,
    engine_fingerprint: str,
    tools: Sequence[Path],
) -> dict[str, object]:
    target = TARGETS[args.target]
    unique_tools = sorted({path.resolve(strict=True) for path in tools}, key=lambda path: path.as_posix())
    return {
        "target": args.target,
        "configuration": args.configuration,
        "product": {
            "name": args.product_name,
            "publisher": args.publisher,
            "version": args.version,
            "executableName": args.executable_name,
        },
        "runtimeContract": {
            "platform": target.platform,
            "textureFamily": target.texture_family,
            "shaderFormat": target.shader_format,
        },
        "inputs": {
            "projectSha256": project_fingerprint,
            "engineSha256": engine_fingerprint,
        },
        "tools": [tool_fingerprint(path) for path in unique_tools],
    }


def _parse_arguments(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project", type=Path, required=True)
    parser.add_argument("--target", choices=tuple(TARGETS), required=True)
    parser.add_argument("--configuration", choices=tuple(CONFIGURATIONS), required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--engine-root", type=Path, required=True)
    parser.add_argument("--build-root", type=Path, required=True)
    parser.add_argument("--product-name", required=True)
    parser.add_argument("--publisher", required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--executable-name", required=True)
    parser.add_argument("--application-icon", type=Path)
    parser.add_argument("--launch", action="store_true")
    parser.add_argument("--android-application-id", default="com.kbengine.game")
    parser.add_argument("--android-label")
    parser.add_argument("--android-version-code", type=int, default=1)
    parser.add_argument("--android-min-sdk", type=int, default=28)
    parser.add_argument("--android-target-sdk", type=int, default=35)
    parser.add_argument("--android-keystore", type=Path)
    parser.add_argument("--android-key-alias", default="")
    parser.add_argument("--android-signing-broker", type=Path)
    parser.add_argument("--emsdk", type=Path)
    parser.add_argument("--linux-host")
    parser.add_argument("--linux-user")
    parser.add_argument("--linux-host-key")
    parser.add_argument("--linux-identity", type=Path)
    parser.add_argument("--linux-port", type=int, default=22)
    parser.add_argument("--linux-engine-root", default="/opt/21kb/engine")
    parser.add_argument("--linux-display", default=":0")
    return parser.parse_args(argv)


def package(args: argparse.Namespace) -> None:
    _validate_arguments(args)
    target = TARGETS[args.target]
    cmake = _required_executable("cmake")
    job_root = args.build_root / "package-jobs"
    job = make_job_directory(job_root)
    candidate = args.output.parent / f".{args.output.name}.candidate-{secrets.token_hex(8)}"
    output_lock = args.output.parent / f".{args.output.name}.package.lock"
    host_build_lock = args.build_root / "package-locks" / "host-build.lock"
    build_lock = args.build_root / "package-locks" / f"{target.platform}.lock"
    running_android_adb: Path | None = None
    published = False
    try:
        with FileLock(output_lock), FileLock(host_build_lock), FileLock(build_lock):
            emit_stage("Validate", 5, f"Validating {args.target} {args.configuration}")
            project_fingerprint = _project_source_fingerprint(args.project)
            snapshot_project = _copy_project_snapshot(args.project, job / "project")
            engine_fingerprint = _engine_fingerprint(args.engine_root)
            args.engine_fingerprint = engine_fingerprint
            cooker, validator = _ensure_host_tools(args, cmake)
            emit_stage("Validate", 20, "Inputs and host tools validated")

            emit_stage("Cook", 25, f"Cooking {target.texture_family} assets and {target.shader_format} shaders")
            pack = _cook(args, snapshot_project, job, cooker, validator)
            emit_stage("Cook", 50, "Runtime asset pack verified")

            emit_stage("Stage", 55, f"Building and staging {args.target}")
            candidate.mkdir(mode=0o700)
            stage_result = _stage_target(args, cmake, pack, candidate, job)
            running_android_adb = stage_result.running_android_adb
            if _engine_fingerprint(args.engine_root) != engine_fingerprint:
                raise PackagingError("engine package inputs changed while the job was running")
            if _project_source_fingerprint(args.project) != project_fingerprint:
                raise PackagingError("project inputs changed while the job was running")
            emit_stage("Stage", 80, "Platform artifact staged")

            emit_stage("Verify", 82, "Sealing exact package file set")
            seal_unit(
                candidate,
                _receipt(
                    args,
                    project_fingerprint,
                    engine_fingerprint,
                    (cmake, cooker, validator, *stage_result.tools),
                ),
                runtime_first_frame=(
                    stage_result.first_frame.receipt_fields()
                    if stage_result.first_frame is not None
                    else None
                ),
            )
            verify_unit(candidate)
            atomic_publish(candidate, args.output)
            verify_unit(args.output)
            published = True
            emit_stage("Verify", 100, "Final package verified and published")
            print(f"RESULT|{args.output.resolve(strict=True)}", flush=True)
        if args.launch and target.platform != "android":
            try:
                _launch(args)
            except PackagingError as error:
                emit_diagnostic("Warning", f"Package succeeded, but launch failed: {error}")
    finally:
        if running_android_adb is not None and not published:
            _try_android_force_stop(running_android_adb, args.android_application_id, candidate.parent)
        if candidate.exists():
            remove_tree(candidate, allowed_parent=candidate.parent)
        if job.exists():
            remove_tree(job, allowed_parent=job_root)


def main(argv: Sequence[str] | None = None) -> int:
    arguments = sys.argv[1:] if argv is None else list(argv)
    try:
        package(_parse_arguments(arguments))
        return 0
    except (PackagingError, OSError, ValueError) as error:
        emit_diagnostic("Error", error)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())

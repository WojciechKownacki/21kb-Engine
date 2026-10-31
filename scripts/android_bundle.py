#!/usr/bin/env python3
"""Validate an Android app bundle against the cooked runtime payload."""

from __future__ import annotations

import argparse
import hashlib
import xml.etree.ElementTree as ET
import zipfile
from pathlib import Path, PurePosixPath

from package_contract import PackagingError, run_checked, sha256_file


ANDROID = "{http://schemas.android.com/apk/res/android}"
DISTRIBUTION = "{http://schemas.android.com/apk/distribution}"
RUNTIME_MODULE = "runtimeassets"


def _manifest(java: Path, bundletool: Path, bundle: Path, module: str) -> ET.Element:
    result = run_checked(
        [java, "-jar", bundletool, "dump", "manifest", f"--bundle={bundle}", f"--module={module}"],
        cwd=bundle.parent, timeout_seconds=120,
    )
    try:
        return ET.fromstring(result.output)
    except ET.ParseError as error:
        raise PackagingError(f"Android bundle {module} manifest is invalid") from error


def _matches(archive: zipfile.ZipFile, name: str, source: Path) -> None:
    if archive.getinfo(name).file_size != source.stat().st_size:
        raise PackagingError(f"Android bundle payload size differs: {name}")
    digest = hashlib.sha256()
    with archive.open(name) as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    if digest.hexdigest() != sha256_file(source):
        raise PackagingError(f"Android bundle payload differs: {name}")


def validate_bundle(
    bundle: Path, *, java: Path, bundletool: Path, profile: str,
    pack: Path, trust_anchor: Path, legal_assets: Path,
    application_id: str, version_code: int, version_name: str,
    min_sdk: int, target_sdk: int, debuggable: bool,
    asset_delivery: bool,
) -> None:
    """Check bundle structure and exact payload; signing is a separate gate."""
    bundle = bundle.resolve(strict=True)
    if profile not in ("Android.ASTC.arm64", "Android.ETC2.arm64"):
        raise PackagingError("Android bundle requires an exact arm64 texture profile")
    if not legal_assets.is_dir() or not any(path.is_file() for path in legal_assets.rglob("*")):
        raise PackagingError("Android bundle requires staged third-party notices")
    module = RUNTIME_MODULE if asset_delivery else "base"
    expected = {
        f"{module}/assets/game.kbpack": pack,
        "base/assets/kb_trust_anchor.bin": trust_anchor,
    }
    expected.update({
        f"base/assets/{path.relative_to(legal_assets).as_posix()}": path
        for path in legal_assets.rglob("*") if path.is_file()
    })
    try:
        with zipfile.ZipFile(bundle) as archive:
            names = archive.namelist()
            if len(names) != len(set(name.casefold() for name in names)):
                raise PackagingError("Android bundle contains duplicate or case-colliding entries")
            for name in names:
                path = PurePosixPath(name)
                if path.is_absolute() or ".." in path.parts or "\\" in name:
                    raise PackagingError("Android bundle contains an unsafe archive path")
            required = set(expected) | {
                "BundleConfig.pb", "base/manifest/AndroidManifest.xml",
                "base/lib/arm64-v8a/libkb_game_android.so",
            }
            if asset_delivery:
                required.add(f"{RUNTIME_MODULE}/manifest/AndroidManifest.xml")
            missing = required - set(names)
            if missing:
                raise PackagingError(f"Android bundle is missing runtime entries: {sorted(missing)}")
            packs = {name for name in names if name.endswith("/assets/game.kbpack")}
            if packs != {f"{module}/assets/game.kbpack"}:
                raise PackagingError("Android bundle contains an unexpected runtime pack location")
            native_paths = [PurePosixPath(name).parts for name in names if "/lib/" in name and not name.endswith("/")]
            if any(len(parts) != 4 or parts[0] != "base" or parts[1] != "lib" or parts[2] != "arm64-v8a"
                   for parts in native_paths):
                raise PackagingError("Android bundle contains an unexpected native module or ABI")
            with archive.open("base/lib/arm64-v8a/libkb_game_android.so") as native:
                header = native.read(20)
                if header[:6] != b"\x7fELF\x02\x01" or header[18:20] != b"\xb7\x00":
                    raise PackagingError("Android bundle host is not a little-endian ARM64 ELF image")
                marker = profile.encode("ascii")
                found = False
                tail = header
                for block in iter(lambda: native.read(1024 * 1024), b""):
                    block = tail + block
                    found = found or marker in block
                    tail = block[-len(marker):]
                if not found:
                    raise PackagingError("Android bundle host texture profile differs")
            for name, source in expected.items():
                _matches(archive, name, source)
    except (zipfile.BadZipFile, KeyError, OSError) as error:
        raise PackagingError(f"Android bundle payload cannot be read: {error}") from error

    run_checked([java, "-jar", bundletool, "validate", f"--bundle={bundle}"],
                cwd=bundle.parent, timeout_seconds=120)
    manifest = _manifest(java, bundletool, bundle, "base")
    sdk = manifest.find("uses-sdk")
    application = manifest.find("application")
    if (manifest.tag != "manifest" or manifest.get("package") != application_id or
            manifest.get(ANDROID + "versionCode") != str(version_code) or
            manifest.get(ANDROID + "versionName") != version_name or sdk is None or
            sdk.get(ANDROID + "minSdkVersion") != str(min_sdk) or
            sdk.get(ANDROID + "targetSdkVersion") != str(target_sdk) or application is None or
            application.get(ANDROID + "debuggable", "false") != str(debuggable).lower()):
        raise PackagingError("Android bundle manifest differs from package settings")
    if asset_delivery:
        assets = _manifest(java, bundletool, bundle, RUNTIME_MODULE)
        asset_module = assets.find(DISTRIBUTION + "module")
        delivery = assets.find(f"{DISTRIBUTION}module/{DISTRIBUTION}delivery")
        if (assets.get("split") != RUNTIME_MODULE or asset_module is None or
                asset_module.get(DISTRIBUTION + "type") != "asset-pack" or delivery is None or
                [child.tag for child in delivery] != [DISTRIBUTION + "install-time"] or
                len(delivery[0]) != 0):
            raise PackagingError("Android runtime assets require unconditional install-time delivery")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("bundle", type=Path)
    for name in ("java", "bundletool", "pack", "trust-anchor", "legal-assets"):
        parser.add_argument(f"--{name}", type=Path, required=True)
    for name in ("profile", "application-id", "version-name"):
        parser.add_argument(f"--{name}", required=True)
    for name in ("version-code", "min-sdk", "target-sdk"):
        parser.add_argument(f"--{name}", type=int, required=True)
    parser.add_argument("--debuggable", action="store_true")
    parser.add_argument("--asset-delivery", action="store_true")
    args = parser.parse_args()
    try:
        validate_bundle(**vars(args))
    except (PackagingError, OSError) as error:
        parser.exit(1, f"Android bundle validation failed: {error}\n")
    print("Android bundle structure and runtime payload verified; signing requires a separate check")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env python3
"""License notices and a CycloneDX SBOM for a 21kb product, built from third_party/third_party_manifest.json."""

from __future__ import annotations

import argparse
import json
import shutil
import sys
import uuid
from dataclasses import dataclass
from pathlib import Path
from typing import Sequence

MANIFEST_PATH = Path("third_party/third_party_manifest.json")
MANIFEST_SCHEMA = "21kb.third-party/v1"
SCOPES = ("game", "engine")
PLATFORMS = ("windows", "linux", "android", "webgl", "webgpu")


class NoticeError(RuntimeError):
    pass


@dataclass(frozen=True)
class Component:
    id: str
    name: str
    version: str
    license: str
    copyright: str
    url: str
    path: str
    license_files: tuple[str, ...]
    scope: str
    platforms: tuple[str, ...]


def load_components(engine_root: Path) -> list[Component]:
    manifest = json.loads((engine_root / MANIFEST_PATH).read_text(encoding="utf-8"))
    if manifest.get("schema") != MANIFEST_SCHEMA:
        raise NoticeError(f"unsupported third-party manifest schema: {manifest.get('schema')!r}")
    components: list[Component] = []
    seen: set[str] = set()
    for entry in manifest["components"]:
        component = Component(
            id=entry["id"],
            name=entry["name"],
            version=entry["version"],
            license=entry["license"],
            copyright=entry["copyright"],
            url=entry["url"],
            path=entry["path"],
            license_files=tuple(entry["licenseFiles"]),
            scope=entry["scope"],
            platforms=tuple(entry.get("platforms", ())),
        )
        if component.id in seen:
            raise NoticeError(f"third-party component listed twice: {component.id}")
        if component.scope not in SCOPES:
            raise NoticeError(f"{component.id}: unknown scope {component.scope!r}")
        if any(platform not in PLATFORMS for platform in component.platforms):
            raise NoticeError(f"{component.id}: unknown platform in {component.platforms}")
        if not component.license_files:
            raise NoticeError(f"{component.id}: no license file")
        for license_file in component.license_files:
            if not (engine_root / license_file).is_file():
                raise NoticeError(f"{component.id}: license file is missing: {license_file}")
        seen.add(component.id)
        components.append(component)
    return components


def select_components(components: Sequence[Component], product: str, platform: str | None) -> list[Component]:
    """A game ships the game-scope components of its platform; the engine ships everything."""
    if product == "engine":
        return list(components)
    if product != "game" or platform not in PLATFORMS:
        raise NoticeError(f"unknown product {product!r} or platform {platform!r}")
    return [
        component for component in components
        if component.scope == "game" and (not component.platforms or platform in component.platforms)
    ]


def stage_notices(engine_root: Path, destination: Path, components: Sequence[Component], product_name: str) -> None:
    """Copies every license text into destination/Licenses and writes THIRD_PARTY_NOTICES.txt beside it."""
    licenses = destination / "Licenses"
    licenses.mkdir(parents=True, exist_ok=True)
    copied: dict[str, str] = {}
    lines = [
        f"{product_name} includes the following third-party software.",
        "Each license text named below is in the Licenses directory.",
        "",
    ]
    for component in components:
        names = []
        for index, license_file in enumerate(component.license_files):
            # A shared text (such as the Apache License) is copied once, under the first name it was given.
            name = copied.get(license_file)
            if name is None:
                # Texts kept in third_party/licenses keep their names, which other notices refer to.
                suffix = "" if index == 0 else f"-{index + 1}"
                shared = license_file.startswith("third_party/licenses/")
                name = Path(license_file).name if shared else f"{component.id}{suffix}.txt"
                shutil.copy2(engine_root / license_file, licenses / name)
                copied[license_file] = name
            names.append(name)
        lines += [
            f"{component.name} {component.version}",
            f"  {component.copyright}",
            f"  License: {component.license} (Licenses/{', Licenses/'.join(names)})",
            f"  {component.url}",
            "",
        ]
    (destination / "THIRD_PARTY_NOTICES.txt").write_text("\n".join(lines), encoding="utf-8", newline="\n")


def build_sbom(product_name: str, product_version: str, components: Sequence[Component]) -> dict[str, object]:
    """CycloneDX 1.5; the serial number follows from the product, so the same release gives the same document."""
    serial = uuid.uuid5(uuid.NAMESPACE_URL, f"21kb-sbom:{product_name}:{product_version}")
    return {
        "bomFormat": "CycloneDX",
        "specVersion": "1.5",
        "serialNumber": f"urn:uuid:{serial}",
        "version": 1,
        "metadata": {
            "component": {
                "type": "application",
                "bom-ref": "product",
                "name": product_name,
                "version": product_version,
            },
        },
        "components": [
            {
                "type": "library",
                "bom-ref": component.id,
                "name": component.name,
                "version": component.version,
                "licenses": [{"expression": component.license}],
                "copyright": component.copyright,
                "externalReferences": [{"type": "website", "url": component.url}],
                "properties": [{"name": "21kb:source-path", "value": component.path}],
            }
            for component in components
        ],
        "dependencies": [
            {"ref": "product", "dependsOn": [component.id for component in components]},
        ],
    }


def write_sbom(path: Path, product_name: str, product_version: str, components: Sequence[Component]) -> None:
    document = build_sbom(product_name, product_version, components)
    path.write_text(json.dumps(document, indent=2) + "\n", encoding="utf-8", newline="\n")


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--engine-root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--product", choices=("game", "engine"), required=True)
    parser.add_argument("--platform", choices=PLATFORMS)
    parser.add_argument("--product-name", default="21kb Engine")
    parser.add_argument("--version", required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args(argv)
    try:
        components = select_components(load_components(args.engine_root), args.product, args.platform)
        args.out.mkdir(parents=True, exist_ok=True)
        stage_notices(args.engine_root, args.out, components, args.product_name)
        write_sbom(args.out / "sbom.cdx.json", args.product_name, args.version, components)
    except NoticeError as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

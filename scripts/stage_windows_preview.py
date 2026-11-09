#!/usr/bin/env python3
"""Stage an existing Release build as a relocatable Windows editor and C++ SDK."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import shutil

from package_contract import FileLock, PackagingError, sha256_file
import prebuilt_engine
from third_party_notices import load_components, select_components, stage_notices, write_sbom
from windows_pe_symbols import pe_pdb_identity, find_matching_pdb


def stage(build: Path, output: Path) -> None:
    engine = Path(__file__).resolve().parents[1]
    build = build.resolve(strict=True)
    output = output.absolute()
    for source_root in (build / "bin", engine / "sources", engine / "docs", engine / "third_party", engine / "scripts"):
        if output.resolve().is_relative_to(source_root.resolve()):
            raise PackagingError("preview output must be outside staged source directories")
    if output.exists() and any(output.iterdir()):
        raise PackagingError("preview output must be a new or empty directory")
    cache = (build / "CMakeCache.txt").read_text(encoding="utf-8")
    if re.search(r"^CMAKE_BUILD_TYPE:STRING=Release$", cache, re.M) is None:
        raise PackagingError("Windows preview staging requires a single-config Release build")
    output.mkdir(parents=True, exist_ok=True)
    files = {}

    def copy(source: Path, relative: str | Path):
        target = output / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)
        files[Path(relative).as_posix()] = sha256_file(target)

    def tree(source: Path, relative: str):
        for source_file in sorted(source.rglob("*")):
            if source_file.is_file():
                if source_file.is_symlink() or not source_file.resolve().is_relative_to(source.resolve()):
                    raise PackagingError(f"preview input escapes the engine: {source_file.name}")
                copy(source_file, Path(relative) / source_file.relative_to(source))

    with FileLock(build / "package-locks/host-build.lock"):
        for target, relative in prebuilt_engine.TARGETS.items():
            if relative.endswith(".exe"):
                source = next((p for p in (build / "bin" / (target + ".exe"),
                                           build / "renderer" / (target + ".exe")) if p.is_file()), None)
                if source is None:
                    raise PackagingError(f"build target {target} before staging")
            else:
                directory = "world_2d" if target == "kb_world2d_plugin" else target.removeprefix("kb_").removesuffix("_plugin")
                source = build / directory / (target + ".dll")
            copy(source, relative)
            if target == "kb_game" or relative.endswith(".dll"):
                identity = pe_pdb_identity(source)
                symbols = source.with_suffix(".pdb")
                if identity is None or find_matching_pdb(identity, [symbols]) is None:
                    raise PackagingError(f"matching crash symbols are required for {target}")
                copy(symbols, Path(relative).with_suffix(".pdb"))
        copy(build / "terrain_editor/kb_terrain_editor_plugin.dll", "bin/kb_terrain_editor_plugin.dll")
        for name in ("phonon.dll", "python314.dll"):
            copy(build / "bin" / name, "bin/" + name)
        shaderc = build / "third_party/bgfx.cmake/cmake/bgfx"
        for name in ("shaderc.exe", "dxcompiler.dll"):
            copy(shaderc / name, "bin/" + name)
        if (shaderc / "dxil.dll").is_file():
            copy(shaderc / "dxil.dll", "bin/dxil.dll")
        for name in ("Content", "shaders", "Python"):
            tree(build / "bin" / name, "bin/" + name)
        for name in ("engine", "editor"):
            tree(engine / "sources" / name / "include", "sources/" + name + "/include")
        tree(engine / "sources/renderer/shaders", "sources/renderer/shaders")
        for path in (engine / "third_party/bgfx.cmake/bgfx/src").glob("*.sh"):
            copy(path, Path("third_party/bgfx.cmake/bgfx/src") / path.name)
        for path in (engine / "scripts").glob("*.py"):
            copy(path, Path("scripts") / path.name)
        tree(engine / "third_party/licenses", "third_party/licenses")
        copy(engine / "third_party/third_party_manifest.json", "third_party/third_party_manifest.json")
        components = select_components(load_components(engine), "engine", None)
        for relative in sorted({name for component in components for name in component.license_files}):
            copy(engine / relative, relative)
        stage_notices(engine, output, components, "21kb Engine")
        write_sbom(output / "sbom.cdx.json", "21kb Engine", "test", components)
        for relative in ("THIRD_PARTY_NOTICES.txt", "sbom.cdx.json"):
            files[relative] = sha256_file(output / relative)
        for license_file in (output / "Licenses").iterdir():
            files[license_file.relative_to(output).as_posix()] = sha256_file(license_file)
        copy(engine / "platform/windows/CRASH_REPORTS.txt", "platform/windows/CRASH_REPORTS.txt")
        tree(engine / "docs", "docs")
        tree(engine / "examples/EditorExtension", "examples/EditorExtension")
        for name in ("LICENSE", "README.md"):
            copy(engine / name, name)
        copy(engine / "docs/windows_preview_start.txt", "START-HERE.txt")
        copy(build / "engine/kb_engine.lib", "build/engine/Release/kb_engine.lib")
        copy(build / "third_party/flecs/flecs_static.lib", "build/third_party/flecs/Release/flecs_static.lib")
        for name in ("lua", "ufbx", "monocypher", "zstd", "recast", "ui_text_shaping", "harfbuzz", "sheenbidi", "csg_geometry"):
            copy(build / ("kb_" + name + ".lib"), "build/Release/kb_" + name + ".lib")
        manifest = dict(schema=prebuilt_engine.SCHEMA, target="Windows.x64",
                        engineConfiguration="Release", files=files)
        (output / prebuilt_engine.MANIFEST).write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
        prebuilt_engine.fingerprint(output)
    print(f"Staged {len(files)} files; Windows Release tools and native SDK verified.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-root", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    try:
        stage(args.build_root, args.output)
    except (OSError, PackagingError) as error:
        parser.exit(1, f"Windows preview staging failed: {error}\n")

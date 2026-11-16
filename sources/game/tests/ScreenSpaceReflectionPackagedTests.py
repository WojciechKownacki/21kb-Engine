"""Cook a scene with a glossy floor and a bright wall, and compare the packaged player's frame with screen-space reflections off and on."""

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for option in ["cli", "cooker", "player", "engine-root", "test-root", "shaderc"]:
        parser.add_argument("--" + option, type=Path, required=True)
    parser.add_argument("--plugin", type=Path, action="append", required=True)
    parser.add_argument("--runtime-dll", type=Path, action="append", default=[])
    args = parser.parse_args()
    require(os.name == "nt", "The packaged reflection test requires the native Windows host")
    require(args.shaderc.is_file(), "Packaged GPU testing requires shaderc: enable KB_BUILD_GRAPH_SHADERC or set KB_COOKER_SHADERC")
    sys.path.insert(0, str(args.engine_root.resolve() / "scripts"))
    from png_readback import read_png

    args.test_root.mkdir(parents=True, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix="SsrPackage", dir=args.test_root))
    project = root / "SampleScene"
    history = []

    def run(executable, *tail, cwd=root):
        process = subprocess.run([str(executable), *map(str, tail)], cwd=cwd, capture_output=True, text=True,
                                 encoding="utf-8", errors="replace", timeout=300, creationflags=subprocess.CREATE_NO_WINDOW)
        history.append(dict(executable=str(executable), arguments=list(map(str, tail)), code=process.returncode,
                            stdout=process.stdout, stderr=process.stderr))
        (root / "commands.json").write_text(json.dumps(history, indent=2), encoding="utf-8")
        require(process.returncode == 0, f"Unexpected exit {process.returncode}: {history[-1]}")
        return process.stdout + process.stderr

    def cli(*tail):
        return run(args.cli, *tail)

    header = "version 1\nmaterialType builtin.pbr\nmaterialTypeVersion 1\n"
    cli("new", "--parent", root, "--name", project.name, "--template", "Empty")
    materials = project / "Assets" / "Materials"
    materials.mkdir(parents=True)
    (materials / "Mirror.kbmat").write_text(header + "baseColor 0.9 0.9 0.9 1\nroughnessFactor 0.05\n", encoding="utf-8")
    (materials / "Wall.kbmat").write_text(header + "baseColor 1 0.05 0.05 1\nemissiveColor 1 0.05 0.05\nemissiveStrength 3\n", encoding="utf-8")
    cli("entity", "add", "--project", project, "--create", "Camera", "--name", "MainCamera", "--position", "-6,1,-3", "--look-at", "2,0,0")
    cli("entity", "add", "--project", project, "--create", "Point Light", "--name", "Lamp", "--position", "-1,5,0")
    cli("entity", "add", "--project", project, "--create", "Cube", "--name", "Floor", "--position", "0,-0.5,0", "--scale", "40,1,40")
    cli("entity", "add", "--project", project, "--create", "Cube", "--name", "Wall", "--position", "3,3,0", "--scale", "0.5,6,20")
    for name, material in [("Floor", "Mirror"), ("Wall", "Wall")]:
        cli("component", "set", "--project", project, "--entity", name, "--type", "MeshRenderer",
            "--set", f"material=/Game/Materials/{material}.kbmat")
    # A fixed exposure and no temporal filtering keep two runs of the same package identical.
    cli("post-process-new", "--project", project, "--name", "Look", "--set", "autoExposureMetering=Manual",
        "--set", "temporalAntiAliasingEnabled=0", "--set", "bloomEnabled=0")
    cli("scene-post-process", "--project", project, "--profile", "/Game/PostProcess/Look.kbppfx")

    def capture(name):
        package = root / name
        package.mkdir()
        run(args.cooker, "--project", project, "--target", "Windows.x64", "--output", package / "Game.kbpack",
            "--engine-root", args.engine_root, "--shaderc", args.shaderc, "--cache", root / "CookCache")
        # The development player takes the package by path and writes screenshots; the package's modules stand beside it.
        for module in [*args.plugin, *args.runtime_dll]:
            shutil.copy2(module, package / module.name)
        image = package / "frame.png"
        output = run(args.player, f"--project={package / 'Game.kbpack'}", "--headless", "--frames=30", "--uncapped", "--width=640",
                     "--height=360", "--profile-fixed-step", f"--profile-file={package / 'profile.json'}",
                     "--screenshot-frame=30", f"--screenshot-file={image}", cwd=args.player.resolve().parent)
        require("shutdown=clean" in output and "errors=0" in output, output)
        return read_png(image)

    off = capture("Off")
    cli("post-process-set", "--project", project, "--profile", "/Game/PostProcess/Look.kbppfx", "--set", "screenSpaceReflectionsEnabled=1")
    on = capture("On")

    def changed(first, second, x, y):
        return max(abs(a - b) for a, b in zip(first[y][x], second[y][x])) > 6

    height, width = len(off), len(off[0])
    # The mirror floor reflects the scene below the horizon; the sky and the top of the wall above it cannot change.
    floor_changed = sum(changed(off, on, x, y) for y in range(height // 2, height) for x in range(width))
    above_changed = sum(changed(off, on, x, y) for y in range(0, int(height * 0.45)) for x in range(width))
    print(f"ssr_package floor_changed={floor_changed} above_horizon_changed={above_changed}")
    require(floor_changed > 5000, f"Reflections did not change the packaged floor: {floor_changed}")
    require(above_changed == 0, f"Reflections changed pixels that cannot reflect: {above_changed}")
    # With the reflection intensity at zero the frame is the one without reflections: the change is the reflection's.
    cli("post-process-set", "--project", project, "--profile", "/Game/PostProcess/Look.kbppfx", "--set", "screenSpaceReflectionsIntensity=0")
    faded = capture("Faded")
    faded_changed = sum(changed(off, faded, x, y) for y in range(height) for x in range(width))
    require(faded_changed == 0, f"Zero reflection intensity still changed the frame: {faded_changed}")
    print(f"PASS: packaged screen-space reflections, floor_changed={floor_changed}; {root}")

if __name__ == "__main__":
    main()

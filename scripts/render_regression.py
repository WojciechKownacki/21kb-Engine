"""Capture fixed-step scene sequences and compare them with reviewed backend goldens.

Capture-only writes candidates, never a passing regression result. Goldens are
updated separately after image review; a failing run never replaces references.
"""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from compare_images import main as compare_main
from png_readback import read_png
from profile_scene import backend_matches, drivers, run_player

BACKENDS = {"d3d11": "Direct3D 11", "d3d12": "Direct3D 12", "vulkan": "Vulkan", "opengl": "OpenGL"}


def digest(path):
    value = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            value.update(chunk)
    return value.hexdigest()


def source_identity(root):
    if root is None:
        return {"git_head": None, "reason": "source root was not supplied"}
    result = subprocess.run(["git", "-C", str(root), "rev-parse", "HEAD"], capture_output=True, text=True)
    manifest = {}
    for folder in ("sources/renderer", "sources/game", "sources/engine", "sources/editor", "scripts"):
        for path in sorted((root / folder).rglob("*")):
            if path.is_file() and "__pycache__" not in path.parts:
                manifest[path.relative_to(root).as_posix()] = digest(path)
    backend_source = root / "third_party/bgfx.cmake/bgfx/src/renderer_d3d12.cpp"
    if backend_source.is_file():
        manifest[backend_source.relative_to(root).as_posix()] = digest(backend_source)
    encoded = json.dumps(manifest, sort_keys=True, separators=(",", ":")).encode()
    return {"git_head": result.stdout.strip() if result.returncode == 0 else None,
            "working_source_sha256": hashlib.sha256(encoded).hexdigest(), "files": manifest,
            "basis": "HEAD plus working source content; executable hash identifies the tested binary; uncommitted changes are not attributed to HEAD"}


def validate_capture(profile, picture, backend, width, height, frames):
    metadata = json.loads(Path(str(profile) + ".meta.json").read_text(encoding="utf-8"))
    if not backend_matches(backend, metadata["backend"]) or metadata["width"] != width or metadata["height"] != height:
        raise ValueError(f"Requested capture configuration was not honored: {metadata}")
    with profile.open(newline="", encoding="utf-8") as source:
        rows = list(csv.DictReader(source))
    if len(rows) != frames or any(int(row["missing_resources"]) or int(row["dropped"]) for row in rows):
        raise ValueError("Capture has missing frames, missing resources or dropped geometry")
    if not rows or int(rows[-1]["draws"]) == 0 or int(rows[-1]["submitted_meshes"]) == 0:
        raise ValueError("Capture did not submit actual scene geometry")
    pixels = read_png(picture)
    if len(pixels) != height or len(pixels[0]) != width:
        raise ValueError("Capture has the wrong image dimensions")
    colors = {pixel for row in pixels for pixel in row}
    if len(colors) < 16 or max(max(pixel) for pixel in colors) - min(min(pixel) for pixel in colors) < 32:
        raise ValueError("Capture is blank or nearly uniform")
    return metadata, len(colors)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--player", type=Path, required=True)
    parser.add_argument("--project", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--goldens", type=Path)
    parser.add_argument("--source-root", type=Path)
    parser.add_argument("--fixture-sha256", required=True)
    parser.add_argument("--capture-only", action="store_true")
    parser.add_argument("--backends", nargs="+", choices=tuple(BACKENDS), default=list(BACKENDS))
    parser.add_argument("--frames", nargs="+", type=int, default=[60, 120, 240])
    parser.add_argument("--width", type=int, default=960)
    parser.add_argument("--height", type=int, default=540)
    parser.add_argument("--timeout", type=float, default=120)
    args = parser.parse_args()
    if not args.capture_only and args.goldens is None:
        parser.error("comparison requires --goldens; candidate capture requires --capture-only")
    if not (64 <= args.width <= 8192 and 64 <= args.height <= 8192 and args.frames
            and all(1 <= frame <= 119996 for frame in args.frames) and len(set(args.frames)) == len(args.frames)):
        parser.error("dimensions must be 64..8192 and unique capture frames 1..119996")
    player = args.player.resolve(strict=True)
    project = args.project.resolve(strict=True)
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=False)
    report = {"schema": "21kb.render-regression/v1", "result": "RUNNING", "captures": [],
              "fixture_sha256": args.fixture_sha256, "player_sha256": digest(player),
              "project": str(project), "package_sha256": digest(project) if project.is_file() else None,
              "source": source_identity(args.source_root.resolve() if args.source_root else None),
              "drivers": drivers(), "configuration": {"backends": args.backends, "frames": args.frames,
                                                        "width": args.width, "height": args.height},
              "limits": "This run qualifies only the requested native backends and fixture; no absent backend is reported as passed."}
    if project.is_dir():
        shader_root = player.parent / "shaders"
        report["development_shader_sha256"] = {
            path.relative_to(shader_root).as_posix(): digest(path)
            for path in sorted(shader_root.rglob("*.bin"))}
        if not report["development_shader_sha256"]:
            raise ValueError("Development captures require identifiable shaders next to the player")
    goldens = None
    try:
        if not args.capture_only:
            goldens = json.loads((args.goldens / "manifest.json").read_text(encoding="utf-8"))
            if (goldens["fixture_sha256"] != args.fixture_sha256 or goldens["width"] != args.width
                    or goldens["height"] != args.height or goldens["frames"] != args.frames):
                raise ValueError("Golden fixture/configuration differs; review new candidates before changing references")
        for backend in args.backends:
            for frame in args.frames:
                name = f"{backend}-frame-{frame}"
                profile, picture = output / f"{name}.csv", output / f"{name}.png"
                command = [str(player), f"--project={project}", "--headless", "--uncapped", "--profile-fixed-step",
                           f"--renderer={backend}", f"--width={args.width}", f"--height={args.height}",
                           f"--frames={frame + 4}", f"--profile-file={profile}",
                           f"--screenshot-frame={frame}", f"--screenshot-file={picture}"]
                row = {"backend": backend, "frame": frame, "command": command, "image": picture.name}
                report["captures"].append(row)
                row["exit_code"] = run_player(command, player, output / f"{name}.log", args.timeout)
                if row["exit_code"]:
                    raise RuntimeError(f"{name} failed; see its process log")
                row["metadata"], row["distinct_colors"] = validate_capture(profile, picture, backend, args.width, args.height, frame + 4)
                row["sha256"] = digest(picture)
                if goldens is not None:
                    reference = args.goldens / picture.name
                    if digest(reference) != goldens["images"][picture.name]:
                        raise ValueError(f"Golden image changed without manifest review: {picture.name}")
                    limits = goldens["tolerances"][backend]
                    result = compare_main([str(reference), str(picture), "--pixel-tolerance", str(limits["pixel_tolerance"]),
                        "--max-mean", str(limits["max_mean"]), "--max-differing", str(limits["max_differing"]),
                        "--diff", str(output / f"{name}-diff.png"), "--report", str(output / f"{name}-comparison.json")])
                    row["comparison"] = result
                    if result:
                        raise RuntimeError(f"{name} differs from its reviewed golden; actual/diff/config retained")
                (output / "report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
        report["result"] = "CAPTURED" if args.capture_only else "PASS"
    except BaseException as error:
        report.update(result="FAIL", error=str(error))
        raise
    finally:
        (output / "report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps({"result": report["result"], "report": str(output / "report.json"), "captures": len(report["captures"])}))


if __name__ == "__main__":
    main()

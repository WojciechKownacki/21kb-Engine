"""Author, save/reload, capture motion, cook and qualify native backend images."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "scripts"))
from compare_images import compare, main as compare_main
from png_readback import read_png
from profile_scene import run_player
from render_regression import digest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("editor", "cooker", "player", "cli", "engine-root", "test-root", "shaderc"):
        parser.add_argument("--" + name, required=True, type=Path)
    parser.add_argument("--plugin", action="append", type=Path, required=True)
    parser.add_argument("--goldens", type=Path, default=Path(__file__).with_name("goldens") / "Scene3D")
    parser.add_argument("--fog-profile", choices=("Desktop", "Mobile", "Web"),
                        help="Qualify an authored fog profile through isolated package parity (use --capture-only)")
    parser.add_argument("--capture-only", action="store_true")
    parser.add_argument("--material-layers", action="store_true",
                        help="Qualify lacquer and subsurface assets through isolated package parity (use --capture-only)")
    parser.add_argument("--material-graph", action="store_true",
                        help="Qualify a scattering graph and its cooked full/compact variants (use --capture-only)")
    args = parser.parse_args()
    if os.name != "nt":
        parser.error("This matrix qualifies the native Windows host")
    for name in ("editor", "cooker", "player", "cli", "engine_root", "shaderc", "goldens", "test_root"):
        setattr(args, name, getattr(args, name).resolve())
    args.test_root.mkdir(parents=True, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix="SceneImages", dir=args.test_root)).resolve()
    report = {"schema": "21kb.image-workflow/v1", "result": "RUNNING", "commands": [], "comparisons": [],
              "root": str(root), "scope": "Windows packaged D3D11/D3D12/Vulkan and development OpenGL; editor Play reference uses its native selected backend"}
    def save():
        (root / "result.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    def run(name, command, *, cwd=None, timeout=300, expected=0):
        command = list(map(str, command))
        code = run_player(command, Path(command[0]).resolve() if cwd is None else Path(cwd) / "unused.exe", root / (name + ".log"), timeout)
        report["commands"].append({"name": name, "command": command, "exitCode": code, "expected": expected})
        save()
        if code != expected:
            raise RuntimeError(f"{name} returned {code}; see {root / (name + '.log')}")
    def compare_capture(name, first, second, *, negative=False):
        code = compare_main([str(first), str(second), "--pixel-tolerance", "12", "--max-mean", "2", "--max-differing", "0.002",
                             "--diff", str(root / (name + "-diff.png")), "--report", str(root / (name + ".json"))])
        report["comparisons"].append({"name": name, "exitCode": code, "expectedFailure": negative})
        save()
        if code != (1 if negative else 0):
            raise RuntimeError(f"Unexpected image comparison: {name} ({code})")
    try:
        original = json.loads((args.engine_root / "sources/editor/tests/Scene3DParityScenario.json").read_text(encoding="utf-8"))
        motion = Path(__file__).with_name("RenderRegressionMotion.lua").read_text(encoding="utf-8")
        steps = original["steps"]
        if args.material_layers:
            if not args.capture_only:
                raise ValueError("The material-layer fixture needs --capture-only; default goldens describe another fixture")
            at = next(i for i, step in enumerate(steps) if step["op"] == "save_scene")
            header = "version 1\nmaterialType builtin.pbr\nmaterialTypeVersion 1\n"
            materials = {
                "Lacquer": header + "baseColor 0.15 0.04 0.02 1\nmetallicFactor 0.4\nroughnessFactor 0.65\nclearcoatFactor 1\nclearcoatRoughnessFactor 0.12\n",
                "Scattering": header + "baseColor 0.45 0.25 0.12 1\nroughnessFactor 0.65\nsubsurfaceColor 0.9 0.08 0.025\nsubsurfaceFactor 0.8\nclearcoatFactor 0.3\nclearcoatRoughnessFactor 0.25\n"
            }
            steps[at:at] = [
                *[{"op": "write_file", "path": f"Assets/Materials/{name}.kbmat", "content": content}
                  for name, content in materials.items()],
                {"op": "discover_assets"},
                {"op": "assign_asset", "entity": "ball", "asset": "/Game/Materials/Lacquer.kbmat", "role": "material"},
                {"op": "assign_asset", "entity": "pillar", "asset": "/Game/Materials/Scattering.kbmat", "role": "material"}]
            report["material_layers"] = materials
        if args.material_graph:
            if not args.capture_only:
                raise ValueError("The material graph fixture needs --capture-only")
            graph = (args.engine_root / "sources/editor/tests/HeadlessMaterialPreview/Assets/Galeria/M21_Baza.kbmat").read_text(encoding="utf-8")
            graph = graph.replace("graphShadingModel defaultLit", "graphShadingModel subsurface")
            graph = graph.replace("graphParameterValue color4 Color 1 1 1 1", "graphParameterValue color4 Color 0.4 0.1 0.025 1")
            graph += "graphLink 4238460239 4 991233 rgba 1 69665 subsurfaceColor\n"
            graph += "graphLink 4238460240 3 139265 value 1 69656 clearCoat\n"
            at = next(i for i, step in enumerate(steps) if step["op"] == "save_scene")
            steps[at:at] = [
                {"op": "write_file", "path": "Assets/Materials/ScatteringGraph.kbmat", "content": graph},
                {"op": "discover_assets"},
                {"op": "assign_asset", "entity": "ball", "asset": "/Game/Materials/ScatteringGraph.kbmat", "role": "material"}]
            report["material_graph"] = graph
        if args.fog_profile:
            if not args.capture_only:
                raise ValueError("A fog-profile fixture needs --capture-only; the Scene3D goldens describe the default fixture")
            at = next(i for i, step in enumerate(steps) if step.get("field") == "fogDensity") + 1
            steps[at:at] = [
                *[{"op": "post_process_row", "asset": "look", "field": "fogProfile"}
                  for _ in range(("Desktop", "Mobile", "Web").index(args.fog_profile) + 1)],
                {"op": "post_process_row", "asset": "look", "field": "volumetricFogEnabled"},
                {"op": "assert_post_process_profile", "asset": "look", "field": "fogProfile", "value": args.fog_profile},
                {"op": "assert_post_process_profile", "asset": "look", "field": "volumetricFogEnabled", "value": "1"}]
            report["fog_profile"] = args.fog_profile
        before_save = next(index for index, step in enumerate(steps) if step["op"] == "save_scene")
        steps[before_save:before_save] = [
            {"op": "write_file", "path": "Assets/Logic/RenderMotion.lua", "content": motion},
            {"op": "discover_assets"}, {"op": "attach_script", "entity": "cube", "asset": "/Game/Logic/RenderMotion.lua"}]
        start = next(index for index, step in enumerate(steps) if step["op"] == "step")
        finish = start + 2
        sequence = []
        previous = 0
        for frame in (60, 120, 240):
            sequence += [{"op": "step", "frames": frame - previous, "dt": 0.0166666667},
                         {"op": "capture_runtime", "checkpoint": f"editor-frame-{frame}", "require_non_uniform": True}]
            previous = frame
        steps[start:finish] = sequence
        fixture = json.dumps(original, sort_keys=True, separators=(",", ":")).encode()
        report["fixture_sha256"] = hashlib.sha256(fixture).hexdigest()
        scenario = root / "Scenario.json"
        scenario.write_text(json.dumps(original, indent=2), encoding="utf-8")
        run("editor", [args.editor, "--selftest-scenario", scenario, "--selftest-root", root, "--selftest-task", "Authoring"],
            cwd=args.editor.parent.parent, timeout=240)
        editor_report = json.loads((root / "Authoring/report.json").read_text(encoding="utf-8"))
        if editor_report["result"] != "PASS":
            raise RuntimeError("Editor scenario did not pass")
        project = root / "Authoring/workspace"
        screenshots = root / "Authoring/automation/screenshots"
        report["editor"] = editor_report
        motion_stats = compare(read_png(screenshots / "editor-frame-60.png"), read_png(screenshots / "editor-frame-240.png"))
        if motion_stats["mean"] < 0.05:
            raise RuntimeError("The motion sequence did not change its image")
        report["motion"] = motion_stats
        # This region contains the moving cube, below and left of the emitter.
        # Particle animation alone must not satisfy the object motion contract.
        cube_frames = [read_png(screenshots / f"editor-frame-{frame}.png") for frame in (60, 240)]
        cube_motion = compare(*[[row[270:440] for row in pixels[265:360]] for pixels in cube_frames])
        if cube_motion["mean"] < 0.5:
            raise RuntimeError("The scripted cube did not move in the captured sequence")
        report["cube_motion"] = cube_motion

        def captures(name, player, project_path, backends, *, capture_only=None):
            command = [sys.executable, args.engine_root / "scripts/render_regression.py", "--player", player,
                       "--project", project_path, "--output-dir", root / name, "--goldens", args.goldens,
                       "--fixture-sha256", report["fixture_sha256"], "--source-root", args.engine_root,
                       "--backends", *backends]
            use_candidates = args.capture_only if capture_only is None else capture_only
            if use_candidates:
                command.append("--capture-only")
            run(name, command, cwd=args.player.parent, timeout=300)
        # OpenGL is a development renderer on this host; Windows.x64 packages
        # intentionally contain DXBC/DXIL/SPIR-V and do not promise GLSL.
        captures("opengl-source", args.player, project, ["opengl"])
        package, probe = root / "Package", root / "Probe"
        package.mkdir(); probe.mkdir()
        run("cook", [args.cooker, "--project", project, "--target", "Windows.x64", "--output", package / "Game.kbpack",
                      "--engine-root", args.engine_root, "--shaderc", args.shaderc,
                      "--runtime-modules-output", package / "RuntimeModules", "--cache", args.test_root / "CookCache"],
            cwd=args.engine_root, timeout=600)
        if args.material_graph:
            for target in ("WebGL.wasm32", "Android.ETC2.arm64"):
                run("cook-" + target, [args.cooker, "--project", project, "--target", target,
                    "--output", root / (target + ".kbpack"), "--engine-root", args.engine_root,
                    "--shaderc", args.shaderc, "--cache", args.test_root / "CookCache"],
                    cwd=args.engine_root, timeout=600)
        for source in [args.player, *args.plugin]:
            shutil.copy2(source, package / source.name)
        shutil.copy2(args.player, probe / args.player.name)
        if (package / "Assets").exists() or (probe / "Game.kbpack").exists():
            raise RuntimeError("Isolated capture layout was not clean")
        hidden = project.with_name("workspace.hidden")
        if root not in project.resolve().parents or root not in hidden.resolve().parents or hidden.exists():
            raise RuntimeError("Project isolation paths escaped this test workspace")
        project.rename(hidden)
        try:
            report["source_project_hidden"] = not project.exists()
            run("shipped", [package / args.player.name, "--headless", "--uncapped", "--frames=120", "--width=320", "--height=180"],
                cwd=package, timeout=120)
            text = (root / "shipped.log").read_text(encoding="utf-8", errors="replace")
            if "rendered=120" not in text or "errors=0" not in text or "shutdown=clean" not in text:
                raise RuntimeError("Shipped executable did not finish cleanly")
            captures("packaged", probe / args.player.name, package / "Game.kbpack", ["d3d11", "d3d12", "vulkan"])
        finally:
            hidden.rename(project)
        parity_failures = []
        for frame in (60, 120, 240):
            reference = screenshots / f"editor-frame-{frame}.png"
            for backend in ("d3d11", "d3d12", "vulkan", "opengl"):
                folder = "opengl-source" if backend == "opengl" else "packaged"
                try:
                    compare_capture(f"editor-{backend}-{frame}", reference, root / folder / f"{backend}-frame-{frame}.png")
                except RuntimeError as error:
                    parity_failures.append(str(error))
        if parity_failures:
            raise RuntimeError("; ".join(parity_failures))
        # Collapse the visible cube in the authored scene and render it again.
        run("mutate-cube", [args.cli, "component", "set", "--project", project, "--entity", "Cube", "--type", "Transform",
                            "--set", "localScale=0,0,0"], cwd=args.cli.parent)
        captures("mutant", args.player, project, ["d3d11"], capture_only=True)
        compare_capture("missing-cube-negative-control", root / "packaged/d3d11-frame-240.png",
                        root / "mutant/d3d11-frame-240.png", negative=True)
        report["package_sha256"] = digest(package / "Game.kbpack")
        report["result"] = "CAPTURED" if args.capture_only else "PASS"
    except BaseException as error:
        report.update(result="FAIL", error=str(error))
        raise
    finally:
        save()
    print(json.dumps({"result": report["result"], "root": str(root)}))


if __name__ == "__main__":
    main()

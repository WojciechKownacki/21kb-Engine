"""Exercise authored particle assets through cook and the actual hidden packaged player."""

import argparse
import ctypes
import json
import math
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for option in ["cli", "cooker", "player", "engine-root", "test-root", "shaderc"]:
        parser.add_argument("--" + option, type=Path, required=True)
    parser.add_argument("--plugin", type=Path, action="append", required=True)
    args = parser.parse_args()
    require(os.name == "nt", "The packaged GPU test requires the native Windows host")
    require(args.shaderc.is_file(), "Packaged GPU testing requires shaderc: enable KB_BUILD_GRAPH_SHADERC or set KB_COOKER_SHADERC")
    ctypes.windll.kernel32.SetErrorMode(1 | 2 | 0x8000)
    args.test_root.mkdir(parents=True, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix="ParticleGpuPackage", dir=args.test_root))
    project = root / "Sample"
    history = []

    def run(executable, *tail, expected=0, cwd=root):
        process = subprocess.run([str(executable), *map(str, tail)], cwd=cwd, capture_output=True,
                                 text=True, encoding="utf-8", errors="replace", timeout=150,
                                 creationflags=subprocess.CREATE_NO_WINDOW)
        history.append(dict(executable=str(executable), arguments=list(map(str, tail)),
                            code=process.returncode, stdout=process.stdout, stderr=process.stderr))
        (root / "commands.json").write_text(json.dumps(history, indent=2), encoding="utf-8")
        require((process.returncode == 0) == (expected == 0),
                f"Unexpected exit {process.returncode}: {history[-1]}")
        return process.stdout

    def sets(*values):
        return [part for value in values for part in ["--set", value]]

    def edit(name, *values):
        return run(args.cli, "vfx-set", "--project", project, "--effect", f"/Game/Particles/{name}.kbvfx", *sets(*values))

    run(args.cli, "new", "--parent", root, "--name", "Sample", "--template", "Empty")
    for name in ["Mixed", "Contact"]:
        run(args.cli, "vfx-new", "--project", project, "--name", name, *sets(
            "effect.backendPolicy=GpuVisualPreferred", "effect.determinismSeed=42", "effect.durationSeconds=10", "effect.looping=false",
            "effect.emitter[0].maxParticles=64", "effect.emitter[0].spawn.mode=Burst", "effect.emitter[0].spawn.burstCount=1",
            "effect.emitter[0].spawn.burst[0].timeSeconds=0", "effect.emitter[0].spawn.burst[0].count=64",
            "effect.emitter[0].spawn.lifetimeMin=5", "effect.emitter[0].spawn.lifetimeMax=5",
            "effect.emitter[0].spawn.speedMin=4", "effect.emitter[0].spawn.speedMax=4",
            "effect.emitter[0].spawn.direction=1 0 0", "effect.emitter[0].spawn.spreadDegrees=0",
            "effect.emitter[0].spawn.randomization=0", "effect.emitter[0].output.depthTest=false",
            "effect.emitter[0].output.softParticles=false"))
        run(args.cli, "entity", "add", "--project", project, "--name", name)
        run(args.cli, "component", "add", "--project", project, "--entity", name, "--type", "Particle Effect",
            *sets(f"effectAssetId=/Game/Particles/{name}.kbvfx", "deterministicSeed=1"))
    run(args.cli, "entity", "add", "--project", project, "--create", "Camera", "--name", "MainCamera")
    run(args.cli, "component", "set", "--project", project, "--entity", "MainCamera", "--type", "Transform",
        *sets("localPosition.z=-10"))
    for module in ["Noise", "AxisRotation", "Attraction", "Noise", "AxisRotation", "Attraction"]:
        run(args.cli, "vfx-module-add", "--project", project, "--effect", "/Game/Particles/Mixed.kbvfx", "--emitter", "1", "--type", module)
    edit("Mixed", "effect.emitter[0].module[2].payload.strength=0.7", "effect.emitter[0].module[2].payload.frequency=0.5",
         "effect.emitter[0].module[3].payload.axis=0 1 0", "effect.emitter[0].module[3].payload.degreesPerSecond=80",
         "effect.emitter[0].module[4].payload.point=1 1 0", "effect.emitter[0].module[4].payload.strength=3",
         "effect.emitter[0].module[4].payload.radius=4", "effect.emitter[0].module[5].payload.strength=1.3",
         "effect.emitter[0].module[5].payload.frequency=2", "effect.emitter[0].module[6].payload.axis=1 0 0",
         "effect.emitter[0].module[6].payload.degreesPerSecond=60", "effect.emitter[0].module[7].payload.point=-2 0 3",
         "effect.emitter[0].module[7].payload.strength=2", "effect.emitter[0].module[7].payload.radius=0",
         "effect.emitter[0].module[2].authoringOrder=5", "effect.emitter[0].module[5].authoringOrder=2")
    run(args.cli, "vfx-module-add", "--project", project, "--effect", "/Game/Particles/Contact.kbvfx", "--emitter", "1", "--type", "CollisionPlane")
    edit("Contact", "effect.emitter[0].module[2].payload.normal=-1 0 0", "effect.emitter[0].module[2].payload.distance=-1",
         "effect.emitter[0].module[2].payload.killOnContact=true")

    def cook(name):
        package = root / name
        package.mkdir()
        run(args.cooker, "--project", project, "--target", "Windows.x64", "--output", package / "Game.kbpack",
            "--engine-root", args.engine_root, "--shaderc", args.shaderc, "--cache", root / "CookCache")
        player = package / "Sample.exe"
        shutil.copy2(args.player, player)
        for plugin in args.plugin:
            shutil.copy2(plugin, package / plugin.name)
        require(not (package / "Assets").exists() and not (package / "shaders").exists(), "Package fixture includes loose content")
        return player

    def capture(player, frames, gpu=True, expected=0):
        flags = ["--headless", f"--frames={frames}", "--width=64", "--height=64", "--particle-diagnostics", "--particle-samples=8"]
        if gpu:
            flags.append("--particle-require-gpu")
        output = run(player, *flags, cwd=player.parent, expected=expected)
        lines = [line.split("particle_diagnostics=", 1)[1] for line in output.splitlines() if line.startswith("kb_game: particle_diagnostics=")]
        require(len(lines) == 1, "Player did not emit exactly one terminal particle report")
        report = json.loads(lines[0])
        (player.parent / f"capture-{frames}-{'gpu' if gpu else 'cpu'}.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
        require(report["schema"] == "21kb.particle-runtime/v1" and report["packaged"] and report["exclusive_package_shaders"], "Capture did not come from the exclusive packaged shader provider")
        require(int(report["fixed_step"]) == frames and report["rendered_frames"] == frames, "Player did not advance the requested fixed steps")
        require(report["succeeded"] == (expected == 0), "Diagnostic report and process exit disagree")
        require(report["image"]["succeeded"] and report["image"]["width"] == 64 and report["image"]["height"] == 64 and
                len(bytes.fromhex(report["image"]["rgba8_hex"])) == 64 * 64 * 4, "Packaged scene image readback is missing")
        if gpu and expected == 0:
            require(report["gpu_execution_observed"] and len(report["gpu_captures"]) == 2, "Packaged GPU execution/readback was not observed")
            require(all(row["path"] == "gpu" and row["reason"] == "none" and row["cpu_render_record_count"] == 0 for row in report["emitters"]), "Package fell back to CPU")
            require(all(row["readback_succeeded"] and len(row["raw_instance_records"]) == 8 for row in report["gpu_captures"]), "Actual GPU records are missing")
        return report

    gpu_player = cook("Gpu")
    birth = capture(gpu_player, 8)
    gpu = capture(gpu_player, 60)
    contact_before = next(row for row in birth["gpu_captures"] if row["plane_kill_on_contact"])
    contact_after = next(row for row in gpu["gpu_captures"] if row["plane_kill_on_contact"])
    require(all(row[3] > 0 for row in contact_before["raw_instance_records"]), "Contact particles were not alive before contact")
    require(all(value == 0 for row in contact_after["raw_instance_records"] for value in row), "Contact-dead particles still have drawable GPU records")
    mixed = next(row for row in gpu["gpu_captures"] if row["motion_modules"])
    ops = mixed["motion_modules"]
    require([op["type"] for op in ops] == [3, 4, 5, 3, 4, 5], "Cooked repeated/mixed operation order changed")
    require(math.isclose(ops[0]["strength"], 1.3, abs_tol=1e-6) and ops[0]["parameter"] == 2 and ops[0]["seed"] == "6" and
            math.isclose(ops[3]["strength"], 0.7, abs_tol=1e-6) and ops[3]["parameter"] == 0.5 and ops[3]["seed"] == "3",
            "Reordered noise parameters were overwritten by cooking or runtime")
    require(ops[1]["vector"] == [0, 1, 0] and ops[4]["vector"] == [1, 0, 0] and
            ops[2]["vector"] == [1, 1, 0] and ops[2]["parameter"] == 4 and ops[5]["vector"] == [-2, 0, 3] and ops[5]["parameter"] == 0,
            "Cooked field vectors or radii changed")
    for name in ["Mixed", "Contact"]:
        edit(name, "effect.backendPolicy=CpuDeterministic")
    cpu_player = cook("Cpu")
    cpu = capture(cpu_player, 60, gpu=False)
    cpu_birth = capture(cpu_player, 8, gpu=False)
    cpu_mixed = next(row for row in cpu["emitters"] if row["instance_id"] == mixed["instance_id"])
    cpu_contact = next(row for row in cpu["emitters"] if row["instance_id"] == contact_after["instance_id"])
    require(cpu_contact["cpu_render_record_count"] == 0 and cpu_mixed["cpu_render_record_count"] == 64, "CPU contact/motion reference is invalid")
    position_error = velocity_error = 0.0
    for actual, reference in zip(mixed["raw_instance_records"], cpu_mixed["cpu_position_velocity_samples"], strict=True):
        require(actual[3] > 0, "Mixed GPU particle unexpectedly died")
        for axis in range(3):
            position_error = max(position_error, abs(actual[axis] - reference[axis]))
            velocity_error = max(velocity_error, abs(actual[8 + axis] - reference[3 + axis]))
    require(position_error <= 0.001 and velocity_error <= 0.003,
            f"Cooked GPU disagrees with CPU: position={position_error}, velocity={velocity_error}")
    def pixels(report):
        return bytes.fromhex(report["image"]["rgba8_hex"])

    image_errors = []
    for actual, reference in [(birth, cpu_birth), (gpu, cpu)]:
        actual_pixels, reference_pixels = pixels(actual), pixels(reference)
        require(any(actual_pixels[index] > 8 for index in range(len(actual_pixels)) if index % 4 != 3),
                "Packaged live effect did not render visible pixels")
        mismatches = sum(abs(a - b) > 2 for a, b in zip(actual_pixels, reference_pixels, strict=True))
        require(mismatches <= len(actual_pixels) // 100, f"Packaged GPU/CPU images disagree in {mismatches} channels")
        image_errors.append(mismatches)
    capture(cpu_player, 8, expected=1)
    for name in ["Mixed", "Contact"]:
        edit(name, "effect.backendPolicy=GpuVisualPreferred")
    edit("Contact", "effect.emitter[0].module[2].payload.killOnContact=false")
    control = capture(cook("BounceControl"), 60)
    control_difference = sum(abs(a - b) > 2 for a, b in zip(pixels(gpu), pixels(control), strict=True))
    require(control_difference > 16, "Contact death image did not differ from the live bounce control")
    for invalid in [["--particle-diagnostics"], ["--particle-diagnostics", "--frames=2"], ["--particle-diagnostics", "--headless", "--frames=36001"],
                    ["--particle-samples=64"], ["--particle-samples=0"], ["--particle-samples=4097"], ["--particle-require-gpu"],
                    ["--headless", "--frames=2", "--profile-fixed-step"]]:
        rejected = run(gpu_player, *invalid, expected=1, cwd=gpu_player.parent)
        require("project=" not in rejected and any(reason in history[-1]["stderr"] for reason in
                ["particle diagnostics require", "--particle-samples expects", "--profile-fixed-step is a development option"]),
                "Invalid diagnostic arguments were not refused before starting the scene")
    print(f"PASS: packaged GPU contact/motion/buffer+pixel readback, CPU parity position={position_error:.9g}, velocity={velocity_error:.9g}, "
          f"image_channels={image_errors}, bounce_difference={control_difference}; {root}")


if __name__ == "__main__":
    main()

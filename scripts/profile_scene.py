"""Profile a complete scene through the hidden native player with warmup and repeated runs."""
import argparse
import csv
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import re
import subprocess
import time


def backend_matches(requested, actual):
    if requested == "opengl":
        return re.fullmatch(r"OpenGL(?: \d+\.\d+)?", actual) is not None
    return actual == {"d3d11": "Direct3D 11", "d3d12": "Direct3D 12", "vulkan": "Vulkan"}[requested]


def percentiles(values):
    values = sorted(float(value) for value in values if value is not None and math.isfinite(float(value)))
    if not values:
        return {"samples": 0, "p50": None, "p95": None, "p99": None}
    def at(fraction):
        position = (len(values) - 1) * fraction
        lower = math.floor(position)
        return values[lower] + (values[math.ceil(position)] - values[lower]) * (position - lower)
    return {"samples": len(values), "p50": at(0.5), "p95": at(0.95), "p99": at(0.99)}


def summarize(csv_path, warmup, frames):
    with csv_path.open(newline="", encoding="utf-8") as source:
        rows = list(csv.DictReader(source))
    if len(rows) != warmup + frames:
        raise ValueError(f"Expected {warmup + frames} complete frames, received {len(rows)}")
    rows = rows[warmup:]
    if any(int(row["dropped"]) or int(row["missing_resources"]) for row in rows):
        raise ValueError("Measured scene dropped geometry or used missing resources")
    trace = [json.loads(line) for line in Path(str(csv_path) + ".trace.jsonl").read_text(encoding="utf-8").splitlines()]
    if len(trace) != warmup + frames:
        raise ValueError("Frame trace is incomplete")
    selected = trace[warmup:]
    # GPU results are delayed and may repeat across several CPU frames. Count
    # each completed GPU query once, and reject queries originating in warmup.
    measured_frames = {frame["rendering"]["bgfx_frame"] for frame in selected}
    gpu = {}
    for frame in selected:
        render = frame["rendering"]
        query = render.get("gpu_frame")
        value = render.get("gpu_ms")
        if query is not None and query in measured_frames and value is not None and value >= 0:
            gpu.setdefault(query, value)
    memory = {}
    for frame in selected:
        for name, value in frame["rendering"].get("memory", {}).items():
            if name.endswith("_bytes"):
                memory[name] = max(memory.get(name) or 0, int(value)) if value is not None else memory.get(name)
    cpu = {name: percentiles(float(row[name]) for row in rows) for name in
           ("processing_wall_ms", "simulation_ms", "begin_submit_ms", "end_frame_ms", "scene_sync_ms")}
    return {"cpu_ms": cpu, "gpu_ms": percentiles(gpu.values()), "memory_peak_bytes": memory,
            "frames": frames, "warmup_frames": warmup,
            "observed_configuration": {
                "resolutions": sorted({(int(row["width"]), int(row["height"])) for row in rows}),
                "lighting_paths": sorted({int(row["lighting_path"]) for row in rows})},
            "workload": {name: {"min": min(int(row[name]) for row in rows), "max": max(int(row[name]) for row in rows)}
                         for name in ("draws", "submitted_meshes", "visible_meshes", "scene_lights")},
            "workload_basis": "submit counters summed across render passes and views; not distinct scene entities or lights",
            "gpu_basis": "unique delayed bgfx GPU queries from measured frames; null percentiles when unavailable",
            "memory_basis": selected[-1]["rendering"].get("memory", {}),
            "max_shadow_lights_dropped": max(frame["rendering"].get("shadow_lights_dropped", 0) for frame in selected),
            "max_gi_box_meshes": max(frame["rendering"].get("gi_box_meshes", 0) for frame in selected)}


def drivers():
    if os.name != "nt":
        return {"available": False, "reason": "OS driver enumeration is implemented for Windows; provide --driver-label"}
    script = "Get-CimInstance Win32_VideoController | Select-Object Name,DriverVersion,PNPDeviceID | ConvertTo-Json -Compress"
    try:
        result = subprocess.run(["powershell.exe", "-NoProfile", "-NonInteractive", "-Command", script],
                                capture_output=True, text=True, timeout=20, creationflags=subprocess.CREATE_NO_WINDOW)
        if result.returncode:
            return {"available": False, "reason": result.stderr.strip()}
        values = json.loads(result.stdout)
        return {"available": True, "adapters": values if isinstance(values, list) else [values]}
    except (OSError, ValueError, subprocess.TimeoutExpired) as error:
        return {"available": False, "reason": str(error)}


def project_manifest(path):
    if path is None:
        return {"basis": "player default project; specify --project to fingerprint scene inputs"}
    root = path.resolve(strict=True)
    if root.is_file():
        root = root.parent
    files = {}
    for directory, children, names in os.walk(root):
        children[:] = sorted(name for name in children if name not in ("Saved", "build", "Build", ".git"))
        for name in sorted(names):
            file = Path(directory) / name
            digest = hashlib.sha256()
            with file.open("rb") as stream:
                for chunk in iter(lambda: stream.read(1024 * 1024), b""):
                    digest.update(chunk)
            files[file.relative_to(root).as_posix()] = digest.hexdigest()
    return {"basis": "project files excluding generated Saved/build directories and git metadata; engine resources identified by executable/build separately",
            "files": files}


def run_player(command, player, log_path, timeout):
    flags = subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0
    with log_path.open("w", encoding="utf-8") as log:
        process = subprocess.Popen(command, cwd=player.parent, stdout=log, stderr=subprocess.STDOUT,
                                   creationflags=flags, env=dict(os.environ, CMAKE_BUILD_PARALLEL_LEVEL="2"))
        try:
            return process.wait(timeout=timeout)
        except BaseException:
            if process.poll() is None:
                if os.name == "nt":
                    subprocess.run(["taskkill", "/PID", str(process.pid), "/T", "/F"],
                                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, creationflags=flags)
                else:
                    process.kill()
            process.wait()
            raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--player", required=True, type=Path)
    parser.add_argument("--project", type=Path)
    parser.add_argument("--scene")
    parser.add_argument("--output-dir", required=True, type=Path)
    parser.add_argument("--backend", choices=("d3d11", "d3d12", "vulkan", "opengl"), default="d3d11")
    parser.add_argument("--lighting", choices=("Forward", "ForwardPlus", "Deferred"), default="Deferred")
    parser.add_argument("--width", type=int, default=960)
    parser.add_argument("--height", type=int, default=540)
    parser.add_argument("--warmup", type=int, default=240)
    parser.add_argument("--frames", type=int, default=600)
    parser.add_argument("--repeats", type=int, default=3)
    parser.add_argument("--timeout", type=float, default=180)
    parser.add_argument("--driver-label")
    parser.add_argument("--capture-frame", type=int, default=120,
                        help="separate fixed-step image run; excluded from timing measurements")
    args = parser.parse_args()
    if not (0 <= args.warmup < 120000 and 1 <= args.frames <= 120000 - args.warmup and 1 <= args.repeats <= 100):
        parser.error("warmup + frames must be 1..120000 and repeats 1..100")
    if not (64 <= args.width <= 8192 and 64 <= args.height <= 8192 and math.isfinite(args.timeout) and args.timeout > 0):
        parser.error("dimensions must be 64..8192 and timeout finite and positive")
    if not 1 <= args.capture_frame <= 119996:
        parser.error("capture-frame must be 1..119996")
    player = args.player.resolve(strict=True)
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=False)
    driver_info = drivers()
    report = {"schema": "21kb.scene-benchmark/v1", "status": "RUNNING", "runs": [],
              "platform": platform.platform(), "architecture": platform.machine(),
              "player_sha256": hashlib.sha256(player.read_bytes()).hexdigest(),
              "project_manifest": project_manifest(args.project),
              "drivers": driver_info, "driver_label": args.driver_label,
              "configuration": {key: value for key, value in vars(args).items() if key not in ("player", "project", "output_dir")},
              "limitations": ["CPU submission includes BeginFrame and synchronization; EndFrame/waits are reported separately.",
                              "GPU timer results are delayed; missing query samples are not zeros.",
                              "Registry payload and bgfx texture/target values are estimates, not driver VRAM residency.",
                              "Driver memory can be unavailable or adapter-wide (D3D11/NVAPI); it is not application residency.",
                              "No comparison with other engines is inferred."]}
    try:
        for repeat in range(args.repeats):
            profile = output / f"run-{repeat + 1}.csv"
            command = [str(player), "--headless", "--uncapped", "--profile-fixed-step", f"--frames={args.warmup + args.frames}",
                       f"--profile-file={profile}", f"--width={args.width}", f"--height={args.height}",
                       f"--renderer={args.backend}", f"--profile-lighting={args.lighting}"]
            if args.project:
                command.append(f"--project={args.project.resolve()}")
            if args.scene:
                command.append(f"--scene={args.scene}")
            start = time.monotonic()
            code = run_player(command, player, output / f"run-{repeat + 1}.log", args.timeout)
            if code:
                raise RuntimeError(f"Player failed with exit code {code}; see run-{repeat + 1}.log")
            metadata = json.loads(Path(str(profile) + ".meta.json").read_text(encoding="utf-8"))
            expected_backend = {"d3d11": "Direct3D 11", "d3d12": "Direct3D 12", "vulkan": "Vulkan", "opengl": "OpenGL"}[args.backend]
            if not backend_matches(args.backend, metadata["backend"]):
                raise RuntimeError(f"Requested {expected_backend}, got {metadata['backend']}")
            matched = [adapter for adapter in driver_info.get("adapters", [])
                       if f"VEN_{metadata['vendor_id']:04X}&DEV_{metadata['device_id']:04X}" in adapter.get("PNPDeviceID", "").upper()]
            row = summarize(profile, args.warmup, args.frames)
            observed = row["observed_configuration"]
            if observed["resolutions"] != [(args.width, args.height)] or observed["lighting_paths"] != [
                    {"Forward": 1, "ForwardPlus": 2, "Deferred": 3}[args.lighting]]:
                raise RuntimeError(f"Actual scene configuration differs from request: {observed}")
            row.update(repeat=repeat + 1, seconds=time.monotonic() - start, metadata=metadata,
                       matched_drivers=matched, driver_identified=bool(matched or args.driver_label), command=command)
            report["runs"].append(row)
            (output / "summary.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
        # Readback can stall a frame. Capture the same fixed-step scene in a
        # separate process so its image evidence never pollutes the percentiles.
        capture = [part for part in command if not part.startswith(("--profile-file=", "--frames="))]
        picture = output / "reference.png"
        capture += [f"--frames={args.capture_frame + 4}", f"--profile-file={output / 'reference.csv'}",
                    f"--screenshot-frame={args.capture_frame}", f"--screenshot-file={picture}"]
        if run_player(capture, player, output / "reference.log", args.timeout):
            raise RuntimeError("Reference image process failed; see reference.log")
        if not picture.is_file() or picture.stat().st_size == 0:
            raise RuntimeError("Reference image was not captured")
        report["reference_image"] = {"file": picture.name, "frame": args.capture_frame,
                                     "sha256": hashlib.sha256(picture.read_bytes()).hexdigest(), "command": capture}
        if project_manifest(args.project) != report["project_manifest"]:
            raise RuntimeError("Project inputs changed during measurement")
        report["status"] = "PASS"
    except BaseException as error:
        report.update(status="FAIL", error=str(error))
        raise
    finally:
        (output / "summary.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps({"status": "PASS", "summary": str(output / "summary.json"), "repeats": len(report["runs"])}))


if __name__ == "__main__":
    main()

"""A reference audio project: stream, one-shot, spatial voice, buses, snapshot and DSP, rendered to real PCM and checked.

The project is authored with kb_cli (WAV sines imported, a mixer with a bus hierarchy, a low-pass chain and a snapshot, audio sources and a
listener) and verified three ways: offline by `kb_cli run` (a Lua script renders PCM through the real provider and compares the levels with
the analytic ones), by the `run --report audio` JSON (blocks, short blocks, mix cost), and by the cooked package in the packaged player. A
changed expectation must fail, so the script is also run with a wrong one.
"""

import argparse
import ctypes
from ctypes import wintypes
import json
import math
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import time
import wave


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def tone(path, hz, amplitude, seconds, rate=48000):
    with wave.open(str(path), "wb") as output:
        output.setparams((1, 2, rate, 0, "NONE", "not compressed"))
        output.writeframes(b"".join(struct.pack("<h", round(amplitude * 32767 * math.sin(2 * math.pi * hz * n / rate)))
                                    for n in range(int(rate * seconds))))


class ProcessMemoryCounters(ctypes.Structure):
    _fields_ = [("cb", wintypes.DWORD), ("PageFaultCount", wintypes.DWORD),
                *[(name, ctypes.c_size_t) for name in (
                    "PeakWorkingSetSize", "WorkingSetSize", "QuotaPeakPagedPoolUsage", "QuotaPagedPoolUsage",
                    "QuotaPeakNonPagedPoolUsage", "QuotaNonPagedPoolUsage", "PagefileUsage", "PeakPagefileUsage", "PrivateUsage")]]


def process_memory(process):
    counters = ProcessMemoryCounters()
    counters.cb = ctypes.sizeof(counters)
    query = ctypes.windll.psapi.GetProcessMemoryInfo
    query.argtypes = [wintypes.HANDLE, ctypes.POINTER(ProcessMemoryCounters), wintypes.DWORD]
    query.restype = wintypes.BOOL
    require(query(int(process._handle), ctypes.byref(counters), counters.cb), "Cannot measure packaged process memory")
    return counters.PrivateUsage, counters.PeakWorkingSetSize


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ["cli", "cooker", "player", "engine-root", "test-root", "shaderc"]:
        parser.add_argument("--" + name, required=True, type=Path)
    parser.add_argument("--qualify-device", action="store_true", help="Require real native output callbacks in the packaged player")
    parser.add_argument("--plugin", action="append", type=Path, required=True)
    parser.add_argument("--runtime-dll", action="append", type=Path, default=[])
    args = parser.parse_args()
    for name in ["cli", "cooker", "player", "engine_root", "test_root", "shaderc"]:
        setattr(args, name, getattr(args, name).resolve())
    args.plugin = [path.resolve() for path in args.plugin]
    args.runtime_dll = [path.resolve() for path in args.runtime_dll]
    require(os.name == "nt", "The reference audio test uses the native Windows player")
    ctypes.windll.kernel32.SetErrorMode(32771)
    args.test_root.mkdir(parents=True, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix="AudioReference", dir=args.test_root))
    temp = root / "Temp"
    temp.mkdir()
    env = dict(os.environ, CMAKE_BUILD_PARALLEL_LEVEL="2", TEMP=str(temp), TMP=str(temp), MSYS_NO_PATHCONV="1")
    project = root / "Ref"
    history = []

    def run(exe, *tail, expected=0, cwd=root, timeout=120, memory_limit=None):
        start = time.monotonic()
        process = subprocess.Popen([str(exe), *map(str, tail)], cwd=cwd, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                   creationflags=subprocess.CREATE_NO_WINDOW)
        memory = dict(sampledPeakPrivateBytes=0, peakWorkingSetBytes=0, samples=0, limitBytes=memory_limit)
        try:
            while True:
                if memory_limit is not None:
                    private, working = process_memory(process)
                    memory["sampledPeakPrivateBytes"] = max(memory["sampledPeakPrivateBytes"], private)
                    memory["peakWorkingSetBytes"] = max(memory["peakWorkingSetBytes"], working)
                    memory["samples"] += 1
                remaining = timeout - (time.monotonic() - start)
                if remaining <= 0:
                    raise subprocess.TimeoutExpired(process.args, timeout)
                try:
                    stdout, stderr = process.communicate(timeout=min(0.05, remaining) if memory_limit is not None else remaining)
                    break
                except subprocess.TimeoutExpired:
                    if memory_limit is None:
                        raise
        except BaseException:
            subprocess.run(["taskkill", "/PID", str(process.pid), "/T", "/F"], capture_output=True,
                           creationflags=subprocess.CREATE_NO_WINDOW, check=False)
            process.communicate()
            raise
        row = dict(executable=str(exe), arguments=list(map(str, tail)), code=process.returncode, seconds=time.monotonic() - start,
                   stdout=stdout.decode("utf8", "replace"), stderr=stderr.decode("utf8", "replace"))
        if memory_limit is not None:
            row["processMemory"] = memory
        history.append(row)
        (root / "commands.json").write_text(json.dumps(history, indent=2), encoding="utf8")
        if memory_limit is not None:
            require(memory["sampledPeakPrivateBytes"] < memory_limit and memory["peakWorkingSetBytes"] < memory_limit,
                    "Packaged process exceeded its memory budget: " + str(memory))
        require(process.returncode == expected, f"Unexpected command result: {row}")
        return row["stdout"] + row["stderr"]

    def cli(*tail, **kwargs):
        return run(args.cli, *tail, **kwargs)

    cli("new", "--parent", root, "--name", "Ref", "--template", "Empty")
    for name, hz, amplitude, seconds in [("Music", 220, 0.4, 4.0), ("Drone", 220, 0.4, 4.0), ("Voice", 440, 0.4, 4.0), ("Hit", 880, 0.5, 0.3)]:
        wav = root / (name + ".wav")
        tone(wav, hz, amplitude, seconds)
        cli("import", "--project", project, "--destination", "/Game/Audio", wav)
    mixer = "/Game/Audio/Main.kbmixer"
    cli("audio-mixer", "new", "--project", project, "--name", "Main",
        "--bus", "Output", "--bus", "Music", "--bus", "Sfx", "--bus", "Voice", "--bus", "Filtered",
        "--set", "Music.parent=Output", "--set", "Sfx.parent=Output", "--set", "Voice.parent=Output", "--set", "Filtered.parent=Output",
        "--set", "Output.volume=0.8", "--set", "Music.volume=0.5",
        "--effect-bus", "Filtered", "--add-effect", "lp=lowpass", "--set-effect", "lp.cutoffHz=55", "--set-effect", "lp.order=4",
        "--add-snapshot", "Quiet", "--snapshot", "Quiet", "--set-snapshot", "Music=0.1")
    cli("audio-mixer", "assign", "--project", project, "--mixer", mixer)

    def source(name, clip, bus, spatial=False, autoplay=True, x=None):
        cli("entity", "add", "--project", project, "--name", name, "--create", "Audio Source")
        settings = ["--set", f"clipAssetId=/Game/Audio/{clip}.21kb", "--set", "loop=true", "--set", f"autoplay={'true' if autoplay else 'false'}",
                    "--set", f"spatial={'true' if spatial else 'false'}", "--set", f"outputBus={bus}"]
        if spatial:
            settings += ["--set", "spatialBlend=1", "--set", "minDistance=1", "--set", "maxDistance=50"]
        cli("component", "set", "--project", project, "--entity", name, "--type", "Audio Source", *settings)
        if x is not None:
            cli("component", "set", "--project", project, "--entity", name, "--type", "Transform", "--set", f"localPosition={x},0,5")

    source("Music", "Music", "Music")
    source("Drone", "Drone", "Filtered")
    source("Voice", "Voice", "Voice", spatial=True, autoplay=False, x=-6)
    # The script plays the Hit clip by path; a source that names it keeps it in the package (a path in a script is not a dependency).
    source("HitClip", "Hit", "Sfx", autoplay=False)
    cli("entity", "add", "--project", project, "--name", "Listener", "--create", "Audio Listener")
    cli("entity", "add", "--project", project, "--name", "Camera", "--create", "Camera")
    cli("script-new", "--project", project, "--name", "Reference", "--language", "lua")
    script = project / "Assets/Scripts/Reference.lua"
    original = (Path(__file__).resolve().parent / "AudioReference.lua").read_text(encoding="utf8")
    script.write_text(original, encoding="utf8")
    cli("scene-attach", "--project", project, "--scene", "Assets/Scenes/Main.21kbscene", "--node", "Camera", "--script", "/Game/Scripts/Reference.lua")

    # Offline, through the CLI: every level in the script is checked against the analytic one.
    text = cli("run", "--project", project, "--frames", "3")
    require("audio reference project verified" in text and "0 diagnostics" in text, "The reference script did not verify offline PCM: " + text)

    # The report: complete blocks, none short, a mix cheaper than real time, and what the provider does not report.
    cli("run", "--project", project, "--frames", "4", "--report", "audio", "--json", "--pcm-frames", "4096")
    report = json.loads(history[-1]["stdout"])
    require(report["succeeded"] and report["offline"] and report["blocks"] == 4 and report["short_blocks"] == 0 and
            report["pcm_frames"] == 16384, "The audio report did not show four complete blocks")
    require(report["realtime_factor"] is not None and 0.0 < report["realtime_factor"] < 1.0, "The mix is not faster than real time: " + str(report["realtime_factor"]))
    require(int(report["worst_block_mix_ns"]) > 0 and {"voice_count", "decoder_memory_bytes", "device_underruns"} <= set(report["unavailable_metrics"]),
            "The report does not say which metrics the provider cannot give")
    runtime = report["runtime"]
    require(runtime["supported"] and runtime["offline"] and int(runtime["allocated_bytes"]) > 0
            and int(runtime["peak_allocated_bytes"]) >= int(runtime["allocated_bytes"])
            and int(runtime["device_blocks"]) == 0, "Offline diagnostics confused allocator/device state")
    require(report["peak"] > 0.15 and [bus["bus"] for bus in report["buses"]].count("Filtered") == 1, "The report is missing the mix")

    # A wrong expectation fails with its message.
    script.write_text(original.replace('near(held("Music", 0), 0.4 * 0.5, 0.01, "Music bus peak")',
                                       'near(held("Music", 0), 0.4 * 0.6, 0.01, "Music bus peak")'), encoding="utf8")
    failing = cli("run", "--project", project, "--frames", "3", expected=1)
    require("Music bus peak" in failing, "A wrong expectation did not fail with its message: " + failing)
    script.write_text(original, encoding="utf8")

    if args.qualify_device:
        script.write_text(original + "\n" + Path(__file__).with_name("AudioDeviceReference.lua").read_text(encoding="utf8"), encoding="utf8")

    # The package: cooked with no loose assets, run by the packaged player.
    output = root / "Packaged"
    output.mkdir()
    run(args.cooker, "--project", project, "--target", "Windows.x64", "--output", output / "Game.kbpack", "--engine-root", args.engine_root,
        "--shaderc", args.shaderc, "--cache", root / "CookCache")
    player = output / "Reference.exe"
    for source_file in [args.player, *args.plugin, *args.runtime_dll]:
        shutil.copy2(source_file, player if source_file == args.player else output / source_file.name)
    require(not (output / "Assets").exists(), "The package used loose assets")
    # Hide the authoring project during the shipped-player run to reject loose-source dependencies.
    hidden_project = project.with_name("Ref.hidden")
    project.rename(hidden_project)
    try:
        packaged = run(player, "--headless", "--frames=1800" if args.qualify_device else "--frames=3",
                       "--width=64", "--height=64", *([] if args.qualify_device else ["--uncapped"]), cwd=output, timeout=180,
                       memory_limit=512 * 1024 * 1024 if args.qualify_device else None)
    finally:
        hidden_project.rename(project)
    if args.qualify_device:
        require(packaged.count("audio device reference verified") == 1 and "audio device reference stopped" in packaged,
                "Packaged device qualification did not complete: " + packaged)
    require(packaged.count("kb_game: script: audio reference project verified") == 1 and "shutdown=clean" in packaged,
            "The packaged player did not verify the reference project: " + packaged)
    (root / "result.json").write_text(json.dumps(dict(result="PASS", report=report, deviceQualified=args.qualify_device, processMemory=history[-1].get("processMemory"), negativeControl="a wrong expectation failed the CLI run",
        limitation="Offline PCM and optional OS output callback qualification. Allocation counters exclude asset/STL/driver memory; "
                   "callback period is buffering cadence, not end-to-end acoustic latency. OS underruns and other platforms are not measured."), indent=2), encoding="utf8")
    print(f"PASS: reference audio project offline and packaged; {root}")


if __name__ == "__main__":
    main()

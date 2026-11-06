"""Compare an installed material fixture on physical Android with its native reference.

The APK must contain the twelve-material scene prepared by the Web material workflow.
Only the specified test application is installed, launched and stopped; logcat is not cleared.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import subprocess
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[4] / "scripts"))
from png_readback import read_png


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ("adb", "apk", "player", "reference-pack", "output"):
        parser.add_argument("--" + key, type=Path, required=True)
    parser.add_argument("--compact-reference-pack", type=Path)
    parser.add_argument("--serial", required=True)
    parser.add_argument("--application-id", default="com.kbengine.materialmatrix")
    parser.add_argument("--lighting-path", choices=("forward", "deferred"), required=True)
    args = parser.parse_args()
    args.output = args.output.resolve()
    args.output.mkdir(parents=True, exist_ok=True)
    report = dict(result="RUNNING", path=args.lighting_path, commands=[], regions=[])

    def save():
        (args.output / "result.json").write_text(json.dumps(report, indent=2), encoding="utf-8")

    def run(*command, binary=False, timeout=120):
        result = subprocess.run(list(map(str, command)), stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            timeout=timeout, creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        report["commands"].append(dict(command=list(map(str, command)), exitCode=result.returncode,
            stderr=result.stderr.decode("utf-8", errors="replace")))
        save()
        if result.returncode:
            raise RuntimeError("Command failed: " + str(report["commands"][-1]))
        return result.stdout if binary else result.stdout.decode("utf-8", errors="replace")

    def adb(*command, **options):
        return run(args.adb, "-s", args.serial, *command, **options)

    launched = False
    try:
        if adb("shell", "getprop", "ro.kernel.qemu").strip() == "1":
            raise RuntimeError("This qualification requires a physical Android device")
        report["api"] = adb("shell", "getprop", "ro.build.version.sdk").strip()
        report["apkSha256"] = hashlib.sha256(args.apk.read_bytes()).hexdigest()
        adb("install", "-r", args.apk.resolve())
        adb("shell", "am", "force-stop", args.application_id)
        adb("shell", "input", "keyevent", "KEYCODE_WAKEUP")
        adb("shell", "am", "start", "-W", "-n", args.application_id + "/com.kbengine.runtime.MainActivity")
        launched = True
        deadline = time.monotonic() + 40
        pid = adb("shell", "pidof", args.application_id).strip().split()[0]
        while True:
            logs = adb("logcat", "-d", "--pid=" + pid, "-s", "21kb:I")
            (args.output / "runtime.log").write_text(logs, encoding="utf-8")
            match = re.search(r"backend=(.+?) resolution=(\d+)x(\d+) first-frame=rendered", logs)
            if match:
                break
            if time.monotonic() >= deadline:
                raise RuntimeError("No rendered frame; inspect runtime.log and the device's lock screen")
            time.sleep(0.2)
        backend, width, height = match[1], int(match[2]), int(match[3])
        expected = "0" if args.lighting_path == "forward" else "2"
        if "packaged lighting path=" + expected not in logs:
            raise RuntimeError("Android did not apply the saved lighting path")
        if "material program fallback:" in logs or "render error kind=" in logs:
            raise RuntimeError("Android reported a missing material program or rendering error")
        if backend != "Vulkan" and not backend.startswith("OpenGL ES 3."):
            raise RuntimeError("Unexpected physical-device backend: " + backend)
        compact = backend.startswith("OpenGL ES 3.") and args.lighting_path == "deferred"
        reference = args.compact_reference_pack if compact else args.reference_pack
        if reference is None:
            raise RuntimeError("A compact reference pack is required for this backend")
        report.update(backend=backend, width=width, height=height, compact=compact,
            referencePackSha256=hashlib.sha256(reference.read_bytes()).hexdigest())
        # The fixed-exposure scene is static. Allow uploads and initial shader work to settle.
        time.sleep(3)
        logs = adb("logcat", "-d", "--pid=" + pid, "-s", "21kb:I")
        (args.output / "runtime.log").write_text(logs, encoding="utf-8")
        if "material program fallback:" in logs or "render error kind=" in logs:
            raise RuntimeError("Android reported a material or rendering failure after startup")
        if compact and "material capability fallback:" not in logs:
            raise RuntimeError("Android did not diagnose its four-target material reduction")
        picture = args.output / "android.png"
        picture.write_bytes(adb("exec-out", "screencap", "-p", binary=True))
        pixels = read_png(picture)
        if len(pixels) != height or len(pixels[0]) != width:
            raise RuntimeError("Screenshot extent differs from the actual render surface; no implicit resize is allowed")
        native = args.output / "native.png"
        output = run(args.player.resolve(), "--project=" + str(reference.resolve()), "--headless", "--uncapped",
            "--profile-fixed-step", "--profile-file=" + str(args.output / "native-profile.csv"),
            "--renderer=d3d11", "--width=" + str(width), "--height=" + str(height), "--frames=124",
            "--screenshot-frame=120", "--screenshot-file=" + str(native))
        (args.output / "native.log").write_text(output, encoding="utf-8")
        control = read_png(native)
        scale = height / (2 * math.tan(math.radians(30)) * 11)
        half = round(scale * 0.95)
        names = ["Base", "Metal", "Mapped", "Mask", "Blend", "Emissive", "Coat", "Scatter",
                 "Combined", "Tiled", "Point", "Graph"]
        for index, name in enumerate(names):
            cx = round(width / 2 + (index % 3 - 1) * 2.2 * scale)
            cy = round(height / 2 - (3.3 - index // 3 * 2.2) * scale)
            deltas = [abs(pixels[y][x][c] - control[y][x][c])
                for y in range(cy-half, cy+half) for x in range(cx-half, cx+half) for c in range(3)]
            mean = sum(deltas) / len(deltas)
            fraction = sum(delta > 12 for delta in deltas) / len(deltas)
            report["regions"].append(dict(material=name, mean=mean, maximum=max(deltas), differing=fraction))
            save()
            if mean > 3 or fraction > 0.03:
                raise RuntimeError("Material differs from its capability reference: " + name)
        report["result"] = "PASS"
    except BaseException as error:
        report.update(result="FAIL", error=str(error))
        raise
    finally:
        if launched:
            adb("shell", "am", "force-stop", args.application_id)
        save()


if __name__ == "__main__":
    main()

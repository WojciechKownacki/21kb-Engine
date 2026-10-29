"""Exercise a static packaged scene across Android backgrounding, memory pressure and process death.

This checks surface reconstruction and rendered output, with optional UI save/settings
coverage. Audio qualification is separate. Every command targets the selected device.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[4] / "scripts"))
from png_readback import read_png


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--adb", type=Path, required=True)
    parser.add_argument("--apk", type=Path, required=True)
    parser.add_argument("--serial", required=True)
    parser.add_argument("--application-id", required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--rotation", action="store_true", help="Verify both landscape rotations and restore display policy afterward")
    parser.add_argument("--storage-flow", action="store_true", help="Exercise the shared UI save/settings fixture and verify persistence after process death")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    report = dict(result="RUNNING", commands=[], captures=[], apkSha256=hashlib.sha256(args.apk.read_bytes()).hexdigest())

    def save():
        (args.output / "result.json").write_text(json.dumps(report, indent=2), encoding="utf-8")

    def adb(*command, binary=False):
        result = subprocess.run([str(args.adb), "-s", args.serial, *map(str, command)],
            capture_output=True, timeout=120, creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        report["commands"].append(dict(args=list(map(str, command)), exitCode=result.returncode,
            stderr=result.stderr.decode(errors="replace")))
        if command[:2] in (("shell", "am"), ("install", "-r")):
            report["commands"][-1]["stdout"] = result.stdout.decode(errors="replace")
        save()
        if result.returncode:
            raise RuntimeError("Device command failed: " + str(command))
        return result.stdout if binary else result.stdout.decode(errors="replace")

    def launch():
        adb("shell", "am", "start", "-W", "-n", args.application_id + "/com.kbengine.runtime.MainActivity")
        return adb("shell", "pidof", args.application_id).strip().split()[0]

    def logs(pid):
        text = adb("logcat", "-d", "--pid=" + pid, "-s", "21kb:I")
        (args.output / ("runtime-" + pid + ".log")).write_text(text, encoding="utf-8")
        if "render error kind=" in text or "renderer initialization failed" in text:
            raise RuntimeError("Runtime reported a rendering error")
        return text

    def wait_log(pid, pattern, occurrences=1):
        deadline = time.monotonic() + 60
        while time.monotonic() < deadline:
            text = logs(pid)
            matches = list(re.finditer(pattern, text))
            if len(matches) >= occurrences:
                return matches[-1]
            time.sleep(0.25)
        raise RuntimeError("Runtime did not report " + pattern)

    def wait_storage(pid, predicate):
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            states = re.findall(r"storage-flow (\{[^\r\n]+\})", logs(pid))
            if states:
                state = json.loads(states[-1])
                if predicate(state):
                    report.setdefault("storage", []).append(state)
                    return state
            time.sleep(0.25)
        raise RuntimeError("Save/settings state did not reach the expected value")

    def capture(label, reference=None):
        time.sleep(3)
        path = args.output / (label + ".png")
        path.write_bytes(adb("exec-out", "screencap", "-p", binary=True))
        pixels = read_png(path)
        if args.storage_flow:
            # Android system insets move the canvas origin. Locate the fixture's
            # blue button in the actual framebuffer before dispatching touch.
            points = [(x, y) for y in range(0, len(pixels), 2)
                for x in range(0, len(pixels[0]), 2)
                if pixels[y][x][2] > pixels[y][x][0] + 50 and pixels[y][x][2] > pixels[y][x][1] + 50]
            if not points:
                raise RuntimeError("The native save button did not render")
            left, top = min(x for x, _ in points), min(y for _, y in points)
            right, bottom = max(x for x, _ in points) + 2, max(y for _, y in points) + 2
            if abs(right - left - 200) > 2 or abs(bottom - top - 160) > 2 or \
                    len(points) * 4 < (right - left) * (bottom - top) * 0.95:
                raise RuntimeError("The native save button has incorrect framebuffer geometry")
            report.setdefault("buttons", []).append(dict(label=label, left=left, top=top, right=right, bottom=bottom))
        if reference is not None:
            if len(pixels) != len(reference) or len(pixels[0]) != len(reference[0]):
                raise RuntimeError("Recreated surface changed image dimensions")
            # Exclude system overlays at the screen edges; the scene must be static.
            height, width = len(pixels), len(pixels[0])
            deltas = [abs(pixels[y][x][c] - reference[y][x][c])
                for y in range(height // 10, height * 9 // 10, 2)
                for x in range(width // 10, width * 9 // 10, 2) for c in range(3)]
            mean = sum(deltas) / len(deltas)
            report["captures"].append(dict(label=label, meanDifference=mean))
            if mean > 2:
                raise RuntimeError("Recreated scene differs from its initial frame")
        return pixels

    rotation_policy = None
    stay_awake = None

    def rotate(rotation):
        adb("shell", "wm", "set-user-rotation", "lock", str(rotation))
        deadline = time.monotonic() + 15
        while time.monotonic() < deadline:
            state = adb("shell", "dumpsys", "window", "displays")
            if re.search(r"mDisplayInfo=.*?\brotation " + str(rotation) + r",", state):
                return
            time.sleep(0.25)
        raise RuntimeError("Display did not adopt the requested rotation")

    try:
        report["deviceKind"] = "emulator" if adb("shell", "getprop", "ro.kernel.qemu").strip() == "1" else "physical"
        report["api"] = adb("shell", "getprop", "ro.build.version.sdk").strip()
        stay_awake = adb("shell", "settings", "get", "global", "stay_on_while_plugged_in").strip()
        adb("shell", "svc", "power", "stayon", "usb")
        adb("install", "-r", args.apk.resolve())
        adb("shell", "am", "force-stop", args.application_id)
        adb("shell", "input", "keyevent", "KEYCODE_WAKEUP")
        adb("shell", "wm", "dismiss-keyguard")
        if args.rotation:
            state = adb("shell", "dumpsys", "window", "displays")
            policy = re.search(r"mUserRotationMode=USER_ROTATION_(FREE|LOCKED) mUserRotation=ROTATION_(\d+)", state)
            fixed = re.search(r"mFixedToUserRotation=(true|false)", state)
            if not policy or not fixed:
                raise RuntimeError("Cannot save the existing display rotation policy")
            rotation_policy = ("free" if policy[1] == "FREE" else "lock", int(policy[2]) // 90,
                "enabled" if fixed[1] == "true" else "disabled")
            adb("shell", "wm", "set-fix-to-user-rotation", "enabled")
            rotate(1)
        pid = launch()
        wait_log(pid, r"lifecycle frame-presented generation=1\b")
        wait_log(pid, r"first-frame=rendered")
        reference = capture("initial")
        saved_state = None
        if args.storage_flow:
            initial = wait_storage(pid, lambda state: state["ready"])
            button = report["buttons"][-1]
            adb("shell", "input", "tap", str((button["left"] + button["right"]) // 2),
                str((button["top"] + button["bottom"]) // 2))
            saved_state = wait_storage(pid, lambda state: state["counter"] == initial["counter"] + 1 and
                state["written"] and state["settingsWritten"] and state["note"] == "latest")
        if args.rotation:
            rotate(3)
            capture("reverse-landscape", reference)
            rotate(1)
            capture("landscape-restored", reference)
        for cycle in range(2):
            previous = max(map(int, re.findall(r"lifecycle frame-presented generation=(\d+)", logs(pid))))
            adb("shell", "input", "keyevent", "KEYCODE_HOME")
            time.sleep(1)
            pressure_count = logs(pid).count("lifecycle memory-pressure:")
            output = adb("shell", "am", "send-trim-memory", args.application_id, "COMPLETE")
            if "Error" in output:
                raise RuntimeError(output)
            wait_log(pid, r"lifecycle memory-pressure:", pressure_count + 1)
            wait_log(pid, r"lifecycle renderer-released generation=" + str(previous) + r" meshes=0 materials=0 textures=0")
            if launch() != pid:
                raise RuntimeError("Background cycle unexpectedly restarted the process")
            wait_log(pid, r"lifecycle frame-presented generation=" + str(previous + 1) + r"\b")
            capture("resume-" + str(cycle), reference)
        adb("shell", "input", "keyevent", "KEYCODE_HOME")
        time.sleep(1)
        adb("shell", "am", "kill", args.application_id)
        time.sleep(1)
        new_pid = launch()
        if new_pid == pid:
            raise RuntimeError("Background process was not killed")
        wait_log(new_pid, r"lifecycle frame-presented generation=1\b")
        if saved_state is not None:
            wait_storage(new_pid, lambda state: state["ready"] and state["loaded"] and state["settingsLoaded"] and
                state["counter"] == saved_state["counter"] and state["note"] == saved_state["note"] and
                state["label"] == saved_state["label"])
        capture("process-restart", reference)
        report["result"] = "PASS"
    except BaseException as error:
        report.update(result="FAIL", error=str(error))
        raise
    finally:
        try:
            try:
                if rotation_policy is not None:
                    mode, rotation, fixed = rotation_policy
                    adb("shell", "wm", "set-user-rotation", "lock", str(rotation))
                    if mode == "free":
                        adb("shell", "wm", "set-user-rotation", "free")
                    adb("shell", "wm", "set-fix-to-user-rotation", fixed)
                    state = adb("shell", "dumpsys", "window", "displays")
                    expected_mode = "FREE" if mode == "free" else "LOCKED"
                    expected_fixed = "true" if fixed == "enabled" else "false"
                    if not re.search(r"mUserRotationMode=USER_ROTATION_" + expected_mode +
                            r" mUserRotation=ROTATION_" + str(rotation * 90) + r"\b", state) or \
                            "mFixedToUserRotation=" + expected_fixed not in state:
                        raise RuntimeError("Display rotation policy was not restored")
                    report["restoredRotationPolicy"] = True
            finally:
                try:
                    adb("shell", "am", "force-stop", args.application_id)
                finally:
                    if stay_awake is not None:
                        if stay_awake == "null":
                            adb("shell", "settings", "delete", "global", "stay_on_while_plugged_in")
                        else:
                            adb("shell", "settings", "put", "global", "stay_on_while_plugged_in", stay_awake)
        except BaseException as error:
            report.update(result="FAIL", cleanupError=str(error))
            raise
        finally:
            save()


if __name__ == "__main__":
    main()

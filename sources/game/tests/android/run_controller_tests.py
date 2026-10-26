"""Run the real Android controller adapter through a windowless GameActivity test APK.

Requires an authorized Android host with the standard shell uinput test tool.
Virtual devices are registered without injecting input into other applications.
Java MotionEvents and KeyEvents go through the test GameActivity's normal queue.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import queue
import subprocess
import threading
import time

PACKAGE = "com.kbengine.controllertests"
HIDDEN = getattr(subprocess, "CREATE_NO_WINDOW", 0)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--adb", default="adb")
    parser.add_argument("--serial", required=True)
    parser.add_argument("--apk", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--touch-only", action="store_true", help="Exercise touch and surface lifecycle without requiring shell uinput")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    adb = [args.adb, "-s", args.serial]

    def command(*parts: str) -> str:
        result = subprocess.run(adb + list(parts), capture_output=True, text=True,
                                encoding="utf-8", errors="replace", timeout=45,
                                creationflags=HIDDEN)
        if result.returncode != 0:
            raise RuntimeError("Android command failed: " + result.stdout + result.stderr)
        return result.stdout

    def register(label: str):
        axes = [0, 1, 2, 5, 9, 10, 16, 17]
        registration = dict(id=1, command="register", name="Engine Controller Test " + label,
                            vid=1, pid=1 if label == "A" else 2, bus="usb",
                            port="engine-controller-test-" + label, configuration=[
                                dict(type=100, data=[1, 3]),
                                dict(type=101, data=[304, 305, 307, 308, 310, 311, 314, 315, 317, 318]),
                                dict(type=103, data=axes)], abs_info=[])
        for axis in axes:
            stick, hat = axis in [0, 1, 2, 5], axis in [16, 17]
            registration["abs_info"].append(dict(code=axis, info=dict(value=0,
                minimum=-32768 if stick else (-1 if hat else 0),
                maximum=32767 if stick else (1 if hat else 255), fuzz=0,
                flat=4096 if stick else 0, resolution=0)))
        log = (args.output / ("uinput-" + label + ".log")).open("ab")
        process = subprocess.Popen(adb + ["shell", "uinput", "-"], stdin=subprocess.PIPE,
                                   stdout=log, stderr=subprocess.STDOUT, creationflags=HIDDEN)
        assert process.stdin is not None
        process.stdin.write((json.dumps(registration) + "\n").encode())
        process.stdin.flush()
        return process, log

    def close(device) -> None:
        process, log = device
        if process.stdin is not None and not process.stdin.closed:
            process.stdin.close()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.terminate()
            process.wait(timeout=5)
        log.close()

    devices = {}
    instrumentation = None
    output = []
    started = time.monotonic()
    report = dict(schema="21kb.android-controller-tests/v1", result="FAIL",
                  host="Android with OS virtual controllers", gameActivity="4.4.2", controller="2.0.2")
    if args.touch_only:
        report.update(host="Android touch host", scope="touch-only", controllers="not tested")
    try:
        installed = command("install", "-r", "-t", str(args.apk.resolve()))
        if "Success" not in installed:
            raise RuntimeError("Test APK installation did not succeed")
        for label in ([] if args.touch_only else ["A", "B"]):
            devices[label] = register(label)
        until = time.monotonic() + 15
        while True:
            if args.touch_only:
                break
            if any(device[0].poll() is not None for device in devices.values()):
                raise RuntimeError("OS uinput fixture process exited; inspect uinput logs")
            inventory = command("shell", "dumpsys", "input")
            if all("Engine Controller Test " + label in inventory for label in ["A", "B"]):
                break
            if time.monotonic() >= until:
                raise RuntimeError("OS controller registration failed; inspect uinput logs")
            time.sleep(0.1)
        instrumentation = subprocess.Popen(adb + ["shell", "am", "instrument", "-w", "-r"] +
            (["-e", "touchOnly", "true"] if args.touch_only else []) + [
            PACKAGE + "/.ControllerInstrumentation"], stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            text=True, encoding="utf-8", errors="replace", creationflags=HIDDEN)
        lines: queue.Queue[str | None] = queue.Queue()

        def read_output() -> None:
            assert instrumentation is not None and instrumentation.stdout is not None
            for line in instrumentation.stdout:
                lines.put(line)
            lines.put(None)

        reader = threading.Thread(target=read_output, daemon=True)
        reader.start()
        until = time.monotonic() + 120
        while True:
            if time.monotonic() >= until:
                raise RuntimeError("Controller instrumentation timeout")
            try:
                line = lines.get(timeout=1)
            except queue.Empty:
                continue
            if line is None:
                break
            output.append(line)
            print(line, end="", flush=True)
            if "phase=disconnect-a" in line:
                close(devices.pop("A"))
            if "phase=reconnect-a" in line:
                devices["A"] = register("A")
        text = "".join(output)
        (args.output / "instrumentation.log").write_text(text, encoding="utf-8")
        instrumentation.wait(timeout=10)
        if (instrumentation.returncode != 0 or "INSTRUMENTATION_RESULT: result=PASS" not in text or
                "INSTRUMENTATION_RESULT: touch=PASS" not in text or
                "INSTRUMENTATION_CODE: -1" not in text):
            raise RuntimeError("Native GameActivity controller assertions failed")
        if args.touch_only and "INSTRUMENTATION_RESULT: scope=touch-only" not in text:
            raise RuntimeError("The APK did not acknowledge touch-only scope")
        report.update(result="PASS", evidence="instrumentation.log", checks=[
            "real GameActivity key/motion queue and Paddleboat consumption",
            "two OS device IDs with independent six-axis values and buttons",
            "hardware flat regions, sign convention, range and nonfinite conversion",
            "focus suppression, disconnect clears one slot, reconnect uses a new device ID",
            "two touchscreen IDs and queued begin/end phases through real GameActivity motion handlers",
            "synthetic ACTION_CANCEL, focus cancellation, actual orientation resize and surface destruction/recreation"])
        if args.touch_only:
            report["checks"] = report["checks"][-2:]
    except Exception as error:
        report["error"] = str(error)
        raise
    finally:
        cleanup_errors = []
        if output:
            (args.output / "instrumentation.log").write_text("".join(output), encoding="utf-8")
        try:
            if instrumentation is not None and instrumentation.poll() is None:
                instrumentation.terminate()
                instrumentation.wait(timeout=10)
        except Exception as error:
            cleanup_errors.append(str(error))
        for device in devices.values():
            try:
                close(device)
            except Exception as error:
                cleanup_errors.append(str(error))
        try:
            command("shell", "am", "force-stop", PACKAGE)
        except Exception as error:
            cleanup_errors.append(str(error))
        if cleanup_errors:
            report.update(result="FAIL", cleanupErrors=cleanup_errors)
        report["elapsedSeconds"] = time.monotonic() - started
        (args.output / "result.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        if cleanup_errors:
            raise RuntimeError("Controller fixture cleanup failed; inspect result.json")


if __name__ == "__main__":
    main()

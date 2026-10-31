"""Verify packaged Android audio with real OS focus interruption and native mixer measurements.

The competing focus receiver is in the input test APK. PCM meters observe the native
output graph; this test does not record a microphone or claim unavailable route coverage.
"""
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
    for name in ("adb", "apk", "focus-apk", "output"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--serial", required=True)
    parser.add_argument("--application-id", required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    report = dict(result="RUNNING", apkSha256=hashlib.sha256(args.apk.read_bytes()).hexdigest(), commands=[], observations=[])
    stay_awake = None
    pid = None
    competitor = "com.kbengine.controllertests"

    def save():
        (args.output / "result.json").write_text(json.dumps(report, indent=2), encoding="utf-8")

    def adb(*parts, binary=False):
        result = subprocess.run([str(args.adb), "-s", args.serial, *map(str, parts)], capture_output=True,
            timeout=120, creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        report["commands"].append(dict(args=list(map(str, parts)), exitCode=result.returncode,
            stderr=result.stderr.decode(errors="replace")))
        if parts[:2] == ("shell", "am") or parts[0] == "install":
            report["commands"][-1]["stdout"] = result.stdout.decode(errors="replace")
        save()
        if result.returncode: raise RuntimeError("Device command failed: " + str(parts) + result.stderr.decode(errors="replace"))
        return result.stdout if binary else result.stdout.decode(errors="replace")

    def focus(operation, gain=2):
        output = adb("shell", "am", "broadcast", "--receiver-foreground", "--include-stopped-packages", "-n", competitor + "/.AudioFocusReceiver",
            "--es", "operation", operation, "--ei", "gain", str(gain))
        if "result=1" not in output: raise RuntimeError("OS focus request failed: " + output)
        return output

    def runtime_log():
        log = adb("logcat", "-d", "--pid=" + pid, "-s", "21kb:I")
        (args.output / ("runtime-" + pid + ".log")).write_text(log, encoding="utf-8")
        if re.search(r"audio platform .* failed|audio focus bridge .*could not|script runtime error|render error kind=", log):
            raise RuntimeError("Runtime reported a failure; inspect its log")
        return log

    def states():
        log = runtime_log()
        return [json.loads(value) for value in re.findall(r"audio-lifecycle (\{[^\r\n]+\})", log)]

    def wait(label, predicate):
        deadline = time.monotonic() + 30
        latest = None
        while time.monotonic() < deadline:
            samples = states()
            if samples:
                latest = samples[-1]
                if predicate(latest):
                    report["observations"].append(dict(check=label, state=latest))
                    save()
                    return latest
            time.sleep(0.25)
        raise RuntimeError(label + ": " + str(latest))

    def launch():
        output = adb("shell", "am", "start", "-W", "-n", args.application_id + "/com.kbengine.runtime.MainActivity")
        if "Error" in output: raise RuntimeError("Activity launch failed: " + output)
        return adb("shell", "pidof", args.application_id).strip()

    def click(number):
        adb("shell", "input", "tap", str(button[0]), str(button[1]))
        return wait("button " + str(number), lambda s: s["clicks"] == number)

    def playing(s):
        return s["available"] and s["playing"] and s["voiceValid"] and s["pausedValid"] and s["rms"] > 0.001

    try:
        report["api"] = adb("shell", "getprop", "ro.build.version.sdk").strip()
        report["deviceKind"] = "emulator" if adb("shell", "getprop", "ro.kernel.qemu").strip() == "1" else "physical"
        stay_awake = adb("shell", "settings", "get", "global", "stay_on_while_plugged_in").strip()
        adb("shell", "svc", "power", "stayon", "usb")
        adb("shell", "input", "keyevent", "KEYCODE_WAKEUP")
        adb("shell", "wm", "dismiss-keyguard")
        adb("install", "-r", "-t", args.focus_apk.resolve())
        adb("install", "-r", args.apk.resolve())
        adb("shell", "am", "force-stop", args.application_id)
        adb("shell", "input", "keyevent", "KEYCODE_WAKEUP")
        adb("shell", "wm", "dismiss-keyguard")
        pid = launch()
        # An empty graph need not have a meter sample before the first sound.
        deadline = time.monotonic() + 30
        while True:
            log = runtime_log()
            if "first-frame=rendered" in log and re.search(r"audio platform resumed[^\r\n]+status=3\b", log): break
            if time.monotonic() >= deadline: raise RuntimeError("Native output did not acquire audio focus")
            time.sleep(0.25)
        screenshot = args.output / "initial.png"
        screenshot.write_bytes(adb("exec-out", "screencap", "-p", binary=True))
        pixels = read_png(screenshot)
        blue = [(x, y) for y in range(0, len(pixels), 2) for x in range(0, len(pixels[0]), 2)
            if pixels[y][x][2] > pixels[y][x][0] + 50 and pixels[y][x][2] > pixels[y][x][1] + 50]
        if not blue: raise RuntimeError("Audio transport button is missing")
        button = ((min(x for x, _ in blue) + max(x for x, _ in blue)) // 2,
            (min(y for _, y in blue) + max(y for _, y in blue)) // 2)
        report["routes"] = focus("routes")
        click(1)
        baseline = wait("native output after touch", playing)
        if baseline["backend"] == "Null": raise RuntimeError("A null backend is not physical output")
        for gain in (2, 3):
            focus("request", gain)
            suspended = wait("OS focus suspension " + str(gain), lambda s: s["status"] == 4 and not s["available"])
            frozen = wait("output remains suspended " + str(gain), lambda s:
                s["status"] == 4 and s["frame"] > suspended["frame"] + 8)
            if any(frozen[k] != suspended[k] for k in ("deviceFrames", "cursor", "pausedCursor", "generation")):
                raise RuntimeError("Output advanced while OS audio focus was lost")
            focus("release")
            resumed = wait("OS focus restoration " + str(gain), lambda s: playing(s) and s["deviceFrames"] > frozen["deviceFrames"])
            if resumed["generation"] != baseline["generation"] or abs(resumed["rms"] / baseline["rms"] - 1) > 0.1:
                raise RuntimeError("Focus restoration replaced or duplicated output")
            if resumed["pausedCursor"] != baseline["pausedCursor"]:
                raise RuntimeError("Focus restoration resumed a user-paused voice")
        for paused in (False, True):
            if paused: click(2)
            before = states()[-1]
            prior_suspends = adb("logcat", "-d", "--pid=" + pid, "-s", "21kb:I").count("audio platform suspended")
            adb("shell", "input", "keyevent", "KEYCODE_HOME")
            time.sleep(1.2)
            log = adb("logcat", "-d", "--pid=" + pid, "-s", "21kb:I")
            if log.count("audio platform suspended") <= prior_suspends: raise RuntimeError("Backgrounding did not suspend native output")
            if launch() != pid: raise RuntimeError("Audio background cycle restarted the process")
            resumed = wait("background restore paused=" + str(paused), lambda s: s["available"] and
                s["deviceFrames"] > before["deviceFrames"] and s["playing"] == (not paused))
            if resumed["generation"] != baseline["generation"] or resumed["pausedCursor"] != baseline["pausedCursor"]:
                raise RuntimeError("Backgrounding reset the graph or user pause state")
            if paused:
                wait("user-paused source stays silent", lambda s: s["available"] and not s["playing"] and s["peak"] < 0.00001)
            elif abs(resumed["rms"] / baseline["rms"] - 1) > 0.1:
                raise RuntimeError("Background restoration changed output amplitude")
        click(3)
        restored = wait("explicit source resume", playing)
        if abs(restored["rms"] / baseline["rms"] - 1) > 0.1 or restored["shortBlocks"] != 0:
            raise RuntimeError("Restored native output duplicated or produced incomplete blocks")
        report["result"] = "PASS"
        report["limitations"] = ["Native mixer PCM and OS device callbacks; no microphone recording.",
            "Route inventory only; absent wired or wireless routes are not qualified."]
    except BaseException as error:
        report.update(result="FAIL", error=str(error))
        raise
    finally:
        try:
            try:
                focus("release")
            finally:
                try:
                    adb("shell", "am", "force-stop", args.application_id)
                finally:
                    if stay_awake is not None:
                        if stay_awake == "null": adb("shell", "settings", "delete", "global", "stay_on_while_plugged_in")
                        else: adb("shell", "settings", "put", "global", "stay_on_while_plugged_in", stay_awake)
        except BaseException as error:
            report.update(result="FAIL", cleanupError=str(error))
            raise
        finally:
            save()


if __name__ == "__main__":
    main()

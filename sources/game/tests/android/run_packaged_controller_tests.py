"""Exercise OS controller events through a production packaged Android player.

The selected host must permit shell access to /dev/uinput. Events are emitted
only while the selected test application owns focus; each owned device is
removed on EOF. This verifies OS virtual devices, not physical controller models.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import time

FIELDS = ("LX", "LY", "RX", "RY", "LT", "RT", "A", "B", "Up", "Down", "Left", "Right")
HIDDEN = getattr(subprocess, "CREATE_NO_WINDOW", 0)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("adb", "apk", "helper", "output"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--serial", required=True)
    parser.add_argument("--application-id", required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    prefix = [str(args.adb), "-s", args.serial]
    remote = "/data/local/tmp/kb-controller-fixture-" + hashlib.sha256(args.helper.read_bytes()).hexdigest()[:12]
    report = dict(result="FAIL", observations=[], devices=[],
        inputSource="OS uinput devices and kernel events on the selected Android host",
        apkSha256=hashlib.sha256(args.apk.read_bytes()).hexdigest())
    devices = {}
    pid = None
    stay_awake = None
    originally_asleep = False

    def adb(*parts, binary=False):
        result = subprocess.run(prefix + list(map(str, parts)), capture_output=True,
            timeout=60, creationflags=HIDDEN)
        if result.returncode:
            raise RuntimeError("Device command failed (" + " ".join(map(str, parts)) + "): "
                + result.stdout.decode(errors="replace") + result.stderr.decode(errors="replace"))
        return result.stdout if binary else result.stdout.decode("utf-8", errors="replace")

    def focused():
        windows = adb("shell", "dumpsys", "window")
        return any(args.application_id in line and "mCurrentFocus=" in line for line in windows.splitlines())

    def logs():
        text = adb("logcat", "-d", "--pid=" + pid, "-s", "21kb:I")
        (args.output / "runtime.log").write_text(text, encoding="utf-8")
        if "render error kind=" in text or "renderer initialization failed" in text:
            raise RuntimeError("The packaged player reported a rendering error")
        return text

    def wait(label, predicate):
        deadline = time.monotonic() + 25
        state = None
        while time.monotonic() < deadline:
            matches = re.findall(r"controller-flow (\{[^\r\n]+\})", logs())
            if matches:
                state = json.loads(matches[-1])
                if state["ready"] and predicate(state):
                    report["observations"].append(dict(check=label, state=state))
                    return state
            time.sleep(.15)
        raise RuntimeError(label + ": " + str(state))

    def connected(state):
        return [slot for slot, pad in enumerate(state["pads"]) if pad["connected"]]

    def values(state, slot, expected):
        return all(abs(state["pads"][slot][key] - value) < .025 for key, value in expected.items())

    def neutral(state, slot):
        return values(state, slot, dict.fromkeys(FIELDS, 0))

    def register(label):
        if not focused():
            raise RuntimeError("The packaged test player must own focus before controller registration")
        log = (args.output / ("device-" + label + "-stderr.log")).open("ab")
        process = subprocess.Popen(prefix + ["shell", remote, label], stdin=subprocess.PIPE,
            stdout=subprocess.DEVNULL, stderr=log, creationflags=HIDDEN)
        devices[label] = (process, log)
        deadline = time.monotonic() + 15
        while time.monotonic() < deadline:
            if process.poll() is not None:
                raise RuntimeError("Controller fixture exited before registration")
            inventory = adb("shell", "dumpsys", "input")
            match = re.search(r"Device (\d+): Engine Controller Test " + label + r"\b", inventory)
            if match:
                identity = int(match[1])
                report["devices"].append(dict(label=label, deviceId=identity))
                (args.output / ("input-" + label + "-" + str(identity) + ".txt")).write_text(inventory, encoding="utf-8")
                return identity
            time.sleep(.1)
        raise RuntimeError("OS did not register controller " + label)

    def close(label):
        process, log = devices.pop(label)
        try:
            process.stdin.close()
            process.wait(timeout=10)
            if process.returncode:
                raise RuntimeError("Controller fixture exited with an error")
        finally:
            log.close()

    def send(label, axes, buttons=None):
        if not focused():
            raise RuntimeError("Refusing controller events while another application owns focus")
        events = [(3, code, value) for code, value in axes.items()]
        events += [(1, code, value) for code, value in (buttons or {}).items()]
        events.append((0, 0, 0))
        stream = devices[label][0].stdin
        stream.write("".join(f"{kind} {code} {value}\n" for kind, code, value in events).encode())
        stream.flush()

    try:
        report["deviceKind"] = "emulator" if adb("shell", "getprop", "ro.kernel.qemu").strip() == "1" else "physical"
        stay_awake = adb("shell", "settings", "get", "global", "stay_on_while_plugged_in").strip()
        power = adb("shell", "dumpsys", "power")
        originally_asleep = "mWakefulness=Asleep" in power or "mWakefulness=Dozing" in power
        adb("shell", "input", "keyevent", "KEYCODE_WAKEUP")
        adb("shell", "wm", "dismiss-keyguard")
        adb("shell", "svc", "power", "stayon", "usb")
        adb("install", "-r", args.apk.resolve())
        adb("shell", "am", "force-stop", args.application_id)
        adb("shell", "am", "start", "-W", "-n", args.application_id + "/com.kbengine.runtime.MainActivity")
        deadline = time.monotonic() + 15
        while time.monotonic() < deadline:
            process = subprocess.run(prefix + ["shell", "pidof", args.application_id],
                capture_output=True, timeout=10, creationflags=HIDDEN)
            if process.returncode == 0 and process.stdout.strip():
                pid = process.stdout.decode("ascii").strip().split()[0]
                break
            time.sleep(.1)
        if pid is None:
            raise RuntimeError("The packaged player did not start its process")
        wait("initial disconnected controllers", lambda s: s["focus"] and not connected(s))
        if "first-frame=rendered" not in logs():
            raise RuntimeError("The packaged player did not render its first frame")
        (args.output / "initial.png").write_bytes(adb("exec-out", "screencap", "-p", binary=True))
        adb("push", args.helper.resolve(), remote)
        adb("shell", "chmod", "700", remote)
        first_id = register("A")
        state = wait("first OS controller connected", lambda s: len(connected(s)) == 1)
        first = connected(state)[0]
        second_id = register("B")
        if first_id == second_id:
            raise RuntimeError("Controllers must have distinct OS device IDs")
        state = wait("two OS controllers connected", lambda s: len(connected(s)) == 2)
        second = next(slot for slot in connected(state) if slot != first)
        axes_a = {0: 16384, 1: -24576, 2: -8192, 5: 12288, 10: 153, 9: 204, 16: 1, 17: -1}
        expected_a = dict(LX=.5, LY=.75, RX=-.25, RY=-.375, LT=.6, RT=.8, A=1, Right=1, Up=1)
        send("A", axes_a, {304: 1})
        wait("six axes, button and hat reach first slot", lambda s: values(s, first, expected_a) and neutral(s, second))
        expected_b = dict(LX=-.75, LY=-.5, RX=.625, RY=.25, LT=.2, RT=.4, B=1, Left=1, Down=1)
        send("B", {0: -24576, 1: 16384, 2: 20480, 5: -8192, 10: 51, 9: 102, 16: -1, 17: 1}, {305: 1})
        wait("second controller preserves first controller values", lambda s: values(s, first, expected_a) and values(s, second, expected_b))
        send("A", {0: 1024, 1: -1024, 2: 1024, 5: -1024, 10: 0, 9: 0, 16: 0, 17: 0}, {304: 0})
        wait("hardware flat region produces neutral axes", lambda s: neutral(s, first) and values(s, second, expected_b))
        send("A", axes_a, {304: 1})
        wait("held controls before disconnection", lambda s: values(s, first, expected_a))
        close("A")
        wait("disconnect releases only the removed controller", lambda s: connected(s) == [second] and neutral(s, first) and values(s, second, expected_b))
        new_id = register("A")
        if new_id in (first_id, second_id):
            raise RuntimeError("Reconnected controller did not receive a new OS device ID")
        state = wait("reconnect preserves the other controller slot", lambda s: len(connected(s)) == 2 and values(s, second, expected_b))
        restored = next(slot for slot in connected(state) if slot != second)
        if not neutral(state, restored):
            raise RuntimeError("Reconnected controller inherited held controls")
        send("A", axes_a, {304: 1})
        wait("reconnected controller receives new events", lambda s: values(s, restored, expected_a) and values(s, second, expected_b))
        close("A"); close("B")
        wait("all owned controllers removed and released", lambda s: not connected(s) and all(neutral(s, slot) for slot in range(4)))
        report["result"] = "PASS"
    except Exception as error:
        report["error"] = str(error)
    finally:
        cleanup = []
        for label in list(devices):
            try: close(label)
            except Exception as error: cleanup.append(str(error))
        try:
            adb("shell", "rm", "-f", remote)
            if stay_awake is not None:
                adb("shell", "settings", "put", "global", "stay_on_while_plugged_in", stay_awake)
            if originally_asleep:
                adb("shell", "input", "keyevent", "KEYCODE_SLEEP")
        except Exception as error: cleanup.append(str(error))
        if cleanup:
            report.update(result="FAIL", cleanupErrors=cleanup)
        (args.output / "result.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps(dict(result=report["result"], error=report.get("error"), checks=len(report["observations"]))))
    return 0 if report["result"] == "PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())

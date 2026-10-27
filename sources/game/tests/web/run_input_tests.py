"""Verify the production Web touch collector in a real headless browser/WASM module.

Requires websocket-client. Uses trusted DevTools touch input on its own local test
page; no existing browser profiles, remote sites or other applications are used.
"""
from __future__ import annotations

import argparse
import http.server
import json
import re
import socket
import subprocess
import threading
import time
import urllib.request
import uuid
from pathlib import Path

import websocket


class DevTools:
    def __init__(self, url: str):
        self.socket = websocket.create_connection(url, timeout=10, suppress_origin=True)
        self.next_id = 0
        self.events: list[dict] = []

    def call(self, method: str, **parameters):
        self.next_id += 1
        self.socket.send(json.dumps(dict(id=self.next_id, method=method, params=parameters)))
        while True:
            response = json.loads(self.socket.recv())
            if response.get("id") == self.next_id:
                if "error" in response:
                    raise RuntimeError(f"{method}: {response['error']}")
                return response.get("result", {})
            self.events.append(response)

    def evaluate(self, expression: str):
        result = self.call("Runtime.evaluate", expression=expression, returnByValue=True)
        if "exceptionDetails" in result:
            raise RuntimeError(str(result["exceptionDetails"]))
        return result["result"].get("value")

    def close(self):
        self.socket.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--browser", type=Path, required=True)
    parser.add_argument("--directory", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    root = args.directory.resolve(strict=True)
    if not (root / "InputTests.html").is_file() or not (root / "InputTests.wasm").is_file():
        raise RuntimeError("Build the sanitized Web input test target first")
    args.output.mkdir(parents=True, exist_ok=True)

    class Quiet(http.server.SimpleHTTPRequestHandler):
        def log_message(self, *_args):
            pass

    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), lambda *values: Quiet(*values, directory=str(root)))
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    with socket.socket() as reservation:
        reservation.bind(("127.0.0.1", 0))
        port = reservation.getsockname()[1]
    url = f"http://127.0.0.1:{server.server_port}/InputTests.html?test={uuid.uuid4().hex}"
    client = other = process = None
    observations = []
    report = dict(schema="21kb.web-input-tests/v1", result="FAIL")
    started = time.monotonic()

    def endpoint(target_id=None):
        with urllib.request.urlopen(f"http://127.0.0.1:{port}/json/list", timeout=2) as response:
            targets = json.load(response)
        if target_id is not None:
            return next((target for target in targets if target.get("id") == target_id), None)
        return next((target for target in targets if target.get("url", "").startswith(url)), None)

    def snapshot():
        return client.evaluate("JSON.parse(Module.UTF8ToString(Module._kb_input_snapshot()))")

    def gamepads(label, expected):
        value = client.evaluate("JSON.parse(Module.UTF8ToString(Module._kb_gamepad_snapshot()))")
        assert value == expected, (label, value, expected)
        observations.append(dict(check=label, state=value, inputSource="synthetic browser Gamepad samples"))

    def expect(label, points, *, focus=1, viewport=None, pointer=None):
        until = time.monotonic() + 8
        while True:
            value = snapshot()
            if (value["points"] == points and value["focus"] == focus and
                    (viewport is None or value["viewport"] == viewport) and
                    (pointer is None or value["pointer"] == pointer)):
                assert value["touchDown"] == any(point[1] in (0, 1) for point in points), value
                observations.append(dict(check=label, state=value))
                return value
            if time.monotonic() >= until:
                raise AssertionError(f"{label}: {value}; expected {points}")
            time.sleep(0.02)

    def touch(kind, points):
        client.call("Input.dispatchTouchEvent", type=kind, touchPoints=[
            dict(id=identity, x=x, y=y, radiusX=1, radiusY=1, force=1)
            for identity, x, y in points])

    try:
        with (args.output / "browser.log").open("wb") as log:
            process = subprocess.Popen([str(args.browser.resolve(strict=True)), "--headless=new",
                "--disable-extensions", "--disable-background-networking", "--disable-component-update",
                "--disable-default-apps", "--no-first-run", "--no-default-browser-check",
                f"--remote-debugging-port={port}", f"--user-data-dir={args.output.resolve() / 'browser-profile'}", url],
                stdout=log, stderr=subprocess.STDOUT, creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
            until = time.monotonic() + 30
            target = None
            while time.monotonic() < until:
                if process.poll() is not None:
                    raise RuntimeError("Own headless browser exited before loading the test page")
                try:
                    target = endpoint()
                    if target:
                        break
                except (OSError, ValueError):
                    pass
                time.sleep(0.1)
            if not target:
                raise RuntimeError("Own browser test target did not load")
            client = DevTools(target["webSocketDebuggerUrl"])
            client.call("Runtime.enable")
            client.call("Page.enable")
            client.call("Page.bringToFront")
            client.call("Emulation.setTouchEmulationEnabled", enabled=True, maxTouchPoints=10)
            client.call("Emulation.setDeviceMetricsOverride", width=960, height=720, deviceScaleFactor=2, mobile=True)
            until = time.monotonic() + 30
            while not client.evaluate("document.documentElement?.dataset.kbInputReady === 'true'"):
                if time.monotonic() >= until:
                    raise RuntimeError("Sanitized WebAssembly module did not initialize")
                time.sleep(0.05)
            report["browser"] = client.call("Browser.getVersion")["product"]
            initial = expect("initial", [], viewport=[800, 600])
            assert initial["asan"] == 1 and initial["ubsan"] == 1, "The native test module must have both sanitizers enabled"
            assert client.evaluate("Module._kb_gamepad_invalid()") == 1
            transitions = client.evaluate("Module._kb_digital_transitions()")
            assert transitions == 0, "Native digital transition check failed: " + str(transitions)
            observations.append(dict(check="queued mouse/keyboard transitions, repeat, pointer, cancel and overflow",
                inputSource="native production collector", result=transitions))
            client.evaluate("""
                window.kbOriginalGetGamepads = navigator.getGamepads;
                window.kbVirtualPads = [];
                window.kbMakePad = (index,axes,pressed,triggers) => ({
                    id:'Standard test controller',index,connected:true,mapping:'standard',
                    timestamp:performance.now(),axes,
                    buttons:Array.from({length:17},(_,i)=>({pressed:pressed.includes(i),
                        value:i===6?triggers[0]:i===7?triggers[1]:pressed.includes(i)?1:0}))
                });
                navigator.getGamepads = () => kbVirtualPads;
            """)
            neutral = [0] * 9
            first_pad = [1, 1, 0, 0.25, 0.5, -0.75, -1, 0.125, 0.875]
            last_pad = [1, 0, 1, -1, -0.25, 0.5, 0.75, 1, 0]
            gamepads("no browser controllers", [neutral] * 4)
            client.evaluate("kbVirtualPads=[kbMakePad(0,[.25,-.5,-.75,1],[0],[.125,.875]),"
                "null,null,kbMakePad(3,[-1,.25,.5,-.75],[1],[1,0])]")
            gamepads("sparse independent browser controller slots", [first_pad, neutral, neutral, last_pad])
            client.evaluate("kbVirtualPads[0]=null")
            gamepads("controller disconnect releases only its slot", [neutral, neutral, neutral, last_pad])
            client.evaluate("kbVirtualPads[0]=kbMakePad(0,[.25,-.5,-.75,1],[0],[.125,.875])")
            gamepads("controller reconnect restores its slot", [first_pad, neutral, neutral, last_pad])
            client.evaluate("kbVirtualPads[0].mapping=''")
            gamepads("unmapped controller does not guess its layout", [[1] + [0] * 8, neutral, neutral, last_pad])
            client.evaluate("kbVirtualPads[0].mapping='standard'")
            for kind, buttons in [("mousePressed", 1), ("mouseReleased", 0)]:
                client.call("Input.dispatchMouseEvent", type=kind, x=140, y=150,
                            button="left", buttons=buttons, clickCount=1)
            assert snapshot()["mouseEvents"] == 2, "Mouse positive control did not reach the native queue"
            touch("touchStart", [(7, 140, 150)])
            expect("first finger", [[7, 0, 200, 200]])
            touch("touchMove", [(7, 160, 170)])
            expect("queued move retains begin", [[7, 0, 240, 240]])
            touch("touchStart", [(7, 160, 170), (23, 340, 150)])
            expect("two independent fingers", [[7, 0, 240, 240], [23, 0, 600, 200]])
            assert client.evaluate("Module._kb_input_invalid()") == 1
            client.evaluate("Module._kb_input_finish()")
            expect("consumed begins", [[7, 1, 240, 240], [23, 1, 600, 200]])
            touch("touchEnd", [(7, 160, 170)])
            touch("touchMove", [(23, 350, 200)])
            expect("release survives another finger move", [[7, 2, 240, 240], [23, 1, 620, 300]], pointer=[620, 300])
            client.evaluate("Module._kb_input_finish()")
            expect("consumed release", [[23, 1, 620, 300]])
            touch("touchCancel", [])
            expect("browser cancel", [[23, 3, 620, 300]])
            client.evaluate("Module._kb_input_finish()")
            expect("consumed cancellation", [])
            assert snapshot()["mouseEvents"] == 2 and snapshot()["mouseLeft"] == 0, "Touch became emulated mouse input"
            touch("touchStart", [(7, 140, 150)])
            expect("focus fixture down", [[7, 0, 200, 200]])
            second_id = client.call("Target.createTarget", url="about:blank", background=False)["targetId"]
            other = DevTools(endpoint(second_id)["webSocketDebuggerUrl"])
            other.call("Page.bringToFront")
            expect("actual tab focus loss", [[7, 3, 200, 200]], focus=0)
            gamepads("focus loss releases browser controller input", [neutral] * 4)
            client.evaluate("Module._kb_input_finish()")
            expect("unfocused release consumed", [], focus=0)
            client.call("Page.bringToFront")
            expect("actual tab focus restoration", [])
            gamepads("focus restoration samples controllers again", [first_pad, neutral, neutral, last_pad])
            client.evaluate("navigator.getGamepads=kbOriginalGetGamepads")
            client.call("Target.closeTarget", targetId=second_id)
            other.close()
            other = None
            touch("touchCancel", [])
            touch("touchStart", [(7, 140, 150)])
            expect("resize fixture down", [[7, 0, 200, 200]])
            client.evaluate("document.getElementById('canvas').width=1200; Module._kb_input_resize()")
            expect("resized viewport cancels old contact", [[7, 3, 200, 200]], viewport=[1200, 600])
            client.evaluate("Module._kb_input_finish()")
            touch("touchCancel", [])
            touch("touchStart", [(7, 140, 150)])
            expect("independent CSS/backbuffer scales", [[7, 0, 300, 200]], viewport=[1200, 600])
            touch("touchStart", [(7, 140, 150), (23, 340, 150)])
            expect("two contacts before cancellation", [[7, 0, 300, 200], [23, 0, 900, 200]])
            touch("touchCancel", [])
            expect("browser cancels both contacts", [[7, 3, 300, 200], [23, 3, 900, 200]])
            client.evaluate("Module._kb_input_finish()")
            expect("both cancellations consumed", [])
            touch("touchStart", [(7, 140, 150)])
            before = expect("fresh contact after cancellation", [[7, 0, 300, 200]])
            client.evaluate("document.getElementById('text').focus(); document.getElementById('text').blur()")
            after = expect("child text focus is not window blur", [[7, 0, 300, 200]])
            assert before["focusChanges"] == after["focusChanges"], "Child focus reached window focus handler"
            assert after["mouseEvents"] == 2 and after["mouseLeft"] == 0
            client.evaluate("Module._kb_input_detach()")
            detached = expect("handler detachment cancels contacts", [[7, 3, 300, 200]])
            assert detached["attached"] == 0
            assert client.evaluate("document.getElementById('canvas').style.touchAction") == "pan-x"
            touch("touchCancel", [])
            touch("touchStart", [(23, 340, 150)])
            assert snapshot()["begins"] == detached["begins"], "Detached native callback was invoked"
            messages = json.dumps(client.events)
            assert not re.search(r"ERROR: AddressSanitizer|runtime error:|SUMMARY: .*Sanitizer|Aborted", messages), messages
            assert not any(event.get("method") == "Runtime.exceptionThrown" for event in client.events), messages
            dom_events = client.evaluate("window.kbTouchEvents")
            assert dom_events and all(event["trusted"] for event in dom_events), dom_events
            assert any(event["type"] == "touchend" and event["changed"] == [7] for event in dom_events), dom_events
            report.update(result="PASS", trustedTouchEvents=True, sanitizedWebAssembly=True,
                          gamepadSamples="synthetic through the production Emscripten polling bridge; no physical controller claim",
                          touchPhases=dict(began=0, moved=1, ended=2, cancelled=3),
                          mousePositiveControl=True, emulatedMouseEvents=0, observations=observations, domTouchEvents=dom_events)
    except Exception as error:
        report.update(error=str(error), observations=observations)
        raise
    finally:
        cleanup_errors = []
        if client:
            try:
                (args.output / "browser-events.json").write_text(json.dumps(client.events, indent=2), encoding="utf-8")
            except Exception as error:
                cleanup_errors.append(str(error))
            try:
                client.call("Browser.close")
            except Exception:
                pass
            try:
                client.close()
            except Exception as error:
                cleanup_errors.append(str(error))
        if other:
            try:
                other.close()
            except Exception as error:
                cleanup_errors.append(str(error))
        if process:
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                try:
                    process.terminate()
                    process.wait(timeout=5)
                except Exception as error:
                    cleanup_errors.append(str(error))
            if process.returncode not in (None, 0):
                cleanup_errors.append(f"Own browser exited with code {process.returncode}")
        try:
            server.shutdown()
            server.server_close()
            thread.join(timeout=5)
        except Exception as error:
            cleanup_errors.append(str(error))
        if cleanup_errors:
            report.update(result="FAIL", cleanupErrors=cleanup_errors)
        report["elapsedSeconds"] = round(time.monotonic() - started, 3)
        (args.output / "result.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        if cleanup_errors:
            raise RuntimeError("Browser fixture cleanup failed; inspect result.json")


if __name__ == "__main__":
    main()

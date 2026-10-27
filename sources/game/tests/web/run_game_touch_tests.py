"""Author, cook and verify independent gameplay/UI touch input in a real player.

Uses a fresh headless browser profile, trusted DOM input and actual GPU captures.
Requires the test transport in requirements.txt and a hardware WebGL2 renderer.
"""
from __future__ import annotations

import argparse
import base64
import ctypes
import hashlib
import http.server
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import threading
import time
import traceback
import urllib.request

from run_packaged_runtime_tests import RuntimeClient, require

sys.path.insert(0, str(Path(__file__).resolve().parents[4] / "scripts"))
from png_readback import read_png

SOFTWARE_RENDERERS = ["swiftshader", "llvmpipe", "softpipe", "microsoft basic", "warp", "software"]
GRAPHICS_MARKER = "21kb-browser-graphics "


def wait_page_focus(client, focused):
    deadline = time.monotonic() + 10
    while True:
        state = client.evaluate("({focused:document.hasFocus(),visibility:document.visibilityState})")
        if state["focused"] == focused and state["visibility"] == ("visible" if focused else "hidden"):
            return
        require(time.monotonic() < deadline, "Actual browser tab focus did not settle: " + str(state))
        time.sleep(0.025)


def graphics_contexts(events):
    contexts = []
    for event in events:
        if event.get("method") != "Runtime.consoleAPICalled":
            continue
        for argument in event["params"].get("args", []):
            value = argument.get("value")
            if not isinstance(value, str) or not value.startswith(GRAPHICS_MARKER):
                continue
            try:
                context = json.loads(value[len(GRAPHICS_MARKER):])
            except json.JSONDecodeError:
                context = None
            if not isinstance(context, dict):
                contexts.append(dict(diagnostic="Invalid graphics context observation", hardwareIdentified=False))
                continue
            renderer = context.get("renderer")
            context["hardwareIdentified"] = bool(context.get("rendererAvailable") and
                isinstance(renderer, str) and renderer and not any(
                    word in renderer.lower() for word in SOFTWARE_RENDERERS))
            contexts.append(context)
    return contexts


def author_fixture(args, root):
    project = root / "Sample"
    web = root / "Web"
    web.mkdir()
    commands = []

    def run(executable, *tail):
        command = list(map(str, [executable, *tail]))
        result = subprocess.run(command, cwd=root, capture_output=True, text=True,
            encoding="utf-8", errors="replace", timeout=180,
            creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        commands.append(dict(command=command, exitCode=result.returncode,
            stdout=result.stdout, stderr=result.stderr))
        (root / "commands.json").write_text(json.dumps(commands, indent=2), encoding="utf-8")
        require(result.returncode == 0, "Authoring/cook failed: " + str(commands[-1]))

    def entity(name, parent=None):
        tail = ["entity", "add", "--project", project, "--name", name]
        if parent:
            tail += ["--parent", parent]
        run(args.cli, *tail)

    def component(name, kind, *settings):
        tail = ["component", "add", "--project", project, "--entity", name, "--type", kind]
        for setting in settings:
            tail += ["--set", setting]
        run(args.cli, *tail)

    def color(red, green, blue):
        return [f"backgroundColor.{channel}={value}"
            for channel, value in zip("rgba", [red, green, blue, 1])]

    run(args.cli, "new", "--parent", root, "--name", "Sample", "--template", "Empty")
    run(args.cli, "entity", "add", "--project", project, "--create", "Camera", "--name", "MainCamera")
    entity("CanvasRoot")
    component("CanvasRoot", "Rect Transform", "anchorMax=1,1", "offsetMax=0,0")
    component("CanvasRoot", "Canvas")
    component("CanvasRoot", "Canvas Scaler", "scaleMode=0", "scaleFactor=1")
    entity("JoystickBase", "CanvasRoot")
    component("CanvasRoot/JoystickBase", "Rect Transform", "offsetMin=40,80", "offsetMax=240,280")
    component("CanvasRoot/JoystickBase", "Border", *color(0.08, 0.1, 0.16))
    component("CanvasRoot/JoystickBase", "Canvas Group", "blocksRaycasts=false")
    entity("JoystickKnob", "CanvasRoot/JoystickBase")
    component("CanvasRoot/JoystickBase/JoystickKnob", "Rect Transform",
        "offsetMin=85,85", "offsetMax=115,115")
    component("CanvasRoot/JoystickBase/JoystickKnob", "Border", *color(0.0, 0.8, 0.2))
    entity("ActionButton", "CanvasRoot")
    component("CanvasRoot/ActionButton", "Rect Transform", "anchorMax=1,0",
        "anchorMin=1,0", "offsetMin=-220,80", "offsetMax=-60,240")
    component("CanvasRoot/ActionButton", "Border", *color(0.1, 0.2, 0.8))
    component("CanvasRoot/ActionButton", "Selectable", "colorFadeSeconds=0")
    component("CanvasRoot/ActionButton", "Button")
    run(args.cli, "script-new", "--project", project, "--name", "TouchFlow", "--language", "lua")
    shutil.copy2(Path(__file__).with_name("fixtures") / "TouchFlow.lua",
        project / "Assets/Scripts/TouchFlow.lua")
    run(args.cli, "scene-attach", "--project", project, "--scene", "Assets/Scenes/Main.21kbscene",
        "--node", "ActionButton", "--script", "/Game/Scripts/TouchFlow.lua")
    run(args.cooker, "--project", project, "--target", "WebGL.wasm32",
        "--output", web / "Game.kbpack", "--engine-root", args.engine_root,
        "--shaderc", args.shaderc, "--cache", root / "CookCache")
    for name in ["Game.html", "Game.js", "Game.wasm"]:
        shutil.copy2(args.directory / name, web / name)
    return web


OBSERVER = """
window.kbTouchEvents = [];
window.kbMouseEvents = [];
window.kbTouchGraphicsLost = [];
for (const kind of ['touchstart','touchmove','touchend','touchcancel']) {
    document.addEventListener(kind, e => kbTouchEvents.push({
        type:e.type, trusted:e.isTrusted,
        changed:Array.from(e.changedTouches, point => point.identifier)
    }), true);
}
for (const kind of ['mousedown','mouseup','mousemove','click']) {
    document.addEventListener(kind, e => kbMouseEvents.push({
        type:e.type, trusted:e.isTrusted
    }), true);
}
document.addEventListener('webglcontextlost', e => kbTouchGraphicsLost.push({
    type:e.type, message:e.statusMessage
}), true);
const compile = WebGL2RenderingContext.prototype.compileShader;
WebGL2RenderingContext.prototype.compileShader = function(shader) {
    if (!window.kbTouchGraphics || window.kbTouchGraphics.context !== this) {
        const ext = this.getExtension('WEBGL_debug_renderer_info');
        window.kbTouchGraphics = {
            context:this, version:this.getParameter(this.VERSION),
            rendererAvailable:!!ext,
            renderer:ext ? this.getParameter(ext.UNMASKED_RENDERER_WEBGL) : this.getParameter(this.RENDERER),
            shaders:[]
        };
        console.info('21kb-browser-graphics ' + JSON.stringify({
            phase:'first-shader',timeMs:performance.now(),
            version:kbTouchGraphics.version,renderer:kbTouchGraphics.renderer,
            rendererAvailable:kbTouchGraphics.rendererAvailable
        }));
    }
    compile.call(this, shader);
    window.kbTouchGraphics.shaders.push({
        compiled:this.getShaderParameter(shader, this.COMPILE_STATUS),
        message:this.getShaderInfoLog(shader)
    });
};
"""


class TouchScenario:
    marker = "touch-flow"

    def __init__(self, client, root, report):
        self.client = client
        self.root = root
        self.report = report
        self.layout = {}

    def states(self):
        state = self.client.evaluate("({data:{...document.documentElement?.dataset},"
            "output:document.querySelector('#output')?.value})")
        require(not state["data"].get("kbError"), "The actual player failed: " + str(state))
        errors = [event["params"]["exceptionDetails"] for event in self.client.events
            if event.get("method") == "Runtime.exceptionThrown"]
        require(not errors, "Unhandled browser/runtime exception: " + str(errors))
        require(not self.client.evaluate("window.kbTouchGraphicsLost || []"),
            "The player lost its real graphics context")
        marker = "kb_game: script: " + self.marker + " "
        return [json.loads(line[len(marker):])
            for line in (state.get("output") or "").splitlines()
            if line.startswith(marker)]

    def wait(self, label, predicate, after=0):
        deadline = time.monotonic() + 15
        while True:
            values = self.states()
            matches = [value for value in values[after:] if predicate(value)]
            if matches:
                result = matches[-1]
                self.report["observations"].append(dict(check=label, state=result))
                return result
            require(time.monotonic() < deadline, label + ": " + str(values[-6:]))
            time.sleep(0.03)

    def geometry(self):
        deadline = time.monotonic() + 10
        while True:
            self.states()
            self.layout = self.client.evaluate("(() => {const c = document.querySelector('#canvas');"
                "const r = c.getBoundingClientRect();return {left:r.left,top:r.top,cssWidth:r.width,"
                "cssHeight:r.height,width:c.width,height:c.height,dpr:devicePixelRatio};})()")
            value = self.layout
            if (value["width"] == round(value["cssWidth"] * value["dpr"]) and
                    value["height"] == round(value["cssHeight"] * value["dpr"])):
                return value
            require(time.monotonic() < deadline, "The real backbuffer did not resize: " + str(value))
            time.sleep(0.05)

    def touch(self, kind, points):
        layout = self.layout
        self.client.call("Input.dispatchTouchEvent", type=kind, touchPoints=[
            dict(id=identity,
                x=layout["left"] + x * layout["cssWidth"] / layout["width"],
                y=layout["top"] + y * layout["cssHeight"] / layout["height"],
                radiusX=1, radiusY=1, force=1) for identity, x, y in points])

    @staticmethod
    def point(value, identity, active=True):
        return next((point for point in value["points"]
            if point["id"] == identity and point["active"] == active), None)

    def paint(self):
        self.client.call("Runtime.evaluate", expression="new Promise(resolve => requestAnimationFrame("
            "() => requestAnimationFrame(() => resolve(true))))", awaitPromise=True)

    def graphics(self):
        value = self.client.evaluate("(() => {const g = window.kbTouchGraphics;"
            "return g ? {version:g.version,renderer:g.renderer,rendererAvailable:g.rendererAvailable,"
            "lost:g.context.isContextLost(),shaders:g.shaders} : null;})()")
        require(value and not value["lost"] and "WebGL 2.0" in value["version"],
            "The actual WebGL2 context is missing: " + str(value))
        require(value["rendererAvailable"] and not any(word in value["renderer"].lower()
            for word in SOFTWARE_RENDERERS),
            "This test requires an identifiable hardware GPU: " + str(value))
        require(value["shaders"] and all(shader["compiled"] for shader in value["shaders"]),
            "Actual host shader compilation failed: " + str(value))
        return value

    def picture(self, label, locations):
        self.states()
        self.paint()
        self.geometry()
        self.graphics()
        layout = self.layout
        data = self.client.call("Page.captureScreenshot", format="png",
            clip=dict(x=layout["left"], y=layout["top"], width=layout["cssWidth"],
                height=layout["cssHeight"], scale=1), captureBeyondViewport=True)["data"]
        path = self.root / (label + ".png")
        path.write_bytes(base64.b64decode(data))
        rows = read_png(path)
        require(len(rows) == layout["height"] and rows and len(rows[0]) == layout["width"],
            "The captured GPU image does not match the actual backbuffer")
        pixels = [list(rows[y][x]) for x, y in locations]
        self.report["images"].append(dict(check=label, width=layout["width"],
            height=layout["height"], pixels=pixels))
        self.states()
        self.graphics()
        return pixels

    def run(self, targets):
        client = self.client
        self.wait("runtime initialized", lambda value: value["button"] != 0)
        client.evaluate("Object.assign(document.querySelector('#canvas').style,"
            "{width:'800px',height:'600px',margin:'0',display:'block'})")
        self.paint()
        self.geometry()
        initial = self.wait("focused scene ready", lambda value: value["focus"] and not value["points"])
        self.report["initialLayout"] = dict(self.layout)
        button = initial["button"]
        button_x = self.layout["width"] - 140
        pixels = self.picture("initial", [[140, 180], [button_x, 160]])
        require(pixels[0][1] > pixels[0][0] + 50 and pixels[0][1] > pixels[0][2] + 50,
            "The real joystick knob is not green: " + str(pixels))
        require(pixels[1][2] > pixels[1][0] + 50, "The real UI button is not blue: " + str(pixels))

        cursor = len(self.states())
        for kind, buttons in [("mousePressed", 1), ("mouseReleased", 0)]:
            client.call("Input.dispatchMouseEvent", type=kind,
                x=self.layout["left"] + button_x, y=self.layout["top"] + 160,
                button="left", buttons=buttons, clickCount=1)
        self.wait("native UI mouse positive control",
            lambda value: value["clicks"] == 1 and value["pressed"] == 0, cursor)
        mouse_events = client.evaluate("window.kbMouseEvents.length")
        require(mouse_events > 0, "The real mouse positive control did not reach the DOM")

        cursor = len(self.states())
        self.touch("touchStart", [(7, 140, 180)])
        self.wait("gameplay finger owns joystick",
            lambda value: value["joy"] == 7 and value["pressed"] == 0 and self.point(value, 7), cursor)
        cursor = len(self.states())
        self.touch("touchStart", [(7, 140, 180), (23, button_x, 160)])
        self.wait("second finger independently presses UI",
            lambda value: value["joy"] == 7 and value["pressed"] == button and len(value["points"]) == 2, cursor)
        cursor = len(self.states())
        self.touch("touchMove", [(23, button_x, 160), (7, 200, 220)])
        self.wait("packet reorder preserves both owners", lambda value: value["joy"] == 7 and
            abs(value["x"] - 0.6) < 0.01 and abs(value["y"] - 0.4) < 0.01 and value["pressed"] == button, cursor)
        pixels = self.picture("held-controls", [[200, 220], [140, 180], [button_x, 160]])
        require(pixels[0][1] > pixels[0][0] + 50 and pixels[0][1] > pixels[1][1] + 50,
            "Joystick movement did not change real GPU pixels: " + str(pixels))
        cursor = len(self.states())
        self.touch("touchEnd", [(23, button_x, 160)])
        self.wait("UI release clicks while gameplay remains held", lambda value: value["joy"] == 7 and
            value["clicks"] == 2 and value["pressed"] == 0 and self.point(value, 7), cursor)
        require(any(self.point(value, 23, False) and self.point(value, 23, False)["phase"] == 2
            for value in self.states()[cursor:]), "The ended UI contact was not retained")
        pixels = self.picture("touch-click", [[button_x, 160]])
        require(pixels[0][2] > pixels[0][0] + 50, "The native UI click did not update real pixels")
        cursor = len(self.states())
        self.touch("touchEnd", [(7, 200, 220)])
        self.wait("gameplay release returns to neutral",
            lambda value: value["joy"] == 0 and value["pressed"] == 0 and not value["points"], cursor)

        cursor = len(self.states())
        self.touch("touchStart", [(23, button_x, 160)])
        self.touch("touchStart", [(23, button_x, 160), (7, 140, 180)])
        self.wait("UI-first contact order", lambda value: value["joy"] == 7 and
            value["pressed"] == button and len(value["points"]) == 2, cursor)
        cursor = len(self.states())
        self.touch("touchCancel", [])
        self.wait("cancel clears both captures without clicking", lambda value: value["joy"] == 0 and
            value["pressed"] == 0 and value["clicks"] == 2 and len(value["points"]) == 2 and
            all(point["phase"] == 3 and not point["active"] for point in value["points"]), cursor)
        self.wait("terminal samples are consumed normally",
            lambda value: value["joy"] == 0 and not value["points"], cursor)

        cursor = len(self.states())
        self.touch("touchStart", [(7, 140, 180)])
        self.touch("touchStart", [(7, 140, 180), (23, button_x, 160)])
        self.wait("both controls before focus loss",
            lambda value: value["joy"] == 7 and value["pressed"] == button, cursor)
        cursor = len(self.states())
        target_id = client.call("Target.createTarget", url="about:blank", background=False)["targetId"]
        other = None
        try:
            target = next(target for target in targets() if target["id"] == target_id)
            other = RuntimeClient(target["webSocketDebuggerUrl"])
            other.call("Page.enable")
            other.call("Page.bringToFront")
            wait_page_focus(client, False)
            client.call("Page.bringToFront")
            wait_page_focus(client, True)
        finally:
            client.call("Target.closeTarget", targetId=target_id)
            if other is not None:
                other.close()
        self.wait("focus return cancels both controls", lambda value: value["joy"] == 0 and
            value["pressed"] == 0 and value["clicks"] == 2 and len(value["points"]) == 2 and
            all(point["phase"] == 3 and not point["active"] for point in value["points"]), cursor)
        self.wait("actual focus restored",
            lambda value: value["focus"] and value["joy"] == 0 and not value["points"], cursor)
        pixels = self.picture("focus-restored", [[140, 180], [button_x, 160]])
        require(pixels[0][1] > pixels[0][0] + 50 and pixels[1][2] > pixels[1][0] + 50,
            "The actual joystick/UI rendering did not survive focus changes")

        self.touch("touchCancel", [])
        cursor = len(self.states())
        self.touch("touchStart", [(7, 140, 180)])
        self.touch("touchStart", [(7, 140, 180), (23, button_x, 160)])
        self.wait("both controls before resizing",
            lambda value: value["joy"] == 7 and value["pressed"] == button, cursor)
        cursor = len(self.states())
        client.call("Emulation.setDeviceMetricsOverride", width=1100, height=900,
            deviceScaleFactor=2, mobile=False)
        client.evaluate("Object.assign(document.querySelector('#canvas').style,{width:'960px',height:'540px'})")
        self.paint()
        self.geometry()
        self.wait("DPI/viewport change cancels old captures", lambda value: value["joy"] == 0 and
            value["pressed"] == 0 and value["clicks"] == 2 and all(not point["active"] for point in value["points"]), cursor)
        self.touch("touchCancel", [])
        cursor = len(self.states())
        self.touch("touchStart", [(7, 140, 180)])
        self.touch("touchStart", [(7, 140, 180), (23, self.layout["width"] - 140, 160)])
        self.wait("new contacts use the new physical scale", lambda value: value["joy"] == 7 and
            value["pressed"] == button and self.point(value, 7) and abs(self.point(value, 7)["x"] - 140) < 1, cursor)
        cursor = len(self.states())
        self.touch("touchCancel", [])
        self.wait("final controls idle",
            lambda value: value["joy"] == 0 and value["pressed"] == 0 and not value["points"], cursor)
        pixels = self.picture("resized", [[140, 180], [self.layout["width"] - 140, 160]])
        require(pixels[0][1] > pixels[0][0] + 50 and pixels[1][2] > pixels[1][0] + 50,
            "The resized controls did not render correctly")
        require(client.evaluate("window.kbMouseEvents.length") == mouse_events,
            "Touch produced emulated mouse events")
        dom = client.evaluate("window.kbTouchEvents")
        require(dom and all(event["trusted"] for event in dom), "The test did not use real trusted DOM input")
        self.report.update(trustedTouchEvents=dom, emulatedMouseEvents=0,
            graphics=self.graphics(), finalLayout=dict(self.layout))


def main(*, fixture=author_fixture, scenario_type=TouchScenario,
         schema="21kb.web-game-touch-tests/v1", prefix="GameTouch", description=None):
    parser = argparse.ArgumentParser(description=description or __doc__)
    for name in ["cli", "cooker", "engine-root", "shaderc", "directory", "browser", "output"]:
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--browser-arg", action="append", default=[])
    args = parser.parse_args()
    for name in ["cli", "cooker", "engine_root", "shaderc", "directory", "browser", "output"]:
        setattr(args, name, getattr(args, name).resolve())
    if os.name == "nt":
        ctypes.windll.kernel32.SetErrorMode(1 | 2 | 0x8000)
    args.output.mkdir(parents=True, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix=prefix, dir=args.output))
    report = dict(schema=schema, result="FAIL", observations=[], images=[])
    server = thread = process = client = scenario = None

    class Quiet(http.server.SimpleHTTPRequestHandler):
        def log_message(self, *_args):
            pass

    try:
        web = fixture(args, root)
        report["artifacts"] = {name: hashlib.sha256((web / name).read_bytes()).hexdigest()
            for name in ["Game.html", "Game.js", "Game.wasm", "Game.kbpack"]}
        server = http.server.ThreadingHTTPServer(("127.0.0.1", 0),
            lambda *values: Quiet(*values, directory=str(web)))
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        profile = root / "profile"
        with (root / "browser.log").open("wb") as log:
            process = subprocess.Popen([str(args.browser), "--headless=new",
                "--disable-extensions", "--disable-background-networking", "--disable-component-update",
                "--disable-default-apps", "--no-first-run", "--no-default-browser-check",
                "--remote-debugging-port=0", f"--user-data-dir={profile}",
                *args.browser_arg, "about:blank"], stdout=log, stderr=subprocess.STDOUT,
                creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
            deadline = time.monotonic() + 30
            while True:
                try:
                    port = int((profile / "DevToolsActivePort").read_text().splitlines()[0])
                    with urllib.request.urlopen(f"http://127.0.0.1:{port}/json/list", timeout=2) as response:
                        pages = json.load(response)
                    target = next(page for page in pages if page.get("url") == "about:blank")
                    break
                except (OSError, ValueError, StopIteration, IndexError):
                    require(process.poll() is None, "Own headless browser exited before startup")
                    require(time.monotonic() < deadline, "Own headless browser startup timed out")
                    time.sleep(0.05)

            def targets():
                with urllib.request.urlopen(f"http://127.0.0.1:{port}/json/list", timeout=2) as response:
                    return json.load(response)

            client = RuntimeClient(target["webSocketDebuggerUrl"])
            client.call("Runtime.enable")
            client.call("Page.enable")
            client.call("Page.bringToFront")
            report["browser"] = client.call("Browser.getVersion")
            client.call("Emulation.setTouchEmulationEnabled", enabled=True, maxTouchPoints=10)
            client.call("Emulation.setDeviceMetricsOverride", width=1100, height=900,
                deviceScaleFactor=1, mobile=False)
            client.call("Page.addScriptToEvaluateOnNewDocument", source=OBSERVER)
            client.call("Page.navigate", url=f"http://127.0.0.1:{server.server_port}/Game.html")
            scenario = scenario_type(client, root, report)
            scenario.run(targets)
            report["result"] = "PASS"
    except Exception as error:
        report.update(error=str(error), traceback=traceback.format_exc())
        if client is not None:
            try:
                report["failureState"] = client.evaluate("({hash:location.hash,"
                    "data:{...document.documentElement.dataset},focused:document.hasFocus(),"
                    "visibility:document.visibilityState,lost:window.kbTouchGraphics?.context.isContextLost()})")
                data = client.call("Page.captureScreenshot", format="png")["data"]
                (root / "failure.png").write_bytes(base64.b64decode(data))
            except Exception:
                pass
    finally:
        if client is not None:
            if scenario is not None:
                try:
                    (root / "states.json").write_text(json.dumps(scenario.states(), indent=2), encoding="utf-8")
                except Exception:
                    pass
            report["graphicsContexts"] = graphics_contexts(client.events)
            (root / "events.json").write_text(json.dumps(client.events, indent=2), encoding="utf-8")
            try:
                client.call("Browser.close")
            except Exception:
                pass
            client.close()
        if process is not None:
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.terminate()
                process.wait(timeout=5)
        if server is not None:
            server.shutdown()
            server.server_close()
            thread.join(timeout=5)
        (root / "result.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
        print(json.dumps(dict(result=report["result"], checks=len(report["observations"]),
            images=len(report["images"]), error=report.get("error", ""), artifacts=str(root))))
    return 0 if report["result"] == "PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())

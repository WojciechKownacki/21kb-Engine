"""Author, cook and test real WebGL script lifecycle results in an isolated headless browser.

Requires websocket-client and native CLI/cooker tools alongside a built WebGL host.
Only local test files and a fresh browser profile are used.
"""
from __future__ import annotations

import argparse
import base64
import ctypes
import http.server
import json
import os
import re
from pathlib import Path
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import time
import urllib.request

from run_input_tests import DevTools

sys.path.insert(0, str(Path(__file__).resolve().parents[4] / "scripts"))
from png_readback import read_png


def author_depth_fixture(run, args, project):
    for name, value in [("DepthOccluder", "0"), ("DepthSampler", "200")]:
        material = "/Game/Materials/" + name + ".kbmat"
        run(args.cli, "material-new", "--project", project, "--name", name)

        def graph(*tail):
            run(args.cli, "material-graph", tail[0], "--project", project, "--material", material, *tail[1:])

        graph("setting", "--name", "Shading Model", "--value", "unlit")
        graph("add-node", "--kind", "Constant", "--name", "Scale", "--value", value)
        if name == "DepthSampler":
            graph("setting", "--name", "Blend Mode", "--value", "translucent")
            graph("add-node", "--kind", "Constant", "--name", "Opacity", "--value", "0.5")
            graph("connect", "--from", "Opacity", "--to", "Output", "--to-pin", "alpha")
            graph("add-node", "--kind", "SceneDepth", "--name", "Depth")
            graph("add-node", "--kind", "Multiply", "--name", "VisibleDepth")
            graph("connect", "--from", "Depth", "--to", "VisibleDepth", "--to-pin", "a")
            graph("connect", "--from", "Scale", "--to", "VisibleDepth", "--to-pin", "b")
            output = "VisibleDepth"
        else:
            output = "Scale"
        graph("add-node", "--kind", "MakeVector", "--name", "Color")
        for channel in ["x", "y", "z"]:
            graph("connect", "--from", output, "--to", "Color", "--to-pin", channel)
        graph("add-node", "--kind", "Constant", "--name", "One", "--value", "1")
        graph("connect", "--from", "One", "--to", "Color", "--to-pin", "w")
        graph("connect", "--from", "Color", "--to", "Output", "--to-pin", "baseColor")
        graph("connect", "--from", "Color", "--to", "Output", "--to-pin", "emissive")
        run(args.cli, "entity", "add", "--project", project, "--name", name, "--create", "Cube",
            "--position", "0,0,2" if name == "DepthSampler" else "0,0,4")
        run(args.cli, "component", "set", "--project", project, "--entity", name,
            "--type", "MeshRenderer", "--set", "material=" + material)
        if name == "DepthSampler":
            run(args.cli, "component", "set", "--project", project, "--entity", name,
                "--type", "Transform", "--set", "localScale.x=12", "--set", "localScale.y=12")
    run(args.cli, "component", "set", "--project", project, "--entity", "RuntimeProbe",
        "--type", "Border", "--set", "backgroundColor.a=0")


def capture_depth(client, directory, label):
    client.call("Runtime.evaluate", expression="new Promise(resolve => requestAnimationFrame("
        "() => requestAnimationFrame(() => resolve(true))))", awaitPromise=True)
    rect = client.evaluate("(() => {const r=document.querySelector('#canvas').getBoundingClientRect();"
        "return {x:r.x,y:r.y,width:r.width,height:r.height};})()")
    data = client.call("Page.captureScreenshot", format="png", clip=dict(**rect, scale=1),
        captureBeyondViewport=True)["data"]
    path = directory / (label + ".png")
    path.write_bytes(base64.b64decode(data))
    rows = read_png(path)
    height, width = len(rows), len(rows[0])
    pixels = [list(rows[height // 2][width // 2]), list(rows[height // 2][width // 10])]
    require(min(pixels[0]) > max(pixels[1]) + 50,
        "The translucent material did not sample copied opaque depth: " + str(pixels))
    return pixels


class RuntimeClient(DevTools):
    def call(self, method, **parameters):
        self.next_id += 1
        wanted = self.next_id
        self.socket.send(json.dumps(dict(id=wanted, method=method, params=parameters)))
        deadline = time.monotonic() + 20
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError("Browser command timed out: " + method)
            self.socket.settimeout(remaining)
            response = json.loads(self.socket.recv())
            if response.get("id") == wanted:
                if "error" in response:
                    raise RuntimeError(str(response["error"]))
                return response.get("result", {})
            self.events.append(response)
            if response.get("method") == "Page.javascriptDialogOpening":
                # The generated shell can show a context-lost alert on renderer shutdown.
                # Record and dismiss it so a real failure remains observable by the test.
                self.next_id += 1
                self.socket.send(json.dumps(dict(id=self.next_id,
                    method="Page.handleJavaScriptDialog", params=dict(accept=True))))


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def capture_panel(client, directory, label):
    client.call("Runtime.evaluate", expression="new Promise(resolve => requestAnimationFrame("
        "() => requestAnimationFrame(() => resolve(true))))", awaitPromise=True)
    rect = client.evaluate("(() => {const c = document.querySelector('#canvas');"
        "const r = c.getBoundingClientRect();return {x:r.x,y:r.y,width:r.width,height:r.height};})()")
    data = client.call("Page.captureScreenshot", format="png", clip=dict(**rect, scale=1),
        captureBeyondViewport=True)["data"]
    (directory / (label + ".png")).write_bytes(base64.b64decode(data))
    result = client.call("Runtime.evaluate", expression="(async () => {const image = await createImageBitmap("
        "await (await fetch('data:image/png;base64," + data + "')).blob());"
        "const canvas = new OffscreenCanvas(image.width,image.height);const ctx = canvas.getContext('2d');"
        "ctx.drawImage(image,0,0);return Array.from(ctx.getImageData(image.width/2,image.height/2,1,1).data);})()",
        awaitPromise=True, returnByValue=True)
    require("exceptionDetails" not in result, "GPU screenshot could not be decoded: " + str(result))
    pixel = result["result"]["value"]
    require(pixel[2] > pixel[0] + 50 and pixel[2] > pixel[1] + 50,
        "The actual rendered panel is missing: " + str(pixel))
    return pixel


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ["cli", "cooker", "engine-root", "shaderc", "directory", "browser", "output"]:
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--case", choices=["positive", "ready", "tick", "loader", "graphics-loss", "graphics-recreate", "graphics-exhaustion", "geometry-depth"], action="append")
    parser.add_argument("--browser-arg", action="append", default=[])
    args = parser.parse_args()
    for name in ["cli", "cooker", "engine_root", "shaderc", "directory", "browser", "output"]:
        setattr(args, name, getattr(args, name).resolve())
    if os.name == "nt":
        ctypes.windll.kernel32.SetErrorMode(1 | 2 | 0x8000)
    args.output.mkdir(parents=True, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix="PackagedRuntime", dir=args.output)).resolve()
    project = root / "Sample"
    history = []
    report = dict(schema="21kb.web-packaged-runtime-tests/v1", result="FAIL", cases=[])

    def run(executable, *tail):
        process = subprocess.run([str(executable.resolve()), *map(str, tail)], cwd=root,
            capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=180,
            creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        history.append(dict(executable=str(executable), arguments=list(map(str, tail)),
            code=process.returncode, stdout=process.stdout, stderr=process.stderr))
        (root / "commands.json").write_text(json.dumps(history, indent=2), encoding="utf-8")
        require(process.returncode == 0, "Authoring/cook failed: " + str(history[-1]))

    run(args.cli, "new", "--parent", root, "--name", "Sample", "--template", "Empty")
    run(args.cli, "entity", "add", "--project", project, "--create", "Camera", "--name", "MainCamera")
    run(args.cli, "entity", "add", "--project", project, "--name", "RuntimeProbe")
    run(args.cli, "entity", "add", "--project", project, "--name", "FallingBody", "--position", "0,5,0")
    run(args.cli, "component", "add", "--project", project, "--entity", "FallingBody", "--type", "Collider")
    run(args.cli, "component", "add", "--project", project, "--entity", "FallingBody", "--type", "Rigidbody")
    run(args.cli, "script-new", "--project", project, "--name", "RuntimeProbe", "--language", "lua")
    run(args.cli, "scene-attach", "--project", project, "--scene", "Assets/Scenes/Main.21kbscene",
        "--node", "RuntimeProbe", "--script", "/Game/Scripts/RuntimeProbe.lua")
    run(args.cli, "component", "add", "--project", project, "--entity", "RuntimeProbe", "--type", "Rect Transform",
        "--set", "anchorMax=1,1", "--set", "offsetMax=0,0")
    run(args.cli, "component", "add", "--project", project, "--entity", "RuntimeProbe", "--type", "Canvas")
    run(args.cli, "component", "add", "--project", project, "--entity", "RuntimeProbe", "--type", "Border",
        "--set", "backgroundColor.r=0.1", "--set", "backgroundColor.g=0.2", "--set", "backgroundColor.b=0.9",
        "--set", "backgroundColor.a=1")

    class Quiet(http.server.SimpleHTTPRequestHandler):
        def log_message(self, *_args):
            pass

    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0),
        lambda *values: Quiet(*values, directory=str(root)))
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        for case in args.case or ["positive", "ready", "tick", "loader", "graphics-loss", "graphics-recreate", "graphics-exhaustion", "geometry-depth"]:
            case_project = project
            if case == "geometry-depth":
                case_project = root / ("DepthSample" + str(len(report["cases"])))
                shutil.copytree(project, case_project)
                author_depth_fixture(run, args, case_project)
            marker = "packaged-runtime-" + case
            ready = 'error("' + marker + '")' if case == "ready" else 'Log("' + marker + '-ready")'
            tick = ('error("' + marker + '")' if case == "tick" else
                    'assert(Input.TouchCount() == 0)\n'
                    'local velocity = Physics.GetVelocity(World.FindByName("FallingBody"))\n'
                    'assert(velocity.found and velocity.y < 0, "packaged gravity did not execute")\n'
                    'Log("' + marker + '-physics-y=" .. tostring(velocity.y))\n'
                    'Log("' + marker + '-ticks")')
            input_probe = ('local point = Input.TouchPoint(0)\nLog("' + marker +
                '-input=" .. tostring(Input.TouchCount()) .. "," .. tostring(point.id) .. "," .. '
                'tostring(point.phase) .. "," .. tostring(point.active) .. "," .. tostring(Input.HasFocus()))\n'
                'local velocity = Physics.GetVelocity(World.FindByName("FallingBody"))\n'
                'if velocity.found then Log("' + marker + '-state-y=" .. tostring(velocity.y)) end\n'
                if case.startswith("graphics-") else '')
            (case_project / "Assets/Scripts/RuntimeProbe.lua").write_text(
                'function Ready(self, dt)\n' + ready + '\nend\n'
                'function Tick(self, dt)\nlocal frames = (GetShared("runtime.frames") or 0) + 1\n'
                'SetShared("runtime.frames", frames)\nif ' + ('true' if case.startswith("graphics-") else 'frames <= 8') + ' then Log("' + marker +
                '-frame=" .. tostring(frames)) end\nif frames == 8 then\n' + tick +
                '\nend\n' + input_probe + 'end\n' + ('function Destroyed(self, dt) Log("' + marker + '-destroyed") end\n'
                    if case.startswith("graphics-") else ''), encoding="utf-8")
            directory = root / case
            directory.mkdir()
            for name in ["Game.html", "Game.js", "Game.wasm"]:
                shutil.copy2(args.directory / name, directory / name)
            if case == "loader":
                (directory / "Game.wasm").unlink()
            run(args.cooker, "--project", case_project, "--target", "WebGL.wasm32",
                "--output", directory / "Game.kbpack", "--engine-root", args.engine_root,
                "--shaderc", args.shaderc, "--cache", root / "CookCache")
            with socket.socket() as reservation:
                reservation.bind(("127.0.0.1", 0))
                port = reservation.getsockname()[1]
            process = client = None
            observations = []
            try:
                with (directory / "browser.log").open("wb") as log:
                    process = subprocess.Popen([str(args.browser.resolve()), "--headless=new",
                        "--disable-extensions", "--disable-background-networking", "--disable-component-update",
                        "--disable-default-apps", "--no-first-run", "--no-default-browser-check",
                        f"--remote-debugging-port={port}", f"--user-data-dir={directory / 'profile'}",
                        *args.browser_arg, "about:blank"], stdout=log, stderr=subprocess.STDOUT,
                        creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
                    until = time.monotonic() + 30
                    while True:
                        try:
                            with urllib.request.urlopen(f"http://127.0.0.1:{port}/json/list", timeout=2) as response:
                                targets = json.load(response)
                            target = next(t for t in targets if t.get("url") == "about:blank")
                            break
                        except (OSError, ValueError, StopIteration):
                            if time.monotonic() >= until:
                                raise TimeoutError("Own browser startup timed out")
                            time.sleep(0.1)
                    client = RuntimeClient(target["webSocketDebuggerUrl"])
                    client.call("Runtime.enable")
                    client.call("Page.enable")
                    if case == "loader":
                        client.call("Network.enable")
                    client.call("Page.bringToFront")
                    # Observe the context actually used by the host; do not create a test context.
                    client.call("Page.addScriptToEvaluateOnNewDocument", source="""
const compile = WebGL2RenderingContext.prototype.compileShader;
WebGL2RenderingContext.prototype.compileShader = function(shader) {
    if (!window.kbRuntimeGraphics || window.kbRuntimeGraphics.context !== this) {
        const ext = this.getExtension('WEBGL_debug_renderer_info');
        window.kbRuntimeGraphics = {context:this, version:this.getParameter(this.VERSION),
            renderer:ext ? this.getParameter(ext.UNMASKED_RENDERER_WEBGL) : this.getParameter(this.RENDERER),
            shaders:[], copies:[], depthCopies:[]};
    }
    compile.call(this, shader);
    window.kbRuntimeGraphics.shaders.push({compiled:this.getShaderParameter(shader, this.COMPILE_STATUS),
        message:this.getShaderInfoLog(shader)});
};
const copy = WebGL2RenderingContext.prototype.copyTexSubImage2D;
WebGL2RenderingContext.prototype.copyTexSubImage2D = function(...args) {
    window.kbRuntimeGraphics.copies.push({complete:
        this.checkFramebufferStatus(this.FRAMEBUFFER) === this.FRAMEBUFFER_COMPLETE});
    return copy.apply(this, args);
};
const blit = WebGL2RenderingContext.prototype.blitFramebuffer;
WebGL2RenderingContext.prototype.blitFramebuffer = function(...args) {
    if (args[8] & this.DEPTH_BUFFER_BIT) {
        window.kbRuntimeGraphics.depthCopies.push({
            readComplete:this.checkFramebufferStatus(this.READ_FRAMEBUFFER) === this.FRAMEBUFFER_COMPLETE,
            drawComplete:this.checkFramebufferStatus(this.DRAW_FRAMEBUFFER) === this.FRAMEBUFFER_COMPLETE,
            mask:args[8],filter:args[9]
        });
    }
    return blit.apply(this, args);
};
""")
                    client.call("Page.navigate", url=f"http://127.0.0.1:{server.server_port}/{case}/Game.html")
                    until = time.monotonic() + 60
                    while True:
                        state = client.evaluate("({hash:location.hash,data:{...document.documentElement.dataset},"
                            "output:document.querySelector('#output')?.value})")
                        observations.append(state)
                        exceptions = [event["params"]["exceptionDetails"] for event in client.events
                            if event.get("method") == "Runtime.exceptionThrown"]
                        require(case == "loader" or not exceptions, "Unhandled browser/runtime exception: " + str(exceptions))
                        if state["data"].get("kbError"):
                            require(case not in ("positive", "geometry-depth") and not case.startswith("graphics-"), "Positive runtime failed: " + str(state))
                            break
                        if (case in ("positive", "geometry-depth") or case.startswith("graphics-")) and marker + "-ticks" in (state.get("output") or ""):
                            break
                        if time.monotonic() >= until:
                            raise TimeoutError("Packaged lifecycle result missing: " + str(state))
                        time.sleep(0.05)
                    if case == "loader":
                        require(state["data"].get("kbError") == "runtime-aborted" and state["hash"] == "#kb-error-runtime-aborted" and
                            not state["data"].get("kbFirstFrame") and marker not in (state.get("output") or ""),
                            "The missing WASM file did not produce a startup failure")
                        require(any(event.get("method") == "Network.responseReceived" and
                            event["params"]["response"]["url"].endswith("/Game.wasm") and
                            event["params"]["response"]["status"] == 404 for event in client.events),
                            "The loader test did not observe the actual missing WASM response")
                        require(exceptions and all("Aborted(" in json.dumps(error) for error in exceptions),
                            "The startup exception did not come from the actual WASM loader")
                    elif case in ("positive", "geometry-depth") or case.startswith("graphics-"):
                        require(state["data"].get("kbFirstFrame") == "true" and
                            state["data"].get("kbBackend") == "webgl", "Actual WebGL frame missing")
                        require(marker + "-ready" in state["output"], "Actual Lua ready Log missing")
                        velocity = re.search(re.escape(marker) + r"-physics-y=([-+\d.eE]+)", state["output"])
                        require(velocity is not None and float(velocity[1]) < 0,
                            "Actual packaged physics result missing")
                        state["physicsVelocityY"] = float(velocity[1])
                        state["gl"] = client.evaluate("(() => {const observed = window.kbRuntimeGraphics;"
                            "if (!observed) return {available:false};"
                            "return {available:true,lost:observed.context.isContextLost(),"
                            "version:observed.version,renderer:observed.renderer,shaders:observed.shaders};})()")
                        require(state["gl"]["available"] and not state["gl"]["lost"],
                            "Rendered package has no live WebGL2 context: " + str(state["gl"]))
                        require(state["gl"]["shaders"] and all(shader["compiled"] for shader in state["gl"]["shaders"]),
                            "Actual host shader compilation failed")
                        require(not any(word in state["gl"]["renderer"].lower() for word in
                            ["swiftshader", "llvmpipe", "softpipe", "microsoft basic", "warp", "software"]),
                            "Packaged graphics checks require a hardware GPU: " + str(state["gl"]))
                        if case == "geometry-depth":
                            state["depthCopies"] = client.evaluate("window.kbRuntimeGraphics.depthCopies")
                            state["depthPixels"] = capture_depth(client, directory, "copied-depth")
                            require(state["depthCopies"] and all(item["readComplete"] and item["drawComplete"]
                                for item in state["depthCopies"]), "Actual depth blit used an incomplete framebuffer")
                            require(all(item["complete"] for item in client.evaluate("window.kbRuntimeGraphics.copies")),
                                "Geometry rendering copied from an incomplete framebuffer")
                        if case == "positive":
                            require(client.evaluate("typeof Module.requestFullscreen === 'function'"),
                                "The existing browser fullscreen entry point was removed")
                            client.evaluate("Object.assign(document.querySelector('#canvas').style,{width:'800px',height:'600px'})")
                            state["panelPixel"] = capture_panel(client, directory, "resized-panel")
                            target_id = client.call("Target.createTarget", url="about:blank", background=False)["targetId"]
                            other = None
                            try:
                                with urllib.request.urlopen(f"http://127.0.0.1:{port}/json/list", timeout=2) as response:
                                    target = next(t for t in json.load(response) if t["id"] == target_id)
                                other = RuntimeClient(target["webSocketDebuggerUrl"])
                                other.call("Page.enable")
                                other.call("Page.bringToFront")
                                require(client.evaluate("!document.hasFocus() && document.visibilityState === 'hidden'"),
                                    "The own second tab did not cause actual focus loss")
                                client.call("Page.bringToFront")
                            finally:
                                client.call("Target.closeTarget", targetId=target_id)
                                if other is not None:
                                    other.close()
                            state["focusRestoredPixel"] = capture_panel(client, directory, "focus-restored-panel")
                            state["focusRestored"] = client.evaluate("({data:{...document.documentElement.dataset},"
                                "focused:document.hasFocus(),lost:window.kbRuntimeGraphics.context.isContextLost(),"
                                "renderer:window.kbRuntimeGraphics.renderer,shaders:window.kbRuntimeGraphics.shaders,"
                                "statusHidden:document.querySelector('#status')?.hidden})")
                            require(state["focusRestored"]["focused"] and not state["focusRestored"]["lost"] and
                                not state["focusRestored"]["data"].get("kbError") and state["focusRestored"]["statusHidden"],
                                "The rendered package did not survive an actual tab focus change")
                            require(not any(word in state["focusRestored"]["renderer"].lower() for word in
                                ["swiftshader", "llvmpipe", "softpipe", "microsoft basic", "warp", "software"]) and
                                all(shader["compiled"] for shader in state["focusRestored"]["shaders"]),
                                "Focus recovery did not retain actual hardware rendering")
                            state["textureCopies"] = client.evaluate("window.kbRuntimeGraphics.copies")
                            require(state["textureCopies"] and all(item["complete"] for item in state["textureCopies"]),
                                "UI-only rendering attempted a copy from an incomplete framebuffer")
                        if case.startswith("graphics-"):
                            state["initialPixel"] = capture_panel(client, directory, "before-loss")
                            state["recoveryCycles"] = []
                            client.call("Emulation.setTouchEmulationEnabled", enabled=True, maxTouchPoints=10)
                            for cycle in range(4 if case == "graphics-exhaustion" else 2):
                                shader_count = client.evaluate("window.kbRuntimeGraphics.shaders.length")
                                position = client.evaluate("(() => {const r = document.querySelector('#canvas')."
                                    "getBoundingClientRect();return {x:r.x+r.width/2,y:r.y+r.height/2};})()")
                                output_offset = len(client.evaluate("document.querySelector('#output')?.value") or "")
                                client.call("Input.dispatchTouchEvent", type="touchStart",
                                    touchPoints=[dict(id=7, **position, radiusX=1, radiusY=1, force=1)])
                                until = time.monotonic() + 10
                                while True:
                                    active = client.evaluate("document.querySelector('#output')?.value") or ""
                                    if re.search(re.escape(marker) + r"-input=1,7,[01],true,true", active[output_offset:]):
                                        break
                                    require(time.monotonic() < until, "Actual contact did not reach the running script")
                                    time.sleep(0.05)
                                injected = client.evaluate("(() => {const ext = window.kbRuntimeGraphics.context.getExtension('WEBGL_lose_context');"
                                    "if (!ext) return false; window.kbGraphicsLossExtension = ext;"
                                    "window.kbLostGraphics = window.kbRuntimeGraphics.context;window.kbLostCanvas = window.kbLostGraphics.canvas;"
                                    "ext.loseContext(); return true;})()")
                                require(injected, "Actual WebGL context loss injection is unavailable")
                                until = time.monotonic() + 10
                                while True:
                                    lost = client.evaluate("({hash:location.hash,data:{...document.documentElement.dataset},"
                                        "output:document.querySelector('#output')?.value})")
                                    observations.append(lost)
                                    if lost["data"].get("kbError"):
                                        break
                                    if time.monotonic() >= until:
                                        raise TimeoutError("Graphics loss left a successful/stale runtime result: " + str(lost))
                                    time.sleep(0.05)
                                require(lost["data"]["kbError"] == "graphics-context-lost" and
                                    lost["hash"] == "#kb-error-graphics-context-lost", "Wrong graphics loss result")
                                require(client.evaluate("window.kbRuntimeGraphics.context.isContextLost()"),
                                    "The real graphics context was not lost")
                                require("graphics context lost" in json.dumps(client.events, ensure_ascii=False),
                                    "Graphics loss did not reach the host diagnostic output")
                                output = lost.get("output") or ""
                                require(output.count(marker + "-ready") == 1 and marker + "-destroyed" not in output,
                                    "Graphics loss destroyed or restarted the running scene")
                                frames = re.findall(re.escape(marker) + r"-frame=(\d+)", output)
                                client.call("Runtime.evaluate", expression="new Promise(resolve => requestAnimationFrame("
                                    "() => requestAnimationFrame(() => resolve(true))))", awaitPromise=True)
                                after = client.evaluate("document.querySelector('#output')?.value") or ""
                                require(re.findall(re.escape(marker) + r"-frame=(\d+)", after) == frames,
                                    "Game simulation continued after graphics loss")
                                if case == "graphics-exhaustion" and cycle == 3:
                                    until = time.monotonic() + 15
                                    while True:
                                        expired = client.evaluate("({hash:location.hash,data:{...document.documentElement.dataset},"
                                            "output:document.querySelector('#output')?.value})")
                                        observations.append(expired)
                                        if expired["data"].get("kbError") == "graphics-restore-limit" and marker + "-destroyed" in (expired.get("output") or ""):
                                            break
                                        require(time.monotonic() < until, "Unrecoverable graphics loss did not stop cleanly")
                                        time.sleep(0.1)
                                    require(expired["hash"] == "#kb-error-graphics-restore-limit" and
                                        expired["output"].count(marker + "-destroyed") == 1 and
                                        re.findall(re.escape(marker) + r"-frame=(\d+)", expired["output"]) == frames,
                                        "Failed recovery restarted simulation or repeated shutdown")
                                    state["recoveryLimit"] = expired
                                    break
                                if case == "graphics-loss":
                                    require(client.evaluate("(() => {const ext = window.kbGraphicsLossExtension;"
                                        "if (!ext) return false;ext.restoreContext();return true;})()"),
                                        "Actual WebGL restoration injection is unavailable")
                                until = time.monotonic() + 20
                                while True:
                                    restored = client.evaluate("({hash:location.hash,data:{...document.documentElement.dataset},"
                                        "output:document.querySelector('#output')?.value,"
                                        "lost:window.kbRuntimeGraphics.context.isContextLost(),"
                                        "newCanvas:window.kbRuntimeGraphics.context.canvas !== window.kbLostCanvas,"
                                        "oldLost:window.kbLostGraphics.isContextLost(),renderer:window.kbRuntimeGraphics.renderer,"
                                        "shaders:window.kbRuntimeGraphics.shaders})")
                                    observations.append(restored)
                                    if not restored["lost"] and not restored["data"].get("kbError") and restored["hash"] == "#kb-ready-webgl":
                                        break
                                    require(time.monotonic() < until, "GPU rendering did not recover: " + str(restored))
                                    time.sleep(0.05)
                                resumed = re.findall(re.escape(marker) + r"-frame=(\d+)", restored["output"])
                                require(resumed[:len(frames)] == frames and len(resumed) > len(frames) and
                                    int(resumed[len(frames)]) == int(frames[-1]) + 1,
                                    "Restoration reset or failed to resume the real script state")
                                require(restored["output"].count(marker + "-ready") == 1 and
                                    marker + "-destroyed" not in restored["output"], "Restoration restarted the scene lifecycle")
                                require((len(restored["shaders"]) > shader_count if case == "graphics-loss" else
                                    restored["newCanvas"] and restored["oldLost"] and bool(restored["shaders"])) and
                                    all(shader["compiled"] for shader in restored["shaders"]),
                                    "Real bgfx shader resources were not rebuilt")
                                require(not any(word in restored["renderer"].lower() for word in
                                    ["swiftshader", "llvmpipe", "softpipe", "microsoft basic", "warp", "software"]),
                                    "Restoration switched to a software GPU")
                                before_velocity = re.findall(re.escape(marker) + r"-state-y=([-+\d.eE]+)", output)
                                resumed_velocity = re.findall(re.escape(marker) + r"-state-y=([-+\d.eE]+)", restored["output"][len(output):])
                                require(before_velocity and resumed_velocity and
                                    float(resumed_velocity[0]) <= float(before_velocity[-1]) + 0.0001,
                                    "Graphics recovery reset the real physics velocity")
                                restored["pausedVelocityY"] = float(before_velocity[-1])
                                restored["resumedVelocityY"] = float(resumed_velocity[0])
                                state["restoredPixel"] = capture_panel(client, directory, "after-restore-" + str(cycle))
                                require(marker + "-input=1,7,3,false,false" in restored["output"][len(output):],
                                    "Restoration discarded the cancelled contact or reused an input capture")
                                client.call("Input.dispatchTouchEvent", type="touchCancel", touchPoints=[])
                                state["recoveryCycles"].append(dict(lost=lost, restored=restored,
                                    pixel=state["restoredPixel"]))
                                require(not any(event.get("method") == "Runtime.exceptionThrown" for event in client.events),
                                    "Graphics loss caused an unhandled browser/WASM exception")
                    else:
                        require(state["data"].get("kbError") == "runtime-failed" and
                            state["hash"] == "#kb-error-runtime-failed", "Wrong runtime failure result")
                        messages = json.dumps(client.events, ensure_ascii=False)
                        require(marker in messages and "script error:" in messages,
                            "Actual cooked Lua fault diagnostic missing")
                        require((state["data"].get("kbFirstFrame") == "true") == (case == "tick"),
                            "Failure occurred at the wrong lifecycle phase")
                    if case in ("ready", "tick", "loader"):
                        state["failureMessage"] = client.evaluate("({hidden:document.querySelector('#status')?.hidden,"
                            "text:document.querySelector('#status')?.textContent})")
                        require(state["failureMessage"]["hidden"] is False and
                            "Reload" in state["failureMessage"]["text"], "The player did not show an actionable failure")
                    final_exceptions = [event["params"]["exceptionDetails"] for event in client.events
                        if event.get("method") == "Runtime.exceptionThrown"]
                    require((case == "loader" and final_exceptions and all("Aborted(" in json.dumps(error) for error in final_exceptions)) or
                        (case != "loader" and not final_exceptions),
                        "Packaged runtime caused an unhandled browser/WASM exception")
                    report["cases"].append(dict(case=case, result="PASS", state=state))
            finally:
                if client is not None:
                    (directory / "events.json").write_text(json.dumps(client.events, indent=2), encoding="utf-8")
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
                (directory / "states.json").write_text(json.dumps(observations, indent=2), encoding="utf-8")
        report["result"] = "PASS"
    except Exception as error:
        report["error"] = str(error)
        raise
    finally:
        server.shutdown()
        server.server_close()
        thread.join(timeout=5)
        (root / "result.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
        print(json.dumps(dict(result=report["result"], cases=len(report["cases"]), artifacts=str(root))))


if __name__ == "__main__":
    main()

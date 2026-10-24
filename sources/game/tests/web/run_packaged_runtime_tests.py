"""Author, cook and test real WebGL script lifecycle results in an isolated headless browser.

Requires websocket-client and native CLI/cooker tools alongside a built WebGL host.
Only local test files and a fresh browser profile are used.
"""
from __future__ import annotations

import argparse
import ctypes
import http.server
import json
import os
import re
from pathlib import Path
import shutil
import socket
import subprocess
import tempfile
import threading
import time
import urllib.request

from run_input_tests import DevTools


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


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ["cli", "cooker", "engine-root", "shaderc", "directory", "browser", "output"]:
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--case", choices=["positive", "ready", "tick", "graphics-loss"], action="append")
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

    class Quiet(http.server.SimpleHTTPRequestHandler):
        def log_message(self, *_args):
            pass

    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0),
        lambda *values: Quiet(*values, directory=str(root)))
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        for case in args.case or ["positive", "ready", "tick", "graphics-loss"]:
            marker = "packaged-runtime-" + case
            ready = 'error("' + marker + '")' if case == "ready" else 'Log("' + marker + '-ready")'
            tick = ('error("' + marker + '")' if case == "tick" else
                    'assert(Input.TouchCount() == 0)\n'
                    'local velocity = Physics.GetVelocity(World.FindByName("FallingBody"))\n'
                    'assert(velocity.found and velocity.y < 0, "packaged gravity did not execute")\n'
                    'Log("' + marker + '-physics-y=" .. tostring(velocity.y))\n'
                    'Log("' + marker + '-ticks")')
            (project / "Assets/Scripts/RuntimeProbe.lua").write_text(
                'function Ready(self, dt)\n' + ready + '\nend\n'
                'function Tick(self, dt)\nlocal frames = (GetShared("runtime.frames") or 0) + 1\n'
                'SetShared("runtime.frames", frames)\nif ' + ('true' if case == "graphics-loss" else 'frames <= 8') + ' then Log("' + marker +
                '-frame=" .. tostring(frames)) end\nif frames == 8 then\n' + tick +
                '\nend\nend\n' + ('function Destroyed(self, dt) Log("' + marker + '-destroyed") end\n'
                    if case == "graphics-loss" else ''), encoding="utf-8")
            directory = root / case
            directory.mkdir()
            for name in ["Game.html", "Game.js", "Game.wasm"]:
                shutil.copy2(args.directory / name, directory / name)
            run(args.cooker, "--project", project, "--target", "WebGL.wasm32",
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
                    # Observe the context actually used by the host; do not create a test context.
                    client.call("Page.addScriptToEvaluateOnNewDocument", source="""
const compile = WebGL2RenderingContext.prototype.compileShader;
WebGL2RenderingContext.prototype.compileShader = function(shader) {
    if (!window.kbRuntimeGraphics) {
        const ext = this.getExtension('WEBGL_debug_renderer_info');
        window.kbRuntimeGraphics = {context:this, version:this.getParameter(this.VERSION),
            renderer:ext ? this.getParameter(ext.UNMASKED_RENDERER_WEBGL) : this.getParameter(this.RENDERER),
            shaders:[]};
    }
    compile.call(this, shader);
    window.kbRuntimeGraphics.shaders.push({compiled:this.getShaderParameter(shader, this.COMPILE_STATUS),
        message:this.getShaderInfoLog(shader)});
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
                        require(not exceptions, "Unhandled browser/runtime exception: " + str(exceptions))
                        if state["data"].get("kbError"):
                            require(case not in ("positive", "graphics-loss"), "Positive runtime failed: " + str(state))
                            break
                        if case in ("positive", "graphics-loss") and marker + "-ticks" in (state.get("output") or ""):
                            break
                        if time.monotonic() >= until:
                            raise TimeoutError("Packaged lifecycle result missing: " + str(state))
                        time.sleep(0.05)
                    if case in ("positive", "graphics-loss"):
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
                        if case == "graphics-loss":
                            injected = client.evaluate("(() => {const ext = window.kbRuntimeGraphics.context.getExtension('WEBGL_lose_context');"
                                "if (!ext) return false; ext.loseContext(); return true;})()")
                            require(injected, "Actual WebGL context loss injection is unavailable")
                            until = time.monotonic() + 10
                            while True:
                                lost = client.evaluate("({hash:location.hash,data:{...document.documentElement.dataset},"
                                    "output:document.querySelector('#output')?.value})")
                                observations.append(lost)
                                if lost["data"].get("kbError") and marker + "-destroyed" in (lost.get("output") or ""):
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
                            require(output.count(marker + "-ready") == 1 and output.count(marker + "-destroyed") == 1,
                                "Graphics loss repeated or skipped the script lifecycle")
                            frames = re.findall(re.escape(marker) + r"-frame=(\d+)", output)
                            client.call("Runtime.evaluate", expression="new Promise(resolve => requestAnimationFrame("
                                "() => requestAnimationFrame(() => resolve(true))))", awaitPromise=True)
                            after = client.evaluate("document.querySelector('#output')?.value") or ""
                            require(re.findall(re.escape(marker) + r"-frame=(\d+)", after) == frames,
                                "Game simulation continued after graphics loss")
                            require(not any(event.get("method") == "Runtime.exceptionThrown" for event in client.events),
                                "Graphics loss caused an unhandled browser/WASM exception")
                            state["graphicsLoss"] = lost
                    else:
                        require(state["data"].get("kbError") == "runtime-failed" and
                            state["hash"] == "#kb-error-runtime-failed", "Wrong runtime failure result")
                        messages = json.dumps(client.events, ensure_ascii=False)
                        require(marker in messages and "script error:" in messages,
                            "Actual cooked Lua fault diagnostic missing")
                        require((state["data"].get("kbFirstFrame") == "true") == (case == "tick"),
                            "Failure occurred at the wrong lifecycle phase")
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

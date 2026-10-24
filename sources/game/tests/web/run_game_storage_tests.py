"""Cook a game and verify browser save transactions and profile persistence.

Only a fresh owned profile, local project and local HTTP origin are used.
Quota and interrupted transactions are real IndexedDB failures, not passing
substitutes for a successful save. Hardware rendering is checked separately.
"""
from __future__ import annotations

import argparse
import ctypes
import hashlib
import http.server
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import threading
import time
import traceback
import urllib.request

from run_game_touch_tests import OBSERVER, RuntimeClient, TouchScenario, require


def author_fixture(args, root):
    project, web = root / "Sample", root / "Web"
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

    def component(name, kind, *settings):
        tail = ["component", "add", "--project", project, "--entity", name, "--type", kind]
        for setting in settings:
            tail += ["--set", setting]
        run(args.cli, *tail)

    run(args.cli, "new", "--parent", root, "--name", "Sample", "--template", "Empty")
    run(args.cli, "entity", "add", "--project", project, "--create", "Camera", "--name", "MainCamera")
    run(args.cli, "entity", "add", "--project", project, "--name", "CanvasRoot")
    component("CanvasRoot", "Rect Transform", "anchorMax=1,1", "offsetMax=0,0")
    component("CanvasRoot", "Canvas")
    component("CanvasRoot", "Canvas Scaler", "scaleMode=0", "scaleFactor=1")
    run(args.cli, "entity", "add", "--project", project, "--name", "SaveButton", "--parent", "CanvasRoot")
    component("CanvasRoot/SaveButton", "Rect Transform", "offsetMin=40,80", "offsetMax=240,240")
    component("CanvasRoot/SaveButton", "Border", "backgroundColor.r=0.1",
        "backgroundColor.g=0.2", "backgroundColor.b=0.8", "backgroundColor.a=1")
    component("CanvasRoot/SaveButton", "Selectable", "colorFadeSeconds=0")
    component("CanvasRoot/SaveButton", "Button")
    run(args.cli, "script-new", "--project", project, "--name", "StorageFlow", "--language", "lua")
    shutil.copy2(Path(__file__).with_name("fixtures") / "StorageFlow.lua",
        project / "Assets/Scripts/StorageFlow.lua")
    run(args.cli, "scene-attach", "--project", project, "--scene", "Assets/Scenes/Main.21kbscene",
        "--node", "SaveButton", "--script", "/Game/Scripts/StorageFlow.lua")
    run(args.cooker, "--project", project, "--target", "WebGL.wasm32",
        "--output", web / "Game.kbpack", "--engine-root", args.engine_root,
        "--shaderc", args.shaderc, "--cache", root / "CookCache")
    for name in ["Game.html", "Game.js", "Game.wasm"]:
        shutil.copy2(args.directory / name, web / name)
    return web


class StorageScenario(TouchScenario):
    marker = "storage-flow"

    def ready(self):
        value = self.wait("native storage ready after eight game ticks", lambda state: state["ready"])
        self.client.evaluate("Object.assign(document.querySelector('#canvas').style,"
            "{width:'800px',height:'600px',margin:'0',display:'block'})")
        self.paint()
        self.geometry()
        return value

    def click_save(self):
        self.geometry()
        for kind, buttons in [("mousePressed", 1), ("mouseReleased", 0)]:
            self.client.call("Input.dispatchMouseEvent", type=kind,
                x=self.layout["left"] + 140, y=self.layout["top"] + 160,
                button="left", buttons=buttons, clickCount=1)
            self.paint()

    def blue_button(self, label):
        pixels = self.picture(label, [[60, 100]])
        require(pixels[0][2] > pixels[0][0] + 50, "Native save button did not render: " + str(pixels))


class BrowserSession:
    def __init__(self, args, root, profile, report, origin, label, script="", route=""):
        self.args, self.root, self.profile, self.report = args, root, profile, report
        self.origin, self.label, self.script, self.route = origin, label, script, route
        self.client = self.process = self.scenario = self.log = None

    def __enter__(self):
        try:
            self.log = (self.root / (self.label + "-browser.log")).open("wb")
            self.process = subprocess.Popen([str(self.args.browser), "--headless=new",
                "--disable-extensions", "--disable-background-networking", "--disable-component-update",
                "--disable-default-apps", "--no-first-run", "--no-default-browser-check",
                "--remote-debugging-port=0", f"--user-data-dir={self.profile}",
                *self.args.browser_arg, "about:blank"], stdout=self.log, stderr=subprocess.STDOUT,
                creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
            deadline = time.monotonic() + 30
            while True:
                try:
                    port = int((self.profile / "DevToolsActivePort").read_text().splitlines()[0])
                    with urllib.request.urlopen(f"http://127.0.0.1:{port}/json/list", timeout=2) as response:
                        pages = json.load(response)
                    target = next(page for page in pages if page.get("url") == "about:blank")
                    break
                except (OSError, ValueError, StopIteration, IndexError):
                    require(self.process.poll() is None, "Owned browser exited before startup")
                    require(time.monotonic() < deadline, "Owned browser startup timed out")
                    time.sleep(0.05)
            self.client = RuntimeClient(target["webSocketDebuggerUrl"])
            self.client.call("Runtime.enable")
            self.client.call("Page.enable")
            self.client.call("Page.bringToFront")
            self.report.setdefault("browsers", []).append(self.client.call("Browser.getVersion"))
            self.client.call("Emulation.setDeviceMetricsOverride", width=1100, height=900,
                deviceScaleFactor=1, mobile=False)
            self.client.call("Page.addScriptToEvaluateOnNewDocument", source=OBSERVER + self.script)
            self.client.call("Page.navigate", url=self.origin + self.route + "/Game.html")
            self.scenario = StorageScenario(self.client, self.root, self.report)
            return self.scenario
        except BaseException:
            self.__exit__(None, None, None)
            raise

    def __exit__(self, *_):
        if self.client is not None:
            try:
                (self.root / (self.label + "-states.json")).write_text(
                    json.dumps(self.scenario.states(), indent=2), encoding="utf-8")
            except Exception:
                pass
            (self.root / (self.label + "-events.json")).write_text(
                json.dumps(self.client.events, indent=2), encoding="utf-8")
            try:
                self.client.call("Browser.close")
            except Exception:
                pass
            self.client.close()
        if self.process is not None:
            try:
                self.process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.process.terminate()
                self.process.wait(timeout=5)
        if self.log is not None:
            self.log.close()


def run_cases(args, root, report, origin):
    profile = root / "profile"
    with BrowserSession(args, root, profile, report, origin, "write") as case:
        initial = case.ready()
        require(not initial["loaded"] and not initial["settingsLoaded"] and initial["counter"] == 0,
            "A fresh profile unexpectedly contained saves")
        case.blue_button("before-write")
        cursor = len(case.states())
        case.click_save()
        case.wait("native writes are dirty before flush", lambda s: s["status"] == "Dirty" and s["written"] and s["settingsWritten"], cursor)
        case.wait("flush starts asynchronously", lambda s: s["status"] == "Pending" and s["started"], cursor)
        case.wait("latest native save and settings commit", lambda s: s["counter"] == 1 and s["status"] == "Clean" and s["note"] == "latest", cursor)
        report["graphics"] = case.graphics()

    with BrowserSession(args, root, profile, report, origin, "reopen") as case:
        value = case.ready()
        require(value["loaded"] and value["settingsLoaded"] and value["counter"] == 1 and
            value["note"] == "latest" and value["label"] == "Zażółć 1",
            "Native save/settings did not survive closing and reopening the browser: " + str(value))
        report["observations"].append(dict(check="same owned profile survives browser restart", state=value))
        case.blue_button("after-reopen")
        case.client.call("Storage.overrideQuotaForOrigin", origin=origin, quotaSize=1)
        cursor = len(case.states())
        case.click_save()
        failed = case.wait("real browser quota refuses persistence", lambda s: s["status"] == "Failed", cursor)
        require(failed["written"] and failed["settingsWritten"] and "QuotaExceededError" in failed["diagnostic"],
            "Quota fault did not reach the actual browser transaction: " + str(failed))
        require(not any(s["counter"] == 2 and s["status"] == "Clean" for s in case.states()[cursor:]),
            "A failed quota flush reported Clean for the attempted revision")
        case.graphics()
        case.client.call("Storage.overrideQuotaForOrigin", origin=origin)

    with BrowserSession(args, root, profile, report, origin, "abort") as case:
        value = case.ready()
        require(value["loaded"] and value["counter"] == 1 and value["note"] == "latest",
            "Quota failure replaced the previously committed save: " + str(value))
        report["observations"].append(dict(check="quota failure retains previous committed save", state=value))
        case.client.evaluate("window.kbAbortedTransactions=0; window.kbOriginalTransaction=IDBDatabase.prototype.transaction;"
            "IDBDatabase.prototype.transaction=function(...args){const tx=kbOriginalTransaction.apply(this,args);"
            "if(args[1]==='readwrite'){++kbAbortedTransactions;queueMicrotask(()=>tx.abort());}return tx;}")
        cursor = len(case.states())
        case.click_save()
        failed = case.wait("interrupted real IndexedDB transaction is reported", lambda s: s["status"] == "Failed", cursor)
        require(case.client.evaluate("kbAbortedTransactions") > 0 and "AbortError" in failed["diagnostic"],
            "The transaction interruption was not observed")
        require(not any(s["counter"] == 2 and s["status"] == "Clean" for s in case.states()[cursor:]),
            "An aborted flush reported Clean for the attempted revision")
        case.graphics()

    with BrowserSession(args, root, profile, report, origin, "after-abort") as case:
        value = case.ready()
        require(value["loaded"] and value["settingsLoaded"] and value["counter"] == 1 and value["label"] == "Zażółć 1",
            "An aborted transaction damaged the previous save/settings: " + str(value))
        report["observations"].append(dict(check="interrupted transaction preserves previous native save/settings", state=value))
        case.graphics()

    with BrowserSession(args, root, profile, report, origin, "other-directory", route="/other") as case:
        value = case.ready()
        require(not value["loaded"] and not value["settingsLoaded"] and value["counter"] == 0,
            "Exports in different URL directories share save slots")
        report["observations"].append(dict(check="URL directories isolate game storage", state=value))

    denial = "IDBFactory.prototype.open=function(){throw new DOMException('Storage denied by local test','SecurityError');};"
    with BrowserSession(args, root, root / "denied-profile", report, origin, "denied", denial) as case:
        deadline = time.monotonic() + 15
        while True:
            state = case.client.evaluate("({data:{...document.documentElement.dataset},"
                "diagnostic:Module.kbUserStorage?.diagnostic})")
            if state["data"].get("kbError"):
                break
            require(time.monotonic() < deadline, "Storage denial did not fail startup")
            time.sleep(0.03)
        require(state["data"]["kbError"] == "storage-init" and
            state["data"].get("kbFirstFrame") != "true" and "SecurityError" in state["diagnostic"],
            "Storage denial silently started a volatile game: " + str(state))
        report["observations"].append(dict(check="storage denial fails startup explicitly", state=state))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ["cli", "cooker", "engine-root", "shaderc", "directory", "browser", "output"]:
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--browser-arg", action="append", default=[])
    args = parser.parse_args()
    if hasattr(ctypes, "windll"):
        ctypes.windll.kernel32.SetErrorMode(32771)
    args.output.mkdir(parents=True, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix="GameStorage", dir=args.output.resolve()))
    report = dict(schema="21kb.web-game-storage-tests/v1", result="FAIL", observations=[], images=[])
    server = thread = None
    try:
        web = author_fixture(args, root)
        report["artifacts"] = {name: hashlib.sha256((web / name).read_bytes()).hexdigest()
            for name in ["Game.html", "Game.js", "Game.wasm", "Game.kbpack"]}

        class Quiet(http.server.SimpleHTTPRequestHandler):
            def log_message(self, *_):
                pass

            def translate_path(self, path):
                # Serve the same final export from two deployment directories.
                return super().translate_path(path.removeprefix("/other") if path.startswith("/other/") else path)

        server = http.server.ThreadingHTTPServer(("127.0.0.1", 0),
            lambda *values: Quiet(*values, directory=str(web)))
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        run_cases(args, root, report, f"http://127.0.0.1:{server.server_port}")
        report["result"] = "PASS"
    except Exception as error:
        report.update(error=str(error), traceback=traceback.format_exc())
    finally:
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

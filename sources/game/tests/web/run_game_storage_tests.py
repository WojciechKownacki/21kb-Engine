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

from run_game_touch_tests import OBSERVER, RuntimeClient, TouchScenario, graphics_contexts, require


def author_fixture(args, root, *, script_name="StorageFlow", script_source=None, configure=None):
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
    run(args.cli, "script-new", "--project", project, "--name", script_name, "--language", "lua")
    shutil.copy2(script_source or Path(__file__).with_name("fixtures") / "StorageFlow.lua",
        project / "Assets/Scripts" / (script_name + ".lua"))
    run(args.cli, "scene-attach", "--project", project, "--scene", "Assets/Scenes/Main.21kbscene",
        "--node", "SaveButton", "--script", "/Game/Scripts/" + script_name + ".lua")
    if configure is not None:
        configure(run, component, project)
    run(args.cooker, "--project", project, "--target", "WebGL.wasm32",
        "--output", web / "Game.kbpack", "--engine-root", args.engine_root,
        "--shaderc", args.shaderc, "--cache", args.cook_cache or root / "CookCache")
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
    def __init__(self, args, root, profile, report, origin, label, script="", route="", scenario_type=StorageScenario):
        self.args, self.root, self.profile, self.report = args, root, profile, report
        self.origin, self.label, self.script, self.route = origin, label, script, route
        self.scenario_type = scenario_type
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
            self.scenario = self.scenario_type(self.client, self.root, self.report)
            return self.scenario
        except BaseException as error:
            self.__exit__(type(error), error, error.__traceback__)
            raise

    def __exit__(self, exception_type, *_):
        cleanup_errors = []
        if self.client is not None:
            try:
                (self.root / (self.label + "-states.json")).write_text(
                    json.dumps(self.scenario.states(), indent=2), encoding="utf-8")
            except Exception:
                pass
            self.report.setdefault("graphicsContexts", []).append(dict(session=self.label,
                contexts=graphics_contexts(self.client.events)))
            (self.root / (self.label + "-events.json")).write_text(
                json.dumps(self.client.events, indent=2), encoding="utf-8")
            try:
                self.client.call("Browser.close")
            except Exception:
                pass
            try:
                self.client.close()
            except Exception as error:
                cleanup_errors.append(str(error))
        if self.process is not None:
            try:
                self.process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                try:
                    self.process.terminate()
                    self.process.wait(timeout=5)
                except Exception as error:
                    cleanup_errors.append(str(error))
        if self.log is not None:
            self.log.close()
        if cleanup_errors:
            self.report.setdefault("cleanupErrors", []).append(dict(session=self.label, errors=cleanup_errors))
            if exception_type is None:
                raise RuntimeError("Owned browser cleanup failed: " + str(cleanup_errors))


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
        case.client.call("Storage.overrideQuotaForOrigin", origin=origin, quotaSize=1)
        cursor = len(case.states())
        case.click_save()
        case.wait("retry preparation reaches real quota failure", lambda s: s["status"] == "Failed" and s["counter"] == 2, cursor)
        revision = case.client.evaluate("Module.kbUserStorage.revision")
        case.client.call("Storage.overrideQuotaForOrigin", origin=origin)
        cursor = len(case.states())
        case.click_save()
        case.wait("explicit flush retry commits existing working data", lambda s: s["status"] == "Clean" and s["counter"] == 2 and s["started"], cursor)
        require(case.client.evaluate("Module.kbUserStorage.revision") == revision,
            "The retry rewrote working files instead of flushing the failed revision")

    with BrowserSession(args, root, profile, report, origin, "after-retry") as case:
        value = case.ready()
        require(value["loaded"] and value["settingsLoaded"] and value["counter"] == 2 and
            value["note"] == "latest" and value["label"] == "Zażółć 2",
            "Explicit retry did not survive browser restart: " + str(value))
        report["observations"].append(dict(check="explicit retry survives browser restart", state=value))
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

    timeout_profile = root / "timeout-profile"
    with BrowserSession(args, root, timeout_profile, report, origin, "timeout") as case:
        case.ready()
        # Keep a real transaction active in this profile's database. IndexedDB
        # queues the player's synchronization behind it; no fake save result
        # or callback is supplied and the production deadline stays unchanged.
        case.client.call("Runtime.evaluate", awaitPromise=True, expression="""
            new Promise((resolve,reject) => {
                const request=indexedDB.open(Module.kbUserStorage.root);
                request.onerror=()=>reject(request.error);
                request.onsuccess=()=>{
                    const database=request.result;
                    const transaction=database.transaction('FILE_DATA','readwrite');
                    const store=transaction.objectStore('FILE_DATA');
                    window.kbStorageHold={active:true,reads:0,database,transaction};
                    const next=()=>{
                        const query=store.get('__21kb_local_test_hold__');
                        query.onsuccess=()=>{++kbStorageHold.reads;if(kbStorageHold.active)next();};
                    };
                    next();
                    window.kbCompletedStorageWrites=0;
                    const original=IDBDatabase.prototype.transaction;
                    IDBDatabase.prototype.transaction=function(...args){
                        const tx=original.apply(this,args);
                        if(this.name===Module.kbUserStorage.root && args[1]==='readwrite')
                            tx.addEventListener('complete',()=>++kbCompletedStorageWrites);
                        return tx;
                    };
                    resolve(true);
                };
            })
        """)
        cursor = len(case.states())
        started = time.monotonic()
        case.click_save()
        pending = case.wait("real queued storage transaction remains pending", lambda s: s["status"] == "Pending" and s["counter"] == 1, cursor)
        require(pending["snapshotNote"] == "first" and pending["note"] == "latest",
            "The queued flush and subsequent unconfirmed working edit were not distinguished")
        deadline = started + 18
        while True:
            states = case.states()[cursor:]
            failed = next((s for s in reversed(states) if s["status"] == "Failed"), None)
            if failed is not None:
                break
            require(time.monotonic() < deadline, "The unchanged 15-second storage deadline did not report failure")
            time.sleep(0.05)
        elapsed = time.monotonic() - started
        require("TimeoutError" in failed["diagnostic"] and 14.8 <= elapsed < 18,
            "The actual storage timeout was not observed: " + str((elapsed, failed)))
        require(not any(s["counter"] == 1 and s["status"] == "Clean" for s in states),
            "Queued writes were reported complete before the storage transaction finished")
        report["observations"].append(dict(check="unchanged storage deadline reports real queued transaction timeout",
            elapsedSeconds=elapsed, state=failed))
        case.click_save()
        refused = case.states()[-1]
        require(not refused["started"] and refused["status"] == "Failed" and refused["counter"] == 1,
            "A timed out transaction allowed an overlapping retry: " + str(refused))
        report["observations"].append(dict(check="timeout refuses overlapping retry", state=refused))
        require(case.client.evaluate("kbStorageHold.reads") > 0, "The real database transaction was not held")
        case.client.evaluate("kbStorageHold.active=false")
        deadline = time.monotonic() + 5
        while not case.client.evaluate("kbCompletedStorageWrites > 0"):
            require(time.monotonic() < deadline, "The queued real transaction did not finish after release")
            time.sleep(0.03)
        case.paint()
        late = case.states()[-1]
        require(late["status"] == "Failed" and "TimeoutError" in late["diagnostic"],
            "A late successful callback replaced the reported timeout: " + str(late))
        report["observations"].append(dict(check="late real commit cannot acknowledge a timed out operation", state=late))
        case.graphics()

    with BrowserSession(args, root, timeout_profile, report, origin, "after-timeout") as case:
        value = case.ready()
        require(value["loaded"] and value["settingsLoaded"] and value["counter"] == 1 and
            value["note"] == pending["snapshotNote"] and value["label"] == "Zażółć 1",
            "The actual late transaction did not leave a valid save: " + str(value))
        report["observations"].append(dict(check="reopening reads the actual late committed data", state=value))
        case.graphics()


def main(*, fixture=author_fixture, scenario=run_cases,
        schema="21kb.web-game-storage-tests/v1", description=__doc__, prefix="GameStorage",
        http_handler_type=http.server.SimpleHTTPRequestHandler, native_tools=True):
    parser = argparse.ArgumentParser(description=description)
    names = ["directory", "browser", "output"]
    if native_tools:
        names = ["cli", "cooker", "engine-root", "shaderc", *names]
    for name in names:
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--browser-arg", action="append", default=[])
    if native_tools:
        parser.add_argument("--cook-cache", type=Path)
    args = parser.parse_args()
    if hasattr(ctypes, "windll"):
        ctypes.windll.kernel32.SetErrorMode(32771)
    args.output.mkdir(parents=True, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix=prefix, dir=args.output.resolve()))
    report = dict(schema=schema, result="FAIL", observations=[], images=[])
    server = thread = None
    try:
        web = fixture(args, root)
        report["artifacts"] = {name: hashlib.sha256((web / name).read_bytes()).hexdigest()
            for name in ["Game.html", "Game.js", "Game.wasm", "Game.kbpack"]}

        class Quiet(http_handler_type):
            def log_message(self, *_):
                pass

            def translate_path(self, path):
                # Serve the same final export from two deployment directories.
                return super().translate_path(path.removeprefix("/other") if path.startswith("/other/") else path)

        server = http.server.ThreadingHTTPServer(("127.0.0.1", 0),
            lambda *values: Quiet(*values, directory=str(web)))
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        scenario(args, root, report, f"http://127.0.0.1:{server.server_port}")
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

"""Verify production WebGPU failure routing into a rendered WebGL package.

The entry page exercises the production preflight before WASM startup. The fallback
is a real previously cooked package, rendered and sealed during this test.
"""
import argparse
import http.server
import json
from pathlib import Path
import shutil
import sys
import tempfile
import threading
import time

from run_game_storage_tests import BrowserSession, StorageScenario
from run_game_touch_tests import require

sys.path.insert(0, str(Path(__file__).resolve().parents[4] / "scripts"))
import package_game


def wait_for_webgl(case):
    deadline = time.monotonic() + 25
    while not case.client.evaluate("location.pathname.endsWith('/webgl/Game.html') && "
            "document.documentElement.dataset.kbBackend==='webgl' && "
            "document.documentElement.dataset.kbFirstFrame==='true'"):
        require(time.monotonic() < deadline, "Graphics failure did not reach a rendered WebGL fallback")
        time.sleep(.05)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("directory", "project", "browser", "output"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--marker", default="storage-flow")
    parser.add_argument("--webgpu-directory", type=Path,
        help="Complete WebGPU export to exercise its actual generated loader and WASM")
    args = parser.parse_args()
    args.project = args.project.resolve(strict=True)
    args.browser_arg = []
    args.target = "WebGPU.wasm32"
    args.output.mkdir(parents=True, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix="WebGpuFallback", dir=args.output.resolve()))
    baseline = root / "baseline"
    baseline.mkdir()
    for name in ("Game.html", "Game.js", "Game.wasm", "Game.kbpack"):
        shutil.copy2(args.directory / name, baseline / name)
    report = dict(result="FAIL", observations=[], images=[], scope="production preflight and WebGL fallback; no WebGPU rendering claim")

    class Scenario(StorageScenario):
        marker = args.marker

        def exception_events(self):
            # A graphics failure may abort the old WASM instance before navigation.
            # Retain those events in the artifact, but validate the new document.
            events = self.client.events
            start = next((index + 1 for index in range(len(events) - 1, -1, -1)
                if events[index].get("method") == "Runtime.executionContextsCleared"), 0)
            return events[start:]

    class Quiet(http.server.SimpleHTTPRequestHandler):
        def log_message(self, *_):
            pass

    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0),
        lambda *values: Quiet(*values, directory=str(root)))
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    origin = f"http://127.0.0.1:{server.server_port}"
    try:
        with BrowserSession(args, root, root / "profile-baseline", report, origin, "baseline",
                route="/baseline", scenario_type=Scenario) as case:
            case.ready()
            case.blue_button("baseline-webgl")
            require(case.client.evaluate("document.documentElement.dataset.kbBackend==='webgl'"),
                "Baseline did not render with WebGL")
        receipt = dict(target="WebGL.wasm32",
            inputs=dict(projectSha256=package_game._project_source_fingerprint(args.project)),
            product=dict(executableName="Game"))
        package_game.seal_unit(baseline, receipt,
            runtime_first_frame=package_game._first_frame_result("WebGL.wasm32", baseline).receipt_fields())
        fallback = package_game._stage_web_fallback(args, root, baseline)
        if args.webgpu_directory:
            for name in ("Game.html", "Game.js", "Game.wasm", "Game.kbpack"):
                shutil.copy2(args.webgpu_directory / name, root / name)
            entry = root / "Game.html"
            html = entry.read_text(encoding="utf-8")
            entry.write_text(package_game._configure_web_fallback(html, fallback), encoding="utf-8")
            report["scope"] = "compiled WebGPU export failure routing into verified WebGL package"
        else:
            source = Path(__file__).resolve().parents[2] / "src/web/WebGpuSupport.js"
            shutil.copy2(source, root / "WebGpuSupport.js")
            (root / "Game.html").write_text('<!doctype html><script>var Module={kbWebGlFallback:' +
                json.dumps(fallback) + '};</script><script src="WebGpuSupport.js"></script>' +
                '<script>Module.kbPrepareGraphics();</script>', encoding="utf-8")
        failures = [
            ("missing", "Object.defineProperty(navigator,'gpu',{value:undefined});"),
            ("no-adapter", "Object.defineProperty(navigator,'gpu',{value:{requestAdapter:async()=>null}});"),
            ("rejected-adapter", "Object.defineProperty(navigator,'gpu',{value:{requestAdapter:async()=>{throw new Error('denied')}}});"),
        ]
        if args.webgpu_directory:
            failures.append(("device-rejected",
                "GPUAdapter.prototype.requestDevice=async()=>{throw new Error('device denied')};"))
        for label, script in failures:
            with BrowserSession(args, root, root / ("profile-" + label), report, origin, label, script,
                    scenario_type=Scenario) as case:
                wait_for_webgl(case)
                case.ready()
                case.blue_button(label + "-fallback-webgl")
                state = case.client.evaluate("({url:location.href,backend:document.documentElement.dataset.kbBackend})")
                require(state["backend"] == "webgl" and "/webgl/Game.html?kb-fallback=" in state["url"],
                    "Failure did not navigate to the rendered fallback: " + str(state))
                report["observations"].append(dict(check=label + " reaches actual WebGL", state=state))
        if args.webgpu_directory:
            with BrowserSession(args, root, root / "profile-device-loss", report, origin,
                    "device-loss", scenario_type=Scenario) as case:
                case.ready()
                require(case.client.evaluate("document.documentElement.dataset.kbBackend==='webgpu'"),
                    "Device-loss case did not start with WebGPU")
                case.client.call("Browser.crashGpuProcess")
                wait_for_webgl(case)
                case.blue_button("device-loss-fallback-webgl")
                state = case.client.evaluate("({url:location.href,backend:document.documentElement.dataset.kbBackend})")
                require(state["backend"] == "webgl" and "kb-fallback=graphics-device-lost" in state["url"],
                    "Device loss did not preserve its fallback reason: " + str(state))
                report["observations"].append(dict(check="device loss reaches actual WebGL", state=state))
        report["result"] = "PASS"
    except Exception as error:
        report["error"] = str(error)
    finally:
        server.shutdown()
        server.server_close()
        thread.join()
        (root / "result.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps(dict(result=report["result"], error=report.get("error"), directory=str(root))))
    return 0 if report["result"] == "PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())

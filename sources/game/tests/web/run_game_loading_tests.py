"""Check real package loading failures, hardware startup and memory observations."""
from pathlib import Path
import os
import time

from run_game_storage_tests import BrowserSession, main
from run_game_storage_delete_tests import author_fixture, verify
from run_game_touch_tests import require


LOAD_OBSERVER = """
window.kbLoadMetrics={firstFrameMs:null,errorMs:null,maximumWasmCapacity:0};
new MutationObserver(()=>{
    const state=document.documentElement?.dataset;
    if(state?.kbFirstFrame==='true' && kbLoadMetrics.firstFrameMs===null)
        kbLoadMetrics.firstFrameMs=performance.now();
    if(state?.kbError && kbLoadMetrics.errorMs===null)
        kbLoadMetrics.errorMs=performance.now();
}).observe(document,{subtree:true,attributes:true,attributeFilter:['data-kb-first-frame','data-kb-error']});
setInterval(()=>{
    if(typeof HEAPU8!=='undefined')
        kbLoadMetrics.maximumWasmCapacity=Math.max(kbLoadMetrics.maximumWasmCapacity,HEAPU8.byteLength);
},20);
"""


def measure(case, label, report, pack_size):
    metrics = case.client.evaluate("({...kbLoadMetrics,readyMs:performance.now(),"
        "wasmCapacity:typeof HEAPU8==='undefined'?null:HEAPU8.byteLength,"
        "resources:performance.getEntriesByType('resource').map(value=>({"
        "name:new URL(value.name).pathname,duration:value.duration,transferSize:value.transferSize,"
        "encodedBodySize:value.encodedBodySize,decodedBodySize:value.decodedBodySize}))})")
    require(metrics["firstFrameMs"] is not None and metrics["wasmCapacity"] and
        metrics["maximumWasmCapacity"] >= metrics["wasmCapacity"],
        "Actual startup/linear memory observations are missing: " + str(metrics))
    packs = [value for value in metrics["resources"] if value["name"].endswith("/Game.kbpack")]
    require(len(packs) == 1 and packs[0]["decodedBodySize"] == pack_size,
        "Resource timing did not observe the actual package download: " + str(packs))
    report.setdefault("measurements", []).append(dict(label=label, metrics=metrics,
        heap=case.client.call("Runtime.getHeapUsage"), graphics=case.graphics()))


def run_cases(args, root, report, origin):
    web = root / "Web"
    pack = (web / "Game.kbpack").read_bytes()
    profile = root / "profile"
    for label in ["cold", "warm"]:
        with BrowserSession(args, root, profile, report, origin, label, LOAD_OBSERVER) as case:
            verify(case.ready(), 0)
            case.blue_button(label + "-native-button")
            measure(case, label, report, len(pack))
            if label == "warm":
                cursor = len(case.states())
                case.client.call("Memory.simulatePressureNotification", level="moderate")
                verify(case.wait("native game progresses after browser pressure notification",
                    lambda value: value["ready"], cursor), 0)
                case.blue_button("after-browser-pressure")
                measure(case, "after-moderate-browser-pressure", report, len(pack))

    for label, payload, code, diagnostic in [
            ("Missing", None, "fetch-http", "HTTP 404"),
            ("Empty", b"", "pack-empty", "asset pack response was empty"),
            ("Truncated", pack[:len(pack) // 2], "pack-mount", "asset pack mount failed"),
            ("Corrupt", bytes([pack[0] ^ 255]) + pack[1:], "pack-mount", "asset pack mount failed")]:
        folder = web / label
        folder.mkdir()
        for name in ["Game.html", "Game.js", "Game.wasm"]:
            os.link(web / name, folder / name)
        if payload is not None:
            (folder / "Game.kbpack").write_bytes(payload)
        with BrowserSession(args, root, profile, report, origin, label, LOAD_OBSERVER,
                route="/" + label) as case:
            deadline = time.monotonic() + 15
            while True:
                value = case.client.evaluate("({data:{...document.documentElement?.dataset},"
                    "title:document.title,visible:document.querySelector('#status')?.hidden===false,"
                    "message:document.querySelector('#status')?.textContent,"
                    "metrics:window.kbLoadMetrics})")
                if value["data"].get("kbError"):
                    break
                require(time.monotonic() < deadline, "Package startup did not report failure: " + str(value))
                time.sleep(0.03)
            require(value["data"]["kbError"] == code and not value["data"].get("kbFirstFrame") and
                value["metrics"]["firstFrameMs"] is None and value["visible"] and
                value["message"] == "Unable to continue. Reload this page.",
                "A package error did not reach the native startup failure path: " + str(value))
            messages = "\n".join(str(argument.get("value", ""))
                for event in case.client.events if event.get("method") == "Runtime.consoleAPICalled"
                for argument in event["params"]["args"])
            require(diagnostic in messages, "The actual host diagnostic is missing: " + messages[-2000:])
            require(not any(event.get("method") == "Runtime.exceptionThrown" for event in case.client.events),
                "Package failure caused an unhandled browser exception")
            case.paint()
            require(case.client.evaluate("document.documentElement.dataset.kbError") == code,
                "A later frame cleared the package failure")
            report["observations"].append(dict(check="native package startup refuses " + label,
                expected=code, state=value, diagnostic=diagnostic))


if __name__ == "__main__":
    raise SystemExit(main(fixture=author_fixture, scenario=run_cases,
        schema="21kb.web-game-loading/v1", description=__doc__, prefix="GameLoading"))

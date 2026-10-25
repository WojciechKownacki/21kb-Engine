"""Check real WASM startup HTTP, binary and MIME handling in the exported player."""
import http.server
import os
from pathlib import Path
import re
import time

from run_game_loading_tests import LOAD_OBSERVER, measure
from run_game_storage_delete_tests import author_fixture, verify
from run_game_storage_tests import BrowserSession, main
from run_game_touch_tests import require


class WasmLoadingHandler(http.server.SimpleHTTPRequestHandler):
    transmissions = []

    def do_GET(self):
        if self.path not in ["/Disconnected/Game.wasm", "/WrongMime/Game.wasm"]:
            super().do_GET()
            return
        data = Path(self.translate_path(self.path)).read_bytes()
        disconnected = self.path.startswith("/Disconnected/")
        self.send_response(200)
        self.send_header("Content-Type", "application/wasm" if disconnected else "application/octet-stream")
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        sent = self.wfile.write(data[:len(data) // 2] if disconnected else data)
        self.wfile.flush()
        self.close_connection = True
        WasmLoadingHandler.transmissions.append(dict(route=self.path, declaredBytes=len(data), sentBytes=sent))


def run_cases(args, root, report, origin, *, failures_only=False):
    WasmLoadingHandler.transmissions = []
    web = root / "Web"
    engine = web / "Engine"
    engine.mkdir()
    for name in ["Game.js", "Game.wasm"]:
        os.link(web / name, engine / name)
    for label, code, diagnostic in [("Missing", "wasm-http", "HTTP 404"),
            ("Corrupt", "wasm-init", "WASM startup failed"),
            ("Disconnected", "wasm-fetch", "WASM startup failed"), ("WrongMime", None, None),
            ("Relocated", None, None)]:
        if failures_only and code is None:
            continue
        folder = web / label
        folder.mkdir()
        if label == "Relocated":
            html = (web / "Game.html").read_text(encoding="utf-8")
            html, replacements = re.subn(r'''(\bsrc\s*=\s*)(?:"Game\.js"|'Game\.js'|Game\.js(?=[\s>]))''',
                r'\1"../Engine/Game.js"', html)
            require(replacements == 1, "The exported script URL was not found")
            (folder / "Game.html").write_text(html, encoding="utf-8")
            os.link(web / "Game.kbpack", folder / "Game.kbpack")
        else:
            for name in ["Game.html", "Game.js", "Game.kbpack"]:
                os.link(web / name, folder / name)
        if label == "Corrupt":
            (folder / "Game.wasm").write_bytes(b"Invalid WASM fixture")
        elif label not in ["Missing", "Relocated"]:
            os.link(web / "Game.wasm", folder / "Game.wasm")
        with BrowserSession(args, root, root / "profile", report, origin, label, LOAD_OBSERVER,
                route="/" + label) as case:
            if code is None:
                verify(case.ready(), 0)
                case.blue_button("wasm-" + label.lower())
                measure(case, label, report, (web / "Game.kbpack").stat().st_size)
                report["observations"].append(dict(check="WASM startup renders", variant=label))
                continue
            deadline = time.monotonic() + 15
            while True:
                state = case.client.evaluate("({data:{...document.documentElement?.dataset},metrics:window.kbLoadMetrics,"
                    "visible:document.querySelector('#status')?.hidden===false,hasGraphics:!!window.kbTouchGraphics,"
                    "packRequested:performance.getEntriesByType('resource').some(value=>"
                    "new URL(value.name).pathname.endsWith('/Game.kbpack'))})")
                if state["data"].get("kbError"):
                    break
                require(time.monotonic() < deadline, "WASM startup did not report a failure: " + str(state))
                time.sleep(0.03)
            require(state["data"]["kbError"] == code and state["visible"] and
                state["metrics"]["firstFrameMs"] is None and not state["data"].get("kbFirstFrame") and
                not state["hasGraphics"] and not state["packRequested"],
                "WASM failure did not stop before native startup: " + str(state))
            messages = "\n".join(str(argument.get("value", ""))
                for event in case.client.events if event.get("method") == "Runtime.consoleAPICalled"
                for argument in event["params"]["args"])
            require(diagnostic in messages, "WASM failure diagnostic is missing: " + messages[-2000:])
            case.paint()
            require(case.client.evaluate("document.documentElement.dataset.kbError") == code and
                not any(event.get("method") == "Runtime.exceptionThrown" for event in case.client.events),
                "WASM failure changed its cause or produced an unhandled exception")
            report["observations"].append(dict(check="WASM startup refuses " + label, state=state))
    report["httpTransfers"] = list(WasmLoadingHandler.transmissions)
    require(len(report["httpTransfers"]) == (1 if failures_only else 2), "An actual controlled WASM HTTP body is missing")
    partial = next(value for value in report["httpTransfers"] if value["route"].startswith("/Disconnected/"))
    require(0 < partial["sentBytes"] < partial["declaredBytes"], "The WASM connection was not interrupted mid-body")


if __name__ == "__main__":
    raise SystemExit(main(fixture=author_fixture, scenario=run_cases, http_handler_type=WasmLoadingHandler,
        schema="21kb.web-game-wasm-loading/v1", description=__doc__, prefix="WasmLoading"))

"""Verify bounded package streaming with actual HTTP bodies and GPU readback."""
from pathlib import Path
import gzip
import http.server
import os
import time

from run_game_content_loading_tests import author_fixture, run_cases as content_cases, verify_content
from run_game_loading_tests import LOAD_OBSERVER
from run_game_storage_tests import BrowserSession, main
from run_game_touch_tests import require


class StreamedLoadingHandler(http.server.SimpleHTTPRequestHandler):
    transmissions = []

    def do_GET(self):
        if self.path not in ["/UnknownLength/Game.kbpack", "/Gzip/Game.kbpack", "/Oversized/Game.kbpack"]:
            super().do_GET()
            return
        data = Path(self.translate_path(self.path)).read_bytes()
        self.send_response(200)
        self.send_header("Content-Type", "application/octet-stream")
        self.send_header("Cache-Control", "no-store")
        if self.path.startswith("/Gzip/"):
            body = gzip.compress(data, compresslevel=1, mtime=0)
            self.send_header("Content-Encoding", "gzip")
            self.send_header("Content-Length", str(len(body)))
        elif self.path.startswith("/Oversized/"):
            # Check the bridge before any 32-bit truncation or large allocation.
            body = b""
            self.send_header("Content-Length", str(2**32 + len(data)))
        else:
            body = data
        self.end_headers()
        self.wfile.write(body)
        self.wfile.flush()
        self.close_connection = True
        StreamedLoadingHandler.transmissions.append(dict(route=self.path,
            decodedBytes=len(data), sentBytes=len(body),
            declaredBytes=2**32 + len(data) if self.path.startswith("/Oversized/") else
                len(body) if self.path.startswith("/Gzip/") else None))


def verify_retained_buffers(measurement, pack_size):
    retained = measurement["retainedHeapAfterGc"]["backingStorageSize"]
    # Leave room for browser/UI data, but detect retention of a complete response.
    require(retained < pack_size // 2,
        "JavaScript retains too much of the package after GC: " + str(dict(
            label=measurement["label"], retainedBytes=retained, packageBytes=pack_size)))


def run_cases(args, root, report, origin):
    StreamedLoadingHandler.transmissions = []
    content_cases(args, root, report, origin)
    web = root / "Web"
    pack_size = (web / "Game.kbpack").stat().st_size
    report["retainedBufferBudgetBytes"] = pack_size // 2
    for measurement in report["measurements"]:
        verify_retained_buffers(measurement, pack_size)
    for label in ["UnknownLength", "Gzip", "Oversized"]:
        folder = web / label
        folder.mkdir()
        for name in ["Game.html", "Game.js", "Game.wasm", "Game.kbpack"]:
            os.link(web / name, folder / name)
        with BrowserSession(args, root, root / "profile", report, origin, label, LOAD_OBSERVER,
                route="/" + label) as case:
            if label != "Oversized":
                verify_content(case, label, report, pack_size)
                verify_retained_buffers(report["measurements"][-1], pack_size)
                continue
            deadline = time.monotonic() + 15
            while True:
                value = case.client.evaluate("({data:{...document.documentElement?.dataset},"
                    "visible:document.querySelector('#status')?.hidden===false,"
                    "message:document.querySelector('#status')?.textContent,metrics:window.kbLoadMetrics})")
                if value["data"].get("kbError"):
                    break
                require(time.monotonic() < deadline, "Oversized response did not reach native failure: " + str(value))
                time.sleep(0.03)
            require(value["data"]["kbError"] == "pack-size" and not value["data"].get("kbFirstFrame") and
                value["metrics"]["firstFrameMs"] is None and value["visible"] and
                value["message"] == "Unable to continue. Reload this page.",
                "Oversized response did not fail before native rendering: " + str(value))
            messages = "\n".join(str(argument.get("value", ""))
                for event in case.client.events if event.get("method") == "Runtime.consoleAPICalled"
                for argument in event["params"]["args"])
            require("in-memory package size limit" in messages, "Missing native size-limit diagnostic")
            case.paint()
            require(case.client.evaluate("document.documentElement.dataset.kbError") == "pack-size" and
                not any(event.get("method") == "Runtime.exceptionThrown" for event in case.client.events),
                "Cancelling an oversized response changed its failure or threw an unhandled exception")
            report["observations"].append(dict(check="oversized declared length refused before allocation", state=value))
    report["httpTransfers"] = list(StreamedLoadingHandler.transmissions)
    require(len(report["httpTransfers"]) == 3, "An actual controlled HTTP response is missing")
    for item in report["httpTransfers"]:
        if item["route"].startswith("/Oversized/"):
            require(item["sentBytes"] == 0 and item["declaredBytes"] > 2**32,
                "Oversize coverage sent a large body or did not exercise a wide length")
        elif item["route"].startswith("/Gzip/"):
            require(item["sentBytes"] != pack_size and item["decodedBytes"] == pack_size,
                "HTTP compression did not change the transmitted byte length")
        else:
            require(item["declaredBytes"] is None and item["sentBytes"] == pack_size,
                "Unknown-length response did not send the complete package")


if __name__ == "__main__":
    raise SystemExit(main(fixture=author_fixture, scenario=run_cases,
        schema="21kb.web-game-streamed-loading/v1", description=__doc__, prefix="StreamedLoading",
        http_handler_type=StreamedLoadingHandler))

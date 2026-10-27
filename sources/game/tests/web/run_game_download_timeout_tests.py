"""Check actual idle download expiry, active progress and reload in the Web player."""
import http.server
import os
from pathlib import Path
import threading
import time

from run_game_loading_tests import LOAD_OBSERVER, measure
from run_game_storage_tests import BrowserSession, main
from run_game_storage_delete_tests import author_fixture, verify
from run_game_touch_tests import require


class TimeoutHandler(http.server.SimpleHTTPRequestHandler):
    resource_name = "Game.kbpack"
    stop = threading.Event()
    release_stall = threading.Event()
    stalled = True
    transfers = []

    def do_GET(self):
        stalled = self.path == "/Stalled/" + self.resource_name and TimeoutHandler.stalled
        active = self.path == "/Progressing/" + self.resource_name
        if not stalled and not active:
            return super().do_GET()
        data = Path(self.translate_path(self.path)).read_bytes()
        record = dict(route=self.path, declaredBytes=len(data), chunks=[], complete=False)
        TimeoutHandler.transfers.append(record)
        started = time.monotonic()

        def send(chunk):
            count = self.wfile.write(chunk)
            self.wfile.flush()
            record["chunks"].append(dict(bytes=count, elapsedSeconds=time.monotonic() - started))

        try:
            self.send_response(200)
            self.send_header("Content-Type", "application/wasm" if self.resource_name.endswith(".wasm")
                else "application/octet-stream")
            self.send_header("Content-Length", str(len(data)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            quarter = len(data) // 4
            send(data[:quarter])
            if stalled:
                TimeoutHandler.release_stall.wait(45)
            elif not TimeoutHandler.stop.wait(26):
                send(data[quarter:quarter * 2])
                if not TimeoutHandler.stop.wait(6):
                    send(data[quarter * 2:])
        except (BrokenPipeError, ConnectionResetError):
            record["peerClosed"] = True
        finally:
            record["complete"] = True
            self.close_connection = True


def run_cases(args, root, report, origin, *, resource_name="Game.kbpack", error_code="fetch-timeout"):
    TimeoutHandler.stop.clear()
    TimeoutHandler.release_stall.clear()
    TimeoutHandler.stalled = True
    TimeoutHandler.transfers = []
    web = root / "Web"
    pack = (web / "Game.kbpack").read_bytes()
    for route in ["Stalled", "Progressing"]:
        folder = web / route
        folder.mkdir()
        for name in ["Game.html", "Game.js", "Game.wasm", "Game.kbpack"]:
            os.link(web / name, folder / name)
    try:
        with BrowserSession(args, root, root / "stalled-profile", report, origin,
                "stalled", LOAD_OBSERVER, route="/Stalled") as stalled, \
             BrowserSession(args, root, root / "active-profile", report, origin,
                "active", LOAD_OBSERVER, route="/Progressing") as active:
            deadline = time.monotonic() + 42
            while True:
                failed = stalled.client.evaluate("({data:{...document.documentElement?.dataset},"
                    "metrics:window.kbLoadMetrics,visible:document.querySelector('#status')?.hidden===false,"
                    "hasGraphics:!!window.kbTouchGraphics,packRequested:performance.getEntriesByType('resource')"
                    ".some(value=>new URL(value.name).pathname.endsWith('/Game.kbpack'))})")
                if failed["data"].get("kbError"):
                    break
                require(time.monotonic() < deadline, "Idle download did not report a bounded failure")
                time.sleep(0.05)
            require(failed["data"]["kbError"] == error_code and failed["visible"] and
                not failed["data"].get("kbFirstFrame") and failed["metrics"]["firstFrameMs"] is None,
                "Idle download did not stop at its deadline: " + str(failed))
            if resource_name == "Game.wasm":
                require(not failed["hasGraphics"] and not failed["packRequested"] and
                    not failed["data"].get("kbBackend"), "Timed-out WASM startup reached the runtime: " + str(failed))
            stalled.paint()
            require(stalled.client.evaluate("document.documentElement.dataset.kbError") == error_code,
                "Closing the expired fetch replaced its timeout diagnostic")
            report["observations"].append(dict(check="actual stalled body reaches idle timeout", state=failed))
            TimeoutHandler.release_stall.set()

            verify(active.ready(), 0)
            active.blue_button("active-transfer-over-thirty-seconds")
            measure(active, "active-transfer", report, len(pack))
            transfer = next(value for value in TimeoutHandler.transfers if value["route"] == "/Progressing/" + resource_name)
            require(len(transfer["chunks"]) == 3 and transfer["chunks"][-1]["elapsedSeconds"] >= 30 and
                sum(value["bytes"] for value in transfer["chunks"]) == (web / resource_name).stat().st_size,
                "The active response did not exercise progress beyond the idle interval: " + str(transfer))
            report["observations"].append(dict(check="active download longer than idle interval succeeds", transfer=transfer))

            TimeoutHandler.stalled = False
            previous = stalled.client.evaluate("performance.timeOrigin")
            stalled.client.call("Page.reload")
            deadline = time.monotonic() + 10
            while stalled.client.evaluate("performance.timeOrigin") == previous:
                require(time.monotonic() < deadline, "Reload did not replace the failed document")
                time.sleep(0.03)
            verify(stalled.ready(), 0)
            stalled.blue_button("idle-timeout-reload")
            measure(stalled, "after-idle-timeout-reload", report, len(pack))
            cursor = len(stalled.states())
            stalled.click_save()
            saved = stalled.wait("native button commits after idle timeout reload", lambda state:
                state["phase"] == 1 and state["started"] and state["status"] == "Clean", cursor)
            verify(saved, 1)
            report["observations"].append(dict(check="idle timeout reload restores input and persistence", state=saved))
    finally:
        TimeoutHandler.stop.set()
        TimeoutHandler.release_stall.set()
        report["httpTransfers"] = list(TimeoutHandler.transfers)


if __name__ == "__main__":
    raise SystemExit(main(fixture=author_fixture, scenario=run_cases, http_handler_type=TimeoutHandler,
        schema="21kb.web-game-download-timeout/v1", description=__doc__, prefix="DownloadTimeout"))

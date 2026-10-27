"""Verify native save chunks across browser restarts using production IndexedDB storage."""
from __future__ import annotations

import argparse
import http.server
import json
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import threading
import time
import urllib.request

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "game/tests/web"))
from run_input_tests import DevTools


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--browser", type=Path, required=True)
    parser.add_argument("--directory", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    profile = Path(tempfile.mkdtemp(prefix="profile-", dir=args.output.resolve()))

    class Quiet(http.server.SimpleHTTPRequestHandler):
        def log_message(self, *_args):
            pass

    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0),
        lambda *values: Quiet(*values, directory=str(args.directory.resolve())))
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    report = {"result": "FAIL", "phases": []}
    try:
        for phase in ("write", "read"):
            with socket.socket() as reservation:
                reservation.bind(("127.0.0.1", 0))
                port = reservation.getsockname()[1]
            url = f"http://127.0.0.1:{server.server_port}/save_storage_tests.html?phase={phase}"
            client = None
            with (args.output / f"{phase}-browser.log").open("wb") as log:
                process = subprocess.Popen([str(args.browser), "--headless=new", "--no-first-run",
                    "--disable-background-networking", f"--user-data-dir={profile}",
                    f"--remote-debugging-port={port}", url], stdout=log, stderr=log,
                    creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
                try:
                    deadline = time.monotonic() + 90
                    result = None
                    while time.monotonic() < deadline:
                        if process.poll() is not None:
                            raise RuntimeError(f"Browser exited: {process.returncode}")
                        if client is None:
                            try:
                                with urllib.request.urlopen(f"http://127.0.0.1:{port}/json/list", timeout=2) as response:
                                    targets = json.load(response)
                                target = next((item for item in targets if item.get("url") == url), None)
                                if target:
                                    client = DevTools(target["webSocketDebuggerUrl"])
                            except (OSError, StopIteration):
                                pass
                        if client is not None:
                            result = client.evaluate("window.storageResult || null")
                            if result is not None:
                                break
                        time.sleep(0.05)
                    report["phases"].append(result)
                    if not result or not result.get("ok") or result.get("phase") != phase:
                        raise RuntimeError(f"Persistence {phase} failed: {result}")
                finally:
                    if client is not None:
                        try:
                            client.call("Browser.close")
                        except Exception:
                            pass
                        client.close()
                    try:
                        process.wait(timeout=10)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait(timeout=10)
        report["result"] = "PASS"
    finally:
        server.shutdown()
        server.server_close()
        (args.output / "result.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps(report))


if __name__ == "__main__":
    main()

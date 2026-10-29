"""Run the compiled browser transport against the native listener in Chrome."""
import argparse
import functools
import http.server
import json
from pathlib import Path
import subprocess
import sys
import threading
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "game/tests/web"))
from run_game_storage_tests import BrowserSession


class Scenario:
    def __init__(self, client, root, report):
        self.client = client

    def states(self):
        return []

    def wait_value(self, expression, expected):
        deadline = time.monotonic() + 10
        while True:
            value = self.client.evaluate(expression)
            if value == expected:
                return
            assert value in (None, False, 1), (expression, value)
            assert time.monotonic() < deadline, (expression, value)
            time.sleep(0.01)


def run(args):
    root = args.output.resolve()
    root.mkdir(parents=True, exist_ok=True)
    stop = root / "stop"
    stop.unlink(missing_ok=True)
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0),
        functools.partial(http.server.SimpleHTTPRequestHandler, directory=str(args.directory.resolve())))
    origin = f"http://localhost:{server.server_port}"
    threading.Thread(target=server.serve_forever, daemon=True).start()
    report = {}
    with (root / "server.log").open("w") as log:
        process = subprocess.Popen([str(args.harness.resolve()), str(stop), origin],
            stdout=subprocess.PIPE, stderr=log, text=True)
        try:
            port, _ = map(int, process.stdout.readline().split())
            with BrowserSession(args, root, root / "profile", report, origin, "transport",
                                scenario_type=Scenario) as scenario:
                scenario.wait_value("window.testSocketReady", True)
                client = scenario.client
                client.evaluate("window.testSocketUrl='ws://['")
                assert client.evaluate("Module._OpenSocket()") == 0
                client.evaluate(f"window.testSocketUrl='ws://127.0.0.1:{port}/21kb'")
                for seed in range(3):
                    assert client.evaluate("Module._OpenSocket()") == 1
                    assert client.evaluate("Module._SendPacket(1201,0)") == 2
                    for size in (0, 17, 1200):
                        scenario.wait_value(f"Module._SendPacket({size},{seed})", 0)
                        scenario.wait_value(f"Module._CheckPacket({size},{seed})", 0)
                    client.evaluate("Module._CloseSocket()")
            stop.touch()
            assert process.wait(timeout=5) == 0
            report.update(result="PASS", checks=["compiled browser adapter", "invalid URL",
                "empty and maximum datagrams", "oversize send", "reconnect", "shutdown"])
        finally:
            stop.touch()
            if process.poll() is None:
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
            server.shutdown()
            server.server_close()
    (root / "result.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report))


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--harness", type=Path, required=True)
    parser.add_argument("--directory", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--browser", type=Path, required=True)
    parser.add_argument("--browser-arg", action="append", default=[])
    run(parser.parse_args())

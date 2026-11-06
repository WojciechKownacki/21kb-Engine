"""Cook a final Web export and exercise replication, RPC and server restart."""
import argparse
import functools
import http.server
import json
from pathlib import Path
import shutil
import subprocess
import threading
import time

from run_game_storage_tests import BrowserSession, author_fixture
from run_game_touch_tests import TouchScenario


class NetworkScenario(TouchScenario):
    marker = "network-flow"


def prepare(args, root):
    def configure(run, component, project):
        run(args.cli, "entity", "add", "--project", project, "--name", "Net_Object")
    author_fixture(args, root, script_name="NetworkFlow",
        script_source=Path(__file__).with_name("fixtures") / "NetworkFlow.lua", configure=configure)


def runtime(args, root):
    web = root / "Web"
    for name in ("Game.html", "Game.js", "Game.wasm"):
        shutil.copy2(args.directory / name, web / name)
    server_http = http.server.ThreadingHTTPServer(("127.0.0.1", 0),
        functools.partial(http.server.SimpleHTTPRequestHandler, directory=str(web)))
    origin = f"http://localhost:{server_http.server_port}"
    threading.Thread(target=server_http.serve_forever, daemon=True).start()
    key = "31" * 32
    keys = root / "keys.txt"
    keys.write_text("7 " + key + "\n")
    stop = root / "stop"
    status = root / "status.json"
    report = {"observations": []}
    process = None
    logs = []

    def start(port, tag):
        stop.unlink(missing_ok=True)
        status.unlink(missing_ok=True)
        log = (root / (tag + ".log")).open("w")
        logs.append(log)
        process = subprocess.Popen([str(args.server.resolve()), "--project", str(root / "Sample"),
            "--keys", str(keys), "--bind", "127.0.0.1", "--port", "0", "--websocket-port", str(port),
            "--websocket-origin", origin, "--replicate", "prefix:Net_", "--status-file", str(status),
            "--stop-file", str(stop), "--max-seconds", "90"], stdout=log, stderr=subprocess.STDOUT)
        deadline = time.monotonic() + 15
        while True:
            assert process.poll() is None, "server failed to start; inspect " + tag
            try:
                state = json.loads(status.read_text())
                if state.get("websocket_port"):
                    return process, state["websocket_port"]
            except (OSError, ValueError):
                pass
            assert time.monotonic() < deadline, "server startup timed out"
            time.sleep(0.05)

    try:
        process, port = start(0, "server-first")
        config = {"url": f"ws://127.0.0.1:{port}/21kb", "clientId": "7", "key": key,
                  "reconnectAttempts": 3}
        with BrowserSession(args, root, root / "profile", report, origin, "network",
                script="globalThis.kbNetwork=" + json.dumps(config) + ";", scenario_type=NetworkScenario) as case:
            case.wait("first replicated entity and RPC", lambda s: s["spawned"] == 1 and s["replies"] == 1)
            stop.touch()
            assert process.wait(timeout=10) == 0
            first = json.loads(status.read_text())
            (root / "server-first-status.json").write_text(json.dumps(first, indent=2))
            process, _ = start(port, "server-restarted")
            case.wait("reconnect restores replica and RPC", lambda s:
                s["connected"] == 2 and s["disconnected"] == 1 and s["spawned"] == 2 and
                s["despawned"] == 1 and s["replies"] == 2)
            assert case.client.evaluate("document.documentElement.dataset.kbFirstFrame") == "true"
            assert case.client.evaluate("globalThis.kbNetwork === undefined") is True
        stop.touch()
        assert process.wait(timeout=10) == 0
        report["result"] = "PASS"
        report["scope"] = "Final cooked WebGL export, native dedicated server, replication, script RPC, real server restart"
    finally:
        stop.touch()
        if process is not None and process.poll() is None:
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
        for log in logs:
            log.close()
        server_http.shutdown()
        server_http.server_close()
        (root / "result.json").write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    for name in ("cli", "cooker", "engine-root", "shaderc", "cook-cache", "directory", "server", "browser", "root"):
        parser.add_argument("--" + name, type=Path, required=name not in ("cook-cache",))
    parser.add_argument("--prepare-only", action="store_true")
    parser.add_argument("--browser-arg", action="append", default=[])
    args = parser.parse_args()
    for name, value in vars(args).items():
        if isinstance(value, Path):
            setattr(args, name, value.resolve())
    args.root = args.root.resolve()
    args.root.mkdir(parents=True, exist_ok=True)
    if args.prepare_only:
        prepare(args, args.root)
    else:
        runtime(args, args.root)

#!/usr/bin/env python3
"""Run the dedicated server with a real project over UDP on Linux and check it end to end.

The script builds a sample project and keys with ``kb_net_bot``, starts ``kb_dedicated_server``
on a free port, connects three bots, checks the status JSON and stops the server with SIGTERM.
"""

import argparse
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time

# Warnings a healthy run may carry: a project without a release manifest has no checked build
# identity, and the kernel may cap the socket buffers below the request. Everything else is a failure.
BENIGN_DIAGNOSTICS = {"VersionUnchecked", "SocketBufferSmall"}


def wait_for(path, seconds, server=None):
    deadline = time.monotonic() + seconds
    while not path.exists():
        if server is not None and server.poll() is not None:
            raise RuntimeError(f"the server stopped before writing {path.name}")
        if time.monotonic() > deadline:
            raise RuntimeError(f"{path.name} did not appear within {seconds:g} s")
        time.sleep(0.05)


def read_status(path):
    return json.loads(path.read_text(encoding="utf-8"))


def check_status(status, entities):
    peak = int(status["clients_peak"])
    if peak < 3:
        raise RuntimeError(f"the server saw {peak} clients, expected at least 3")
    replicated = int(status["replicated_entities"])
    if replicated < entities:
        raise RuntimeError(f"{replicated} entities were replicated, expected at least {entities}")
    codes = {str(entry["code"]) for entry in status["diagnostics"]}
    refused = codes - BENIGN_DIAGNOSTICS
    if refused:
        raise RuntimeError("the server reported diagnostics: " + ", ".join(sorted(refused)))


def run(args):
    if os.name == "nt":
        raise RuntimeError("this smoke test needs POSIX signals; run it on Linux")
    with tempfile.TemporaryDirectory(prefix="21kb_net_linux_smoke_") as temporary:
        root = Path(temporary)
        project, keys = root / "project", root / "keys.txt"
        subprocess.run([str(args.bot), "--make-project", str(project), "--entities", str(args.entities)], check=True)
        subprocess.run([str(args.bot), "--make-keys", str(args.clients), "--keys-out", str(keys)], check=True)

        port_file, stop_file = root / "port.txt", root / "stop"
        status_file, state_file = root / "status.json", root / "state.21kbscene"
        server_log = (root / "server.log").open("w", encoding="utf-8")
        bot_log = (root / "bots.log").open("w", encoding="utf-8")
        server = subprocess.Popen([
            str(args.server), "--project", str(project), "--keys", str(keys),
            "--port", "0", "--port-file", str(port_file), "--stop-file", str(stop_file),
            "--status-file", str(status_file), "--state-file", str(state_file),
            "--max-seconds", "30", "--json",
        ], stdout=subprocess.PIPE, stderr=server_log, text=True)
        bots = None
        try:
            wait_for(port_file, 20, server)
            wait_for(status_file, 20, server)
            port = port_file.read_text(encoding="utf-8").strip()
            bots = subprocess.Popen([
                str(args.bot), "--server", "127.0.0.1:" + port, "--keys", str(keys),
                "--clients", str(args.clients), "--seconds", str(args.seconds + 2), "--csv", str(root / "bots.csv"),
            ], stdout=bot_log, stderr=subprocess.STDOUT)
            # The spec checks the status once the bots have had time to connect and replicate.
            time.sleep(args.seconds)
            if server.poll() is not None:
                raise RuntimeError("the server stopped while the bots were running: " + (root / "server.log").read_text())
            check_status(read_status(status_file), args.entities)
            if bots.wait(timeout=args.seconds + 30) != 0:
                raise RuntimeError("the bots failed: " + (root / "bots.log").read_text())

            server.send_signal(signal.SIGTERM)
            exit_code = server.wait(timeout=3)
            if exit_code != 0:
                raise RuntimeError(f"the server exited with {exit_code} after SIGTERM: " + (root / "server.log").read_text())
            output = server.stdout.read() if server.stdout is not None else ""
            summary = json.loads(output)
            if summary.get("exit_reason") != "signal":
                raise RuntimeError("the server did not report a signal exit: " + output)
            if not summary.get("state_saved") or not state_file.exists():
                raise RuntimeError("the state file was not saved on shutdown")
        finally:
            if bots is not None and bots.poll() is None:
                bots.terminate()
                try:
                    bots.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    bots.kill()
            if server.poll() is None:
                stop_file.touch()
                try:
                    server.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    server.kill()
            server_log.close()
            bot_log.close()
            if server.stdout is not None:
                server.stdout.close()
    print(f"PASS: {args.clients} bots, {args.entities} replicated entities, clean SIGTERM", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--server", type=Path, required=True, help="path to kb_dedicated_server")
    parser.add_argument("--bot", type=Path, required=True, help="path to kb_net_bot")
    parser.add_argument("--clients", type=int, default=3, help="bots to connect (default 3)")
    parser.add_argument("--entities", type=int, default=200, help="objects in the sample project (default 200)")
    parser.add_argument("--seconds", type=int, default=10, help="seconds before the status is checked (default 10)")
    args = parser.parse_args()
    args.server, args.bot = args.server.resolve(), args.bot.resolve()
    run(args)


if __name__ == "__main__":
    try:
        main()
    except (RuntimeError, OSError, subprocess.SubprocessError, KeyError) as error:
        raise SystemExit(f"net linux smoke: {error}") from error

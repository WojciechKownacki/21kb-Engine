#!/usr/bin/env python3
"""Run real UDP clients and collect server timing and traffic metrics."""

import argparse
import csv
import ctypes
import json
import os
from pathlib import Path
import subprocess
import tempfile
import time


def system_cpu():
    if os.name == "nt":
        idle, kernel, user = (ctypes.c_ulonglong() for _ in range(3))
        if not ctypes.windll.kernel32.GetSystemTimes(ctypes.byref(idle), ctypes.byref(kernel), ctypes.byref(user)):
            raise OSError("cannot read system CPU counters")
        return (kernel.value + user.value) / 1e7, idle.value / 1e7
    fields = [int(value) for value in Path("/proc/stat").read_text().splitlines()[0].split()[1:]]
    ticks = os.sysconf("SC_CLK_TCK")
    return sum(fields[:8]) / ticks, (fields[3] + fields[4]) / ticks


def process_cpu(process):
    if os.name == "nt":
        values = [ctypes.c_ulonglong() for _ in range(4)]
        get_times = ctypes.windll.kernel32.GetProcessTimes
        get_times.argtypes = [ctypes.c_void_p] + [ctypes.POINTER(ctypes.c_ulonglong)] * 4
        if not get_times(int(process._handle), *(ctypes.byref(value) for value in values)):
            raise OSError("cannot read process CPU counters")
        return (values[2].value + values[3].value) / 1e7
    fields = Path(f"/proc/{process.pid}/stat").read_text().rsplit(")", 1)[1].split()
    return (int(fields[11]) + int(fields[12])) / os.sysconf("SC_CLK_TCK")


def background_cpu():
    before = system_cpu()
    time.sleep(1)
    after = system_cpu()
    total = after[0] - before[0]
    return 100 * (1 - (after[1] - before[1]) / total) if total else 0


def read_csv(path):
    with path.open(newline="", encoding="utf-8") as stream:
        return list(csv.DictReader(stream))


def run_case(args, root, clients, seconds):
    case = root / str(clients)
    case.mkdir()
    port_file, stop_file = case / "port.txt", case / "stop"
    metrics, bot_csv, status = case / "server.csv", case / "bots.csv", case / "status.json"
    server_log = (case / "server.log").open("w", encoding="utf-8")
    bot_log = (case / "bots.log").open("w", encoding="utf-8")
    server = subprocess.Popen([
        str(args.server), "--project", str(root / "project"), "--keys", str(root / "keys.txt"),
        "--port", "0", "--port-file", str(port_file), "--stop-file", str(stop_file),
        "--metrics-file", str(metrics), "--status-file", str(status), "--max-clients", "512",
        "--max-connects-per-address", "1024", "--max-sessions-per-address", "512",
        "--max-seconds", str(seconds + 30),
    ], stdout=server_log, stderr=subprocess.STDOUT)
    bots = None
    try:
        deadline = time.monotonic() + 20
        while not port_file.exists():
            if server.poll() is not None or time.monotonic() > deadline:
                raise RuntimeError("server failed to start: " + (case / "server.log").read_text())
            time.sleep(0.05)
        bots = subprocess.Popen([
            str(args.bot), "--server", "127.0.0.1:" + port_file.read_text().strip(),
            "--keys", str(root / "keys.txt"), "--clients", str(clients), "--seconds", str(seconds), "--csv", str(bot_csv),
        ], stdout=bot_log, stderr=subprocess.STDOUT)
        started, cpu_start = time.monotonic(), process_cpu(server)
        cpu_last = cpu_start
        deadline = started + seconds + 20
        while bots.poll() is None:
            if server.poll() is not None:
                raise RuntimeError("server stopped while bots were running")
            cpu_last = process_cpu(server)
            if time.monotonic() > deadline:
                raise RuntimeError("bot timeout")
            time.sleep(0.1)
        if bots.returncode:
            raise RuntimeError("bots failed: " + (case / "bots.log").read_text())
        cpu_percent = 100 * (cpu_last - cpu_start) / (time.monotonic() - started)
        stop_file.touch()
        if server.wait(timeout=10):
            raise RuntimeError("server exited with an error")
        rows, bot_rows = read_csv(metrics), read_csv(bot_csv)
        summary = json.loads(status.read_text())
        if summary["host"]["handshakes_rejected"] != 0:
            raise RuntimeError("handshakes were rejected")
        if len(rows) < 4:
            raise RuntimeError("fewer than four server metric rows")
        final = {int(row["client"]): row for row in bot_rows}
        if len(final) != clients or any(int(row["replicas"]) < 1000 for row in final.values()):
            raise RuntimeError("not every bot received 1000 replicas")
        if any(int(row["state_msgs"]) == 0 for row in final.values()):
            raise RuntimeError("a bot received no state updates")
        measured = [row for row in rows if int(row["second"]) > 0]
        avg = sum(float(row["tick_avg_ms"]) for row in measured) / len(measured)
        maximum = max(float(row["tick_max_ms"]) for row in measured)
        span = max(1, int(rows[-1]["second"]) - int(rows[0]["second"]))
        bandwidth = (int(rows[-1]["bytes_out"]) - int(rows[0]["bytes_out"])) / span / clients
        datagrams = (int(rows[-1]["datagrams_in"]) - int(rows[0]["datagrams_in"])) / span
        print(f"{clients:7d} {avg:11.3f} {maximum:11.3f} {bandwidth:14.1f} {datagrams:12.1f} {cpu_percent:10.1f}", flush=True)
        return {"clients": clients, "tick_avg_ms": avg, "tick_max_ms": maximum, "bytes_per_client_second": bandwidth,
                "datagrams_per_second": datagrams, "server_cpu_percent": cpu_percent}
    finally:
        if bots is not None and bots.poll() is None:
            bots.terminate()
            bots.wait(timeout=10)
        if server.poll() is None:
            stop_file.touch()
            try:
                server.wait(timeout=10)
            except subprocess.TimeoutExpired:
                server.terminate()
                server.wait(timeout=10)
        server_log.close()
        bot_log.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--server", type=Path, required=True)
    parser.add_argument("--bot", type=Path, required=True)
    parser.add_argument("--smoke", action="store_true")
    parser.add_argument("--out", type=Path, help="keep CSVs, logs and the measurement summary here")
    args = parser.parse_args()
    args.server, args.bot = args.server.resolve(), args.bot.resolve()
    if not args.smoke:
        background = background_cpu()
        if background > 20:
            raise RuntimeError(f"measurement invalid: background CPU {background:.1f}% exceeds 20%")
    temporary = None
    if args.out:
        args.out.mkdir(parents=True, exist_ok=False)
        root = args.out.resolve()
    else:
        temporary = tempfile.TemporaryDirectory(prefix="21kb_net_bench_")
        root = Path(temporary.name)
    try:
        subprocess.run([str(args.bot), "--make-keys", "512", "--keys-out", str(root / "keys.txt")], check=True)
        subprocess.run([str(args.bot), "--make-project", str(root / "project"), "--entities", "1000"], check=True)
        print("clients tick_avg_ms tick_max_ms bytes/client/s datagrams/s server_CPU%", flush=True)
        results = [run_case(args, root, clients, 5 if args.smoke else 20) for clients in ([20] if args.smoke else [50, 100, 200, 300])]
        (root / "summary.json").write_text(json.dumps(results, indent=2), encoding="utf-8")
        print("PASS: every bot received 1000 replicas; no rejected handshakes", flush=True)
    finally:
        if temporary:
            temporary.cleanup()


if __name__ == "__main__":
    try:
        main()
    except (RuntimeError, OSError, subprocess.SubprocessError) as error:
        raise SystemExit(f"net bench: {error}") from error

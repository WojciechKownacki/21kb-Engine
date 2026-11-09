"""Real UDP/WebSocket listener checks. Requires the websocket-client Python package."""
import argparse
import json
from pathlib import Path
import socket
import subprocess
import tempfile
import time

import websocket


def run(executable):
    with tempfile.TemporaryDirectory(prefix="NetworkTransport") as folder:
        stop = Path(folder) / "stop"
        with (Path(folder) / "stderr.log").open("w+") as errors:
            process = subprocess.Popen([str(executable), str(stop)], stdout=subprocess.PIPE,
                                       stderr=errors, text=True)
            try:
                line = process.stdout.readline().strip()
                assert line, "listener failed to start"
                port, udp_port = map(int, line.split())
                url = f"ws://127.0.0.1:{port}/21kb"

                def connect(address=url, origin="http://localhost", protocols=None):
                    return websocket.create_connection(address, timeout=3, origin=origin,
                        subprotocols=["21kb.datagram.v1"] if protocols is None else protocols,
                        http_proxy_host=None)

                def reject(**kwargs):
                    try:
                        connection = connect(**kwargs)
                    except websocket.WebSocketException:
                        return
                    connection.close()
                    raise AssertionError("invalid upgrade accepted")

                reject(address=f"ws://127.0.0.1:{port}/other")
                reject(origin="http://untrusted.invalid")
                reject(protocols=["wrong.protocol"])
                with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as udp:
                    udp.settimeout(3)
                    udp.sendto(b"native", ("127.0.0.1", udp_port))
                    assert udp.recv(1200) == b"native"
                connection = connect()
                try:
                    for payload in [b"", b"hello", bytes(range(256)) * 4 + bytes(176)]:
                        connection.send_binary(payload)
                        assert connection.recv() == payload
                    connection.send_frame(websocket.ABNF.create_frame(b"fragment", websocket.ABNF.OPCODE_BINARY, fin=0))
                    connection.send_frame(websocket.ABNF.create_frame(b"ed", websocket.ABNF.OPCODE_CONT, fin=1))
                    assert connection.recv() == b"fragmented"
                    other = connect()
                    try:
                        reject()
                        other.send_binary(b"second")
                        assert other.recv() == b"second"
                    finally:
                        other.close()
                finally:
                    connection.close()
                for payload, binary in [(b"x" * 1201, True), ("text", False)]:
                    connection = connect()
                    try:
                        if binary:
                            connection.send_binary(payload)
                        else:
                            connection.send(payload)
                        try:
                            value = connection.recv()
                            assert value in ("", None), "invalid message was echoed"
                        except websocket.WebSocketConnectionClosedException:
                            pass
                    finally:
                        connection.close()
                for index in range(5):
                    connection = connect()
                    try:
                        payload = f"reconnect-{index}".encode()
                        connection.send_binary(payload)
                        assert connection.recv() == payload
                    finally:
                        connection.close()
                stop.touch()
                assert process.wait(timeout=5) == 0
                return {"result": "PASS", "checks": ["UDP", "binary bounds", "fragmentation",
                    "path", "origin", "subprotocol", "client limit", "text rejection", "reconnect", "shutdown"]}
            finally:
                stop.touch()
                if process.poll() is None:
                    try:
                        process.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()
                errors.seek(0)
                print(errors.read())


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--harness", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    result = run(args.harness)
    args.output.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result))

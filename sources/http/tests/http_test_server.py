"""A small HTTP and HTTPS server for the kb_http tests.

Usage: http_test_server.py <directory> <cert.pem> <key.pem>

It writes <directory>/ports.json ({"http": n, "https": n}) when it is ready and appends one line per
request ("<scheme> <method> <path>") to <directory>/requests.log. It runs until it is killed.
"""
import http.server
import json
import os
import socketserver
import ssl
import sys
import threading
import time
import urllib.parse

DIRECTORY = sys.argv[1]
CERT = sys.argv[2]
KEY = sys.argv[3]
LOG_LOCK = threading.Lock()


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *args):
        pass

    def scheme(self):
        return "https" if isinstance(self.connection, ssl.SSLSocket) else "http"

    def record(self):
        with LOG_LOCK:
            with open(os.path.join(DIRECTORY, "requests.log"), "a", encoding="utf-8") as log:
                log.write("%s %s %s\n" % (self.scheme(), self.command, self.path))

    def read_body(self):
        length = int(self.headers.get("Content-Length", "0") or 0)
        return self.rfile.read(length) if length else b""

    def reply(self, code, body=b"", headers=None):
        self.send_response(code)
        for name, value in (headers or {}).items():
            self.send_header(name, value)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(body)

    def handle_any(self):
        self.record()
        body = self.read_body()
        parsed = urllib.parse.urlparse(self.path)
        path = parsed.path
        query = urllib.parse.parse_qs(parsed.query)
        try:
            if path == "/hello":
                self.reply(200, b"hello", {"X-Test": "yes", "Content-Type": "text/plain"})
            elif path == "/echo":
                self.reply(200, body, {"X-Method": self.command, "X-Auth": self.headers.get("Authorization", "")})
            elif path == "/headers":
                text = "".join("%s: %s\n" % (k, v) for k, v in self.headers.items())
                self.reply(200, text.encode("utf-8"))
            elif path == "/redirect":
                self.reply(302, b"", {"Location": "/hello"})
            elif path == "/redirect303":
                self.reply(303, b"", {"Location": "/echo"})
            elif path == "/redirect307":
                self.reply(307, b"", {"Location": "/echo"})
            elif path == "/relative":
                self.reply(302, b"", {"Location": "hello"})
            elif path == "/loop":
                self.reply(302, b"", {"Location": "/loop"})
            elif path == "/to":
                self.reply(302, b"", {"Location": query.get("url", [""])[0]})
            elif path == "/slow":
                time.sleep(4)
                self.reply(200, b"late")
            elif path == "/big":
                self.reply(200, b"\0" * int(query.get("bytes", ["0"])[0]))
            elif path == "/drip":
                self.send_response(200)
                self.send_header("Content-Length", "100")
                self.end_headers()
                for _ in range(100):
                    self.wfile.write(b"x")
                    self.wfile.flush()
                    time.sleep(0.2)
            elif path == "/stream":
                self.send_response(200)
                self.send_header("Transfer-Encoding", "chunked")
                self.end_headers()
                chunk = b"y" * 1024
                while True:
                    self.wfile.write(b"%x\r\n" % len(chunk) + chunk + b"\r\n")
                    self.wfile.flush()
                    time.sleep(0.005)
            elif path.startswith("/status/"):
                self.reply(int(path.rsplit("/", 1)[1]), b"status")
            else:
                self.reply(404, b"no such path")
        except (BrokenPipeError, ConnectionResetError, ssl.SSLError, OSError):
            self.close_connection = True

    do_GET = handle_any
    do_POST = handle_any
    do_PUT = handle_any
    do_PATCH = handle_any
    do_DELETE = handle_any
    do_HEAD = handle_any


class Server(socketserver.ThreadingMixIn, http.server.HTTPServer):
    daemon_threads = True
    allow_reuse_address = False

    def handle_error(self, request, client_address):
        pass


def main():
    plain = Server(("127.0.0.1", 0), Handler)
    secure = Server(("127.0.0.1", 0), Handler)
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.minimum_version = ssl.TLSVersion.TLSv1_2
    context.load_cert_chain(CERT, KEY)
    secure.socket = context.wrap_socket(secure.socket, server_side=True)
    for server in (plain, secure):
        threading.Thread(target=server.serve_forever, daemon=True).start()
    ports = {"http": plain.server_address[1], "https": secure.server_address[1]}
    temporary = os.path.join(DIRECTORY, "ports.json.tmp")
    with open(temporary, "w", encoding="utf-8") as output:
        json.dump(ports, output)
    os.replace(temporary, os.path.join(DIRECTORY, "ports.json"))
    while True:
        time.sleep(1)


main()

from __future__ import annotations

import json
import os
import pathlib
import socket
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from typing import Any

import duckdb


EXPECTED_DUCKDB = ("v1.5.4", "08e34c447b", "Variegata")
EXPECTED_EXTENSION = (
    "live_rest_product_proof",
    "0.0.0-live-rest-trial",
    True,
    False,
    "NOT_INSTALLED",
)
EXPECTED_PATH = "/search/users?q=duckdb+in%3Alogin&per_page=3"
EXPECTED_ROWS = [
    (9223372036854775806, "duckdb-alpha", False),
    (42, "duckdb-β", True),
    (-7, "duckdb-final", False),
]
RESPONSE_SECRET = "DO_NOT_EXPOSE_RESPONSE_BODY_7f915"


class OracleServer(ThreadingHTTPServer):
    daemon_threads = True

    def __init__(self) -> None:
        super().__init__(("127.0.0.1", 0), OracleHandler)
        self.mode = "success"
        self.requests: list[dict[str, Any]] = []
        self.requests_lock = threading.Lock()
        self.request_started = threading.Event()
        self.handler_exited = threading.Event()
        self.peer_disconnected = threading.Event()
        self.release_blocked = threading.Event()

    def record(self, handler: BaseHTTPRequestHandler) -> None:
        with self.requests_lock:
            self.requests.append(
                {
                    "method": handler.command,
                    "path": handler.path,
                    "expected_host": f"127.0.0.1:{self.server_port}",
                    "headers": {
                        name.lower(): handler.headers.get_all(name) or []
                        for name in handler.headers.keys()
                    },
                }
            )
        self.request_started.set()

    def request_count(self) -> int:
        with self.requests_lock:
            return len(self.requests)

    def last_request(self) -> dict[str, Any]:
        with self.requests_lock:
            return self.requests[-1]

    def prepare_blocking_request(self) -> None:
        self.request_started.clear()
        self.handler_exited.clear()
        self.peer_disconnected.clear()
        self.release_blocked.clear()


class OracleHandler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    @property
    def oracle(self) -> OracleServer:
        return self.server  # type: ignore[return-value]

    def log_message(self, format: str, *args: object) -> None:
        del format, args

    def _send_body(self, status: int, body: bytes) -> None:
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Connection", "close")
        self.end_headers()
        try:
            self.wfile.write(body)
            self.wfile.flush()
        except (BrokenPipeError, ConnectionResetError):
            pass

    def do_GET(self) -> None:
        self.oracle.record(self)
        try:
            self._respond_for_mode(self.oracle.mode)
        finally:
            self.oracle.handler_exited.set()

    def _respond_for_mode(self, mode: str) -> None:
        if mode == "success":
            body = json.dumps(
                {
                    "total_count": 3,
                    "incomplete_results": False,
                    "items": [
                        {"id": row[0], "login": row[1], "site_admin": row[2]}
                        for row in EXPECTED_ROWS
                    ],
                },
                ensure_ascii=False,
                separators=(",", ":"),
            ).encode("utf-8")
            self._send_body(200, body)
            return
        if mode == "status":
            self._send_body(503, f'{{"message":"{RESPONSE_SECRET}"}}'.encode())
            return
        if mode == "malformed":
            self._send_body(
                200,
                (
                    '{"items":[{"id":1,"login":"'
                    + RESPONSE_SECRET
                    + '","site_admin":tru'
                ).encode(),
            )
            return
        if mode == "oversized":
            self._send_body(200, (RESPONSE_SECRET + "x" * 70000).encode())
            return
        if mode == "redirect":
            if self.path == EXPECTED_PATH:
                self.send_response(302)
                self.send_header(
                    "Location",
                    f"http://127.0.0.1:{self.oracle.server_port}/redirect-target",
                )
                self.send_header("Content-Length", "0")
                self.send_header("Connection", "close")
                self.end_headers()
                return
            self._send_body(200, f'{{"message":"{RESPONSE_SECRET}"}}'.encode())
            return
        if mode == "disconnect":
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", "4096")
            self.end_headers()
            partial = f'{{"items":[{{"login":"{RESPONSE_SECRET}'.encode()
            self.wfile.write(partial)
            self.wfile.flush()
            try:
                self.connection.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
            self.connection.close()
            return
        if mode == "blocking":
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Connection", "close")
            self.end_headers()
            try:
                partial = f'{{"items":[{{"login":"{RESPONSE_SECRET}'.encode()
                self.wfile.write(partial)
                self.wfile.flush()
                while not self.oracle.release_blocked.wait(0.02):
                    self.wfile.write(b" ")
                    self.wfile.flush()
            except (BrokenPipeError, ConnectionResetError):
                self.oracle.peer_disconnected.set()
            return
        self._send_body(500, b"unknown oracle mode")


def start_oracle() -> tuple[OracleServer, threading.Thread]:
    server = OracleServer()
    thread = threading.Thread(
        target=server.serve_forever, name="live-rest-oracle", daemon=True
    )
    thread.start()
    return server, thread


def stop_oracle(server: OracleServer, thread: threading.Thread) -> None:
    server.release_blocked.set()
    server.shutdown()
    server.server_close()
    thread.join(timeout=5)
    if thread.is_alive():
        raise AssertionError("controlled HTTP service did not stop")


def configure_controlled_authority(server: OracleServer) -> None:
    os.environ["DUCKDB_API_LIVE_PROOF_AUTHORITY"] = (
        f"http://127.0.0.1:{server.server_port}"
    )
    # Ambient proxy configuration must not widen or reroute trial authority.
    for name in (
        "HTTP_PROXY",
        "HTTPS_PROXY",
        "ALL_PROXY",
        "http_proxy",
        "https_proxy",
        "all_proxy",
    ):
        os.environ[name] = "http://127.0.0.1:1"
    os.environ["NO_PROXY"] = ""
    os.environ["no_proxy"] = ""


def load_extension(extension_path: pathlib.Path) -> duckdb.DuckDBPyConnection:
    connection = duckdb.connect(config={"allow_unsigned_extensions": "true"})
    quoted_path = extension_path.resolve().as_posix().replace("'", "''")
    connection.execute(f"LOAD '{quoted_path}'")
    if connection.execute("PRAGMA version").fetchone() != EXPECTED_DUCKDB:
        raise AssertionError("unexpected DuckDB host identity")
    identity = connection.execute(
        """
        SELECT extension_name, extension_version, loaded, installed, install_mode
        FROM duckdb_extensions()
        WHERE extension_name = 'live_rest_product_proof'
        """
    ).fetchone()
    if identity != EXPECTED_EXTENSION:
        raise AssertionError(f"unexpected extension identity: {identity!r}")
    return connection


def assert_exact_request(request: dict[str, Any]) -> None:
    if request["method"] != "GET" or request["path"] != EXPECTED_PATH:
        raise AssertionError(f"unexpected request target: {request!r}")
    headers = request["headers"]
    expected = {
        "host": [request["expected_host"]],
        "accept": ["application/vnd.github+json"],
        "user-agent": ["duckdb-fdw-live-rest-product-proof"],
        "x-github-api-version": ["2022-11-28"],
    }
    if headers != expected:
        raise AssertionError(f"unexpected request headers: {headers!r}")

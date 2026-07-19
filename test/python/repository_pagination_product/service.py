"""Loopback HTTP responses for the controlled repository product oracle.

The service supplies response bytes and Link fields as black-box Runtime input.
It does not parse Link grammar, construct plans, or decide page transitions.
"""

from __future__ import annotations

import json
import socket
import threading
from dataclasses import dataclass
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from typing import Any, Iterable


RESPONSE_SECRET = "REPOSITORY_RESPONSE_CANARY_27d4"
HOSTILE_NEXT = (
    "<https://credential-canary.invalid/user/repos?per_page=100&page=3>; rel=next"
)


@dataclass(frozen=True)
class ResponseSpec:
    status: int
    body: bytes
    links: tuple[str, ...] = ()
    block: bool = False


def repository_body(rows: Iterable[tuple[int, str, bool, bool, bool]]) -> bytes:
    return json.dumps(
        [
            {
                "id": row[0],
                "full_name": row[1],
                "private": row[2],
                "fork": row[3],
                "archived": row[4],
            }
            for row in rows
        ],
        ensure_ascii=False,
        separators=(",", ":"),
    ).encode("utf-8")


def accepted_next(page: int) -> str:
    """Create accepted response input without implementing its parser."""

    return (
        f"<https://api.github.com/user/repos?per_page=100&page={page}>; rel=next"
    )


def repository_response(
    rows: Iterable[tuple[int, str, bool, bool, bool]],
    *,
    next_page: int | None = None,
) -> ResponseSpec:
    links = () if next_page is None else (accepted_next(next_page),)
    return ResponseSpec(200, repository_body(rows), links)


class RepositoryOracleServer(ThreadingHTTPServer):
    """Loopback-only service with per-scenario and lifetime request evidence."""

    daemon_threads = True

    def __init__(self) -> None:
        super().__init__(("127.0.0.1", 0), RepositoryOracleHandler)
        self._lock = threading.Lock()
        self._responses: list[ResponseSpec] = []
        self._requests: list[dict[str, Any]] = []
        self._lifetime_requests = 0
        self.request_started = threading.Event()
        self.blocked_started = threading.Event()
        self.blocked_exited = threading.Event()
        self.release_blocked = threading.Event()
        self.peer_disconnected = threading.Event()

    def configure(self, responses: Iterable[ResponseSpec]) -> None:
        configured = list(responses)
        if not configured:
            raise AssertionError("repository oracle scenario has no responses")
        with self._lock:
            self._responses = configured
            self._requests = []
        self.request_started.clear()
        self.blocked_started.clear()
        self.blocked_exited.clear()
        self.release_blocked.clear()
        self.peer_disconnected.clear()

    def record_and_select(self, handler: BaseHTTPRequestHandler) -> ResponseSpec:
        request = {
            "method": handler.command,
            "path": handler.path,
            "headers": {
                name.lower(): handler.headers.get_all(name) or []
                for name in handler.headers.keys()
            },
        }
        with self._lock:
            index = len(self._requests)
            self._requests.append(request)
            self._lifetime_requests += 1
            response = (
                self._responses[index]
                if index < len(self._responses)
                else ResponseSpec(500, b'{"message":"unexpected request"}')
            )
        self.request_started.set()
        return response

    def requests(self) -> list[dict[str, Any]]:
        with self._lock:
            return list(self._requests)

    def request_count(self) -> int:
        with self._lock:
            return len(self._requests)

    def lifetime_request_count(self) -> int:
        with self._lock:
            return self._lifetime_requests


class RepositoryOracleHandler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    @property
    def oracle(self) -> RepositoryOracleServer:
        return self.server  # type: ignore[return-value]

    def log_message(self, format: str, *args: object) -> None:
        del format, args

    def do_GET(self) -> None:
        response = self.oracle.record_and_select(self)
        if response.block:
            self._block_response()
            return
        self.send_response(response.status)
        for link in response.links:
            self.send_header("Link", link)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(response.body)))
        self.send_header("Connection", "close")
        self.end_headers()
        try:
            self.wfile.write(response.body)
            self.wfile.flush()
        except (BrokenPipeError, ConnectionResetError):
            self.oracle.peer_disconnected.set()

    def _block_response(self) -> None:
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Connection", "close")
        self.end_headers()
        self.oracle.blocked_started.set()
        try:
            self.wfile.write(b"[")
            self.wfile.flush()
            while not self.oracle.release_blocked.wait(0.02):
                self.wfile.write(b" ")
                self.wfile.flush()
        except (BrokenPipeError, ConnectionResetError, OSError):
            self.oracle.peer_disconnected.set()
        finally:
            try:
                self.connection.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
            self.oracle.blocked_exited.set()


def start_oracle() -> tuple[RepositoryOracleServer, threading.Thread]:
    server = RepositoryOracleServer()
    thread = threading.Thread(
        target=server.serve_forever,
        name="duckdb-api-repository-product-http",
        daemon=True,
    )
    thread.start()
    return server, thread


def stop_oracle(server: RepositoryOracleServer, thread: threading.Thread) -> None:
    server.release_blocked.set()
    server.shutdown()
    server.server_close()
    thread.join(timeout=5)
    if thread.is_alive():
        raise AssertionError("repository product service did not stop")

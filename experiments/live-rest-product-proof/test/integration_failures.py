#!/usr/bin/env python3

from __future__ import annotations

import pathlib
import sys
import threading
import time

import duckdb

from support.http_service import (
    EXPECTED_PATH,
    RESPONSE_SECRET,
    OracleServer,
    assert_exact_request,
    configure_controlled_authority,
    load_extension,
    start_oracle,
    stop_oracle,
)


def assert_redacted_diagnostic(message: str) -> None:
    for value in (RESPONSE_SECRET, EXPECTED_PATH, "127.0.0.1", "api.github.com"):
        if value in message:
            raise AssertionError(
                f"diagnostic exposed forbidden response/request data: {message!r}"
            )


def assert_safe_failure(
    connection: duckdb.DuckDBPyConnection, expected: str
) -> None:
    try:
        connection.execute("SELECT * FROM duckdb_api_live_rest_proof()").fetchall()
    except duckdb.Error as error:
        message = str(error)
        if expected.lower() not in message.lower():
            raise AssertionError(
                f"expected failure containing {expected!r}, received {message!r}"
            ) from error
        assert_redacted_diagnostic(message)
    else:
        raise AssertionError(f"expected a live REST failure containing {expected!r}")


def run_bounded_failures(
    connection: duckdb.DuckDBPyConnection, server: OracleServer
) -> None:
    for mode, expected in (
        ("status", "non-success status"),
        ("malformed", "not valid JSON"),
        ("oversized", "byte budget"),
        ("redirect", "non-success status"),
        ("disconnect", "transport request failed"),
    ):
        server.mode = mode
        before = server.request_count()
        assert_safe_failure(connection, expected)
        if server.request_count() != before + 1:
            raise AssertionError(f"{mode} failure retried or omitted its request")
        assert_exact_request(server.last_request())


def assert_runtime_recovery(
    connection: duckdb.DuckDBPyConnection, server: OracleServer
) -> None:
    server.mode = "success"
    recovered = connection.execute(
        "SELECT count(*), min(id), max(id) FROM duckdb_api_live_rest_proof()"
    ).fetchone()
    if recovered != (3, -7, 9223372036854775806):
        raise AssertionError(f"post-failure cleanup failed: {recovered!r}")


def assert_blocked_handler_aborted(server: OracleServer) -> None:
    if not server.peer_disconnected.wait(2):
        raise AssertionError("client completion did not abort the blocked peer socket")
    if not server.handler_exited.wait(2):
        raise AssertionError("blocked HTTP handler remained active after client completion")


def run_hard_wall_timeout(
    connection: duckdb.DuckDBPyConnection, server: OracleServer
) -> None:
    server.mode = "blocking"
    server.prepare_blocking_request()
    before = server.request_count()
    started_at = time.monotonic()
    assert_safe_failure(connection, "wall-time budget")
    elapsed = time.monotonic() - started_at
    if elapsed < 4 or elapsed > 7:
        raise AssertionError(f"hard wall-time budget completed after {elapsed:.3f}s")
    if server.request_count() != before + 1:
        raise AssertionError("wall-time failure retried or omitted its request")
    assert_exact_request(server.last_request())
    assert_blocked_handler_aborted(server)
    assert_runtime_recovery(connection, server)


def run_cancellation(
    connection: duckdb.DuckDBPyConnection, server: OracleServer
) -> None:
    server.mode = "blocking"
    server.prepare_blocking_request()
    before = server.request_count()
    cancellation_errors: list[BaseException] = []

    def query_until_interrupted() -> None:
        try:
            connection.execute("SELECT * FROM duckdb_api_live_rest_proof()").fetchall()
        except BaseException as error:  # The query must be interrupted.
            cancellation_errors.append(error)

    worker = threading.Thread(
        target=query_until_interrupted, name="live-rest-cancellation", daemon=True
    )
    worker.start()
    if not server.request_started.wait(5):
        connection.interrupt()
        raise AssertionError("blocking request did not begin within five seconds")
    interrupted_at = time.monotonic()
    connection.interrupt()
    worker.join(timeout=5)
    interrupt_elapsed = time.monotonic() - interrupted_at
    if worker.is_alive():
        raise AssertionError("cancelled live REST scan did not stop within five seconds")
    if interrupt_elapsed > 1:
        raise AssertionError(
            "cancelled live REST scan fell back to the five-second wall deadline: "
            f"{interrupt_elapsed:.3f}s"
        )
    if len(cancellation_errors) != 1 or "interrupt" not in str(
        cancellation_errors[0]
    ).lower():
        raise AssertionError(
            f"expected one interruption error, received {cancellation_errors!r}"
        )
    assert_redacted_diagnostic(str(cancellation_errors[0]))
    if server.request_count() != before + 1:
        raise AssertionError("cancelled scan retried or omitted its request")
    assert_exact_request(server.last_request())
    assert_blocked_handler_aborted(server)
    assert_runtime_recovery(connection, server)


def run_connection_close_waits_for_deadline(
    extension_path: pathlib.Path, server: OracleServer
) -> None:
    connection = load_extension(extension_path)
    server.mode = "blocking"
    server.prepare_blocking_request()
    before = server.request_count()
    query_errors: list[BaseException] = []

    def query_during_close() -> None:
        try:
            connection.execute("SELECT * FROM duckdb_api_live_rest_proof()").fetchall()
        except BaseException as error:
            query_errors.append(error)

    worker = threading.Thread(
        target=query_during_close, name="live-rest-active-close", daemon=True
    )
    worker.start()
    try:
        if not server.request_started.wait(5):
            connection.interrupt()
            raise AssertionError("active-close request did not begin within five seconds")
        close_started_at = time.monotonic()
        connection.close()
        close_elapsed = time.monotonic() - close_started_at
        worker.join(timeout=2)
        if close_elapsed < 4 or close_elapsed > 7 or worker.is_alive():
            raise AssertionError(
                "connection close did not wait safely for the hard query deadline: "
                f"{close_elapsed:.3f}s"
            )
        if len(query_errors) != 1:
            raise AssertionError(
                f"active connection close produced unexpected errors: {query_errors!r}"
            )
        if "wall-time budget" not in str(query_errors[0]).lower():
            raise AssertionError(
                "active connection close did not terminate at the hard wall deadline"
            )
        assert_redacted_diagnostic(str(query_errors[0]))
        if server.request_count() != before + 1:
            raise AssertionError("active connection close retried or omitted its request")
        assert_exact_request(server.last_request())
        assert_blocked_handler_aborted(server)
    finally:
        try:
            connection.interrupt()
        except duckdb.Error:
            pass
        connection.close()


def main() -> int:
    if len(sys.argv) != 2:
        raise SystemExit("usage: integration_failures.py PATH_TO_EXTENSION")
    extension_path = pathlib.Path(sys.argv[1]).resolve()
    server, server_thread = start_oracle()
    configure_controlled_authority(server)
    connection = load_extension(extension_path)
    try:
        run_bounded_failures(connection, server)
        run_hard_wall_timeout(connection, server)
        run_cancellation(connection, server)
        run_connection_close_waits_for_deadline(extension_path, server)
        print(f"controlled live REST failure evidence: requests={server.request_count()}")
    finally:
        server.release_blocked.set()
        connection.close()
        stop_oracle(server, server_thread)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

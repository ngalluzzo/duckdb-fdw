"""Offline binding, prepared secret lifecycle, and concurrent isolation oracles."""

from __future__ import annotations

import pathlib
import threading

import duckdb

from live_rest_product.support import RESPONSE_SECRET, OracleServer, load_controlled_extension

from .support import (
    AUTHENTICATED_SCAN,
    AUTHENTICATED_SCHEMA,
    AUTHENTICATED_SQL,
    GITHUB_BEARER_TOKEN_BYTE_LIMIT,
    TOKEN_A,
    TOKEN_B,
    assert_authenticated_request,
    create_temporary_secret,
)


FORBIDDEN_DIAGNOSTIC_VALUES = (
    TOKEN_A,
    TOKEN_B,
    RESPONSE_SECRET,
    "Authorization",
    "Bearer ",
)


def _require_no_request(server: OracleServer, before: int, label: str) -> None:
    if server.request_count() != before:
        raise AssertionError(f"{label} unexpectedly acquired network authority")


def _expect_error(
    connection: duckdb.DuckDBPyConnection,
    sql: str,
    expected_fragment: str,
    forbidden: tuple[str, ...] = (),
) -> str:
    try:
        connection.execute(sql).fetchall()
    except duckdb.Error as error:
        diagnostic = str(error)
        if any(value in diagnostic for value in FORBIDDEN_DIAGNOSTIC_VALUES + forbidden):
            raise AssertionError("credential value escaped through a diagnostic") from None
        if expected_fragment not in diagnostic:
            raise AssertionError("safe diagnostic category or message drifted") from None
        return diagnostic
    raise AssertionError("statement unexpectedly succeeded")


def _run_concurrent_scan(
    connection: duckdb.DuckDBPyConnection,
    index: int,
    rows: list[list[tuple[object, ...]] | None],
    errors: list[str | None],
) -> None:
    try:
        rows[index] = connection.execute(AUTHENTICATED_SQL).fetchall()
    except BaseException as error:
        diagnostic = str(error)
        errors[index] = (
            "credential escaped through concurrent failure"
            if any(value in diagnostic for value in FORBIDDEN_DIAGNOSTIC_VALUES)
            else "concurrent authenticated query failed"
        )


def _assert_concurrent_pairing(
    extension_path: pathlib.Path, server: OracleServer
) -> None:
    first = load_controlled_extension(extension_path)
    second = load_controlled_extension(extension_path)
    try:
        create_temporary_secret(first, TOKEN_A)
        create_temporary_secret(second, TOKEN_B)
        server.prepare_concurrent_requests()
        before = server.request_count()
        rows: list[list[tuple[object, ...]] | None] = [None, None]
        errors: list[str | None] = [None, None]
        threads = [
            threading.Thread(
                target=_run_concurrent_scan,
                args=(connection, index, rows, errors),
                daemon=True,
            )
            for index, connection in enumerate((first, second))
        ]
        for thread in threads:
            thread.start()
        for thread in threads:
            thread.join(timeout=3)
        if any(thread.is_alive() for thread in threads) or any(errors):
            raise AssertionError("concurrent authenticated scans failed safely")
        if rows[0] != [(101, "principal-a", False)]:
            raise AssertionError("token A scan returned the wrong principal")
        if rows[1] != [(202, "principal-b", True)]:
            raise AssertionError("token B scan returned the wrong principal")
        if server.request_count() != before + 2:
            raise AssertionError("concurrent scans did not perform one request each")
    finally:
        first.close()
        second.close()


def _assert_initialized_snapshot(
    connection: duckdb.DuckDBPyConnection,
    catalog: duckdb.DuckDBPyConnection,
    server: OracleServer,
    mutation_sql: str,
    active_token: str,
    expected_row: tuple[object, ...],
) -> None:
    server.prepare_delayed_success_request()
    before = server.request_count()
    rows: list[list[tuple[object, ...]] | None] = [None]
    errors: list[str | None] = [None]
    thread = threading.Thread(
        target=_run_concurrent_scan,
        args=(connection, 0, rows, errors),
        daemon=True,
    )
    thread.start()
    if not server.request_started.wait(2):
        server.release_blocked.set()
        raise AssertionError("initialized authenticated request did not reach Runtime")
    catalog.execute(mutation_sql)
    server.release_blocked.set()
    thread.join(timeout=3)
    if thread.is_alive() or errors[0] is not None:
        raise AssertionError("initialized authenticated scan did not finish safely")
    assert_authenticated_request(server, before, active_token)
    if rows[0] != [expected_row]:
        raise AssertionError("initialized scan changed principal after catalog mutation")


def run_secret_contract(extension_path: pathlib.Path, server: OracleServer) -> int:
    """Prove offline bind plus current-value resolution at every execution."""

    connection = load_controlled_extension(extension_path)
    try:
        before = server.request_count()
        described = connection.execute(f"DESCRIBE SELECT * {AUTHENTICATED_SCAN}").fetchall()
        if [(row[0], row[1]) for row in described] != AUTHENTICATED_SCHEMA:
            raise AssertionError(f"authenticated DESCRIBE drifted: {described!r}")
        connection.execute(f"EXPLAIN {AUTHENTICATED_SQL}").fetchall()
        connection.execute(f"PREPARE authenticated_identity AS {AUTHENTICATED_SQL}")
        _require_no_request(server, before, "bind/describe/explain/prepare")

        _expect_error(
            connection,
            "EXECUTE authenticated_identity",
            "[duckdb_api][authentication]",
        )
        _require_no_request(server, before, "missing-secret execution")

        create_temporary_secret(connection, TOKEN_A)
        rows = connection.execute("EXECUTE authenticated_identity").fetchall()
        assert_authenticated_request(server, before, TOKEN_A)
        if rows != [(101, "principal-a", False)]:
            raise AssertionError(f"prepared token A row drifted: {rows!r}")

        catalog = connection.cursor()
        _assert_initialized_snapshot(
            connection,
            catalog,
            server,
            "CREATE OR REPLACE TEMPORARY SECRET github_default "
            f"(TYPE duckdb_api, PROVIDER config, TOKEN '{TOKEN_B}')",
            TOKEN_A,
            (101, "principal-a", False),
        )
        server.mode = "success"
        before = server.request_count()
        rows = connection.execute("EXECUTE authenticated_identity").fetchall()
        assert_authenticated_request(server, before, TOKEN_B)
        if rows != [(202, "principal-b", True)]:
            raise AssertionError(f"prepared token B row drifted: {rows!r}")

        _assert_initialized_snapshot(
            connection,
            catalog,
            server,
            "DROP SECRET github_default",
            TOKEN_B,
            (202, "principal-b", True),
        )
        catalog.close()
        server.mode = "success"
        before = server.request_count()
        _expect_error(
            connection,
            "EXECUTE authenticated_identity",
            "named duckdb_api secret was not found",
            (TOKEN_A, TOKEN_B),
        )
        _require_no_request(server, before, "dropped-secret execution")
        connection.execute("DEALLOCATE authenticated_identity")

        before = server.request_count()
        _expect_error(
            connection,
            "CREATE SECRET persisted (TYPE duckdb_api, PROVIDER config, TOKEN 'x')",
            "require explicit CREATE TEMPORARY SECRET",
        )
        _expect_error(
            connection,
            "CREATE TEMPORARY SECRET empty_token "
            "(TYPE duckdb_api, PROVIDER config, TOKEN '')",
            "TOKEN must be a non-empty VARCHAR",
        )
        boundary_token = "e" * GITHUB_BEARER_TOKEN_BYTE_LIMIT
        create_temporary_secret(connection, boundary_token)
        connection.execute("DROP SECRET github_default")
        oversized_token = "o" * (GITHUB_BEARER_TOKEN_BYTE_LIMIT + 1)
        _expect_error(
            connection,
            "CREATE TEMPORARY SECRET oversized_token "
            "(TYPE duckdb_api, PROVIDER config, TOKEN '"
            + oversized_token
            + "')",
            "[duckdb_api][resource] field=header_bytes",
            (oversized_token,),
        )
        _expect_error(
            connection,
            "SELECT * FROM duckdb_api_scan(connector := 'github', "
            "relation := 'authenticated_user')",
            "required named argument secret is missing",
        )
        _expect_error(
            connection,
            "SELECT * FROM duckdb_api_scan(connector := 'github', "
            "relation := 'authenticated_user', secret := '')",
            "named argument secret must not be NULL or empty",
        )
        _expect_error(
            connection,
            "SELECT * FROM duckdb_api_scan(connector := 'github', "
            "relation := 'duckdb_login_search_page', secret := 'unused')",
            "named argument secret is not accepted",
        )
        _require_no_request(server, before, "rejected secret surfaces")
    finally:
        connection.close()

    _assert_concurrent_pairing(extension_path, server)
    return server.request_count()

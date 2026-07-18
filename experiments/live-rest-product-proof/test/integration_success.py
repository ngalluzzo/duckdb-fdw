#!/usr/bin/env python3

from __future__ import annotations

import argparse
import os
import pathlib

from support.http_service import (
    EXPECTED_ROWS,
    assert_exact_request,
    configure_controlled_authority,
    load_extension,
    start_oracle,
    stop_oracle,
)


def run_controlled_success(extension_path: pathlib.Path) -> int:
    server, server_thread = start_oracle()
    configure_controlled_authority(server)
    connection = load_extension(extension_path)
    try:
        if server.request_count() != 0:
            raise AssertionError("loading the extension performed network I/O")
        described = connection.execute(
            "DESCRIBE SELECT * FROM duckdb_api_live_rest_proof()"
        ).fetchall()
        if [(row[0], row[1]) for row in described] != [
            ("id", "BIGINT"),
            ("login", "VARCHAR"),
            ("site_admin", "BOOLEAN"),
        ]:
            raise AssertionError(f"unexpected bound schema: {described!r}")
        if server.request_count() != 0:
            raise AssertionError("bind or planning performed network I/O")

        result = connection.execute("SELECT * FROM duckdb_api_live_rest_proof()")
        logical_types = [str(column[1]) for column in result.description]
        rows = result.fetchall()
        if logical_types != ["BIGINT", "VARCHAR", "BOOLEAN"] or rows != EXPECTED_ROWS:
            raise AssertionError(
                f"unexpected typed rows: types={logical_types!r} rows={rows!r}"
            )
        if server.request_count() != 1:
            raise AssertionError("successful scan did not perform exactly one request")
        assert_exact_request(server.last_request())

        connection.execute(
            "PREPARE live_rest_snapshot AS "
            "SELECT * FROM duckdb_api_live_rest_proof()"
        )
        if server.request_count() != 1:
            raise AssertionError("preparing the scan performed network I/O")
        os.environ["DUCKDB_API_LIVE_PROOF_AUTHORITY"] = "https://rejected.invalid"
        prepared = connection.execute("EXECUTE live_rest_snapshot")
        prepared_types = [str(column[1]) for column in prepared.description]
        prepared_rows = prepared.fetchall()
        if prepared_types != ["BIGINT", "VARCHAR", "BOOLEAN"]:
            raise AssertionError(f"unexpected prepared types: {prepared_types!r}")
        if prepared_rows != EXPECTED_ROWS or server.request_count() != 2:
            raise AssertionError(
                "prepared scan did not retain its offline immutable plan"
            )
        assert_exact_request(server.last_request())
        connection.execute("DEALLOCATE live_rest_snapshot")
        configure_controlled_authority(server)
        return server.request_count()
    finally:
        configure_controlled_authority(server)
        connection.close()
        stop_oracle(server, server_thread)


def run_public_compatibility(extension_path: pathlib.Path) -> None:
    os.environ.pop("DUCKDB_API_LIVE_PROOF_AUTHORITY", None)
    connection = load_extension(extension_path)
    try:
        result = connection.execute("SELECT * FROM duckdb_api_live_rest_proof()")
        rows = result.fetchall()
        types = [str(column[1]) for column in result.description]
        if not rows or len(rows) > 3 or types != ["BIGINT", "VARCHAR", "BOOLEAN"]:
            raise AssertionError(
                f"unexpected public GitHub result: types={types!r} rows={rows!r}"
            )
        print(f"public compatibility evidence: rows={len(rows)}")
    finally:
        connection.close()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("extension", type=pathlib.Path)
    parser.add_argument("--real", action="store_true")
    arguments = parser.parse_args()
    request_count = run_controlled_success(arguments.extension.resolve())
    print(f"controlled live REST success evidence: requests={request_count}")
    if arguments.real:
        run_public_compatibility(arguments.extension.resolve())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env python3

"""Run the authenticated repository relation without printing row data."""

from __future__ import annotations

import argparse
import getpass
import json
import pathlib

import duckdb


EXPECTED_DUCKDB = ("v1.5.4", "08e34c447b", "Variegata")
EXPECTED_EXTENSION = ("duckdb_api", "0.5.0", True, False, "NOT_INSTALLED")
EXPECTED_SCHEMA = [
    ("id", "BIGINT"),
    ("full_name", "VARCHAR"),
    ("private", "BOOLEAN"),
    ("fork", "BOOLEAN"),
    ("archived", "BOOLEAN"),
]


def sql_literal(value: str) -> str:
    return "'" + value.replace("'", "''") + "'"


def read_token() -> str:
    token = getpass.getpass("Short-lived GitHub token: ")
    if not token:
        raise SystemExit("a non-empty token is required")
    return token


def main() -> int:
    parser = argparse.ArgumentParser(
        description=(
            "Validate the bounded authenticated repository relation using only "
            "schema, aggregate count, and fixed request-envelope metadata."
        )
    )
    parser.add_argument("artifact", help="path to duckdb_api.duckdb_extension")
    args = parser.parse_args()

    artifact = pathlib.Path(args.artifact).resolve(strict=True)
    sql_path = pathlib.Path(__file__).with_name("authenticated-repositories.sql")
    token = read_token()
    connection = duckdb.connect(config={"allow_unsigned_extensions": "true"})
    try:
        if connection.execute("PRAGMA version").fetchone() != EXPECTED_DUCKDB:
            raise SystemExit("DuckDB host identity is outside the supported cell")
        connection.execute(f"LOAD {sql_literal(artifact.as_posix())}")
        extension = connection.execute(
            """
            SELECT extension_name, extension_version, loaded, installed, install_mode
            FROM duckdb_extensions()
            WHERE extension_name = 'duckdb_api'
            """
        ).fetchone()
        if extension != EXPECTED_EXTENSION:
            raise SystemExit("extension identity is outside the 0.5.0 product cell")
        connection.execute(
            "CREATE TEMPORARY SECRET github_default "
            "(TYPE duckdb_api, PROVIDER config, TOKEN "
            f"{sql_literal(token)})"
        )
        token = ""

        statements = connection.extract_statements(
            sql_path.read_text(encoding="utf-8")
        )
        if len(statements) != 2:
            raise SystemExit("authenticated repository example statement count drifted")
        described = connection.execute(statements[0]).fetchall()
        schema = [(row[0], row[1]) for row in described]
        if schema != EXPECTED_SCHEMA:
            raise SystemExit("authenticated repository schema drifted")
        repository_count = connection.execute(statements[1]).fetchone()[0]
        if not isinstance(repository_count, int) or repository_count < 0:
            raise SystemExit("authenticated repository count drifted")
    finally:
        token = ""
        connection.close()

    print(
        json.dumps(
            {
                "artifact": artifact.name,
                "extension": {
                    "install_mode": extension[4],
                    "installed": extension[3],
                    "loaded": extension[2],
                    "name": extension[0],
                    "version": extension[1],
                },
                "relation": "github.authenticated_repositories",
                "repository_count": repository_count,
                "request_profile": {
                    "maximum_concurrency": 1,
                    "maximum_execution_seconds": 30,
                    "maximum_pages": 32,
                    "page_size": 100,
                    "retries": 0,
                },
                "schema": [list(column) for column in schema],
            },
            sort_keys=True,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

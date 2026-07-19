#!/usr/bin/env python3

"""Run the fixed authenticated GitHub relation with an interactively read token."""

from __future__ import annotations

import argparse
import getpass
import pathlib

import duckdb


EXPECTED_DUCKDB = ("v1.5.4", "08e34c447b", "Variegata")
EXPECTED_EXTENSION = ("duckdb_api", "0.6.0", True, False, "NOT_INSTALLED")
EXPECTED_SCHEMA = [
    ("id", "BIGINT"),
    ("login", "VARCHAR"),
    ("site_admin", "BOOLEAN"),
]


def sql_literal(value: str) -> str:
    return "'" + value.replace("'", "''") + "'"


def main() -> int:
    parser = argparse.ArgumentParser(
        description=(
            "Query the duckdb_api 0.6.0 authenticated GitHub relation. "
            "The credential is read from a hidden interactive prompt."
        )
    )
    parser.add_argument("artifact", help="path to duckdb_api.duckdb_extension")
    args = parser.parse_args()

    token = getpass.getpass("Short-lived GitHub token: ")
    if not token:
        raise SystemExit("a non-empty token is required")

    artifact = pathlib.Path(args.artifact).resolve(strict=True)
    query_path = pathlib.Path(__file__).with_name("authenticated-user.sql")
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
            raise SystemExit(f"extension identity mismatch: {extension!r}")
        connection.execute(
            "CREATE TEMPORARY SECRET github_default "
            "(TYPE duckdb_api, PROVIDER config, TOKEN "
            f"{sql_literal(token)})"
        )
        result = connection.execute(query_path.read_text(encoding="utf-8"))
        rows = result.fetchall()
        schema = [(column[0], str(column[1])) for column in result.description]
        if schema != EXPECTED_SCHEMA or len(rows) != 1:
            raise SystemExit(
                f"authenticated relation contract drifted: schema={schema!r}, rows={len(rows)}"
            )
    finally:
        connection.close()

    print("Schema " + ", ".join(f"{name} {kind}" for name, kind in schema))
    print("id\tlogin\tsite_admin")
    print(f"{rows[0][0]}\t{rows[0][1]}\t{str(rows[0][2]).lower()}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

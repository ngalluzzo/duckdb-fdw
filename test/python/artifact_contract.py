#!/usr/bin/env python3

from __future__ import annotations

import hashlib
import json
import os
import pathlib
import shutil
import sys
import tempfile

import duckdb


EXPECTED_DUCKDB = ("v1.5.4", "08e34c447b", "Variegata")
EXPECTED_EXTENSION = ("duckdb_api", "0.2.0", True, False, "NOT_INSTALLED")
EXPECTED_ROWS = [(1, "alpha", True), (2, "beta", False), (3, "gamma", True)]
EXPECTED_SCHEMA = [("id", "BIGINT"), ("name", "VARCHAR"), ("active", "BOOLEAN")]


def canonical_digest(value: object) -> str:
    encoded = json.dumps(value, sort_keys=True, separators=(",", ":")).encode()
    return hashlib.sha256(encoded).hexdigest()


def main() -> int:
    if len(sys.argv) != 2:
        raise SystemExit("usage: artifact_contract.py PATH_TO_EXTENSION")

    source_artifact = pathlib.Path(sys.argv[1]).resolve(strict=True)
    os.environ["DUCKDB_API_FIXTURE_SCENARIO"] = "malformed"
    os.environ["DUCKDB_API_CONNECTOR_PATH"] = "/top-secret/connector.yaml"

    with tempfile.TemporaryDirectory(prefix="duckdb-api-artifact-") as directory:
        isolated = pathlib.Path(directory)
        artifact = isolated / "duckdb_api.duckdb_extension"
        shutil.copyfile(source_artifact, artifact)
        (isolated / "connector.yaml").write_text("top-secret-decoy", encoding="utf-8")
        (isolated / "items.json").write_text("top-secret-decoy", encoding="utf-8")
        os.chdir(isolated)

        connection = duckdb.connect(config={"allow_unsigned_extensions": "true"})
        if connection.execute("PRAGMA version").fetchone() != EXPECTED_DUCKDB:
            raise AssertionError("artifact test host identity drifted")

        before_functions = set(
            connection.execute(
                "SELECT function_name, function_type FROM duckdb_functions()"
            ).fetchall()
        )
        before_settings = set(
            connection.execute("SELECT name FROM duckdb_settings()").fetchall()
        )
        before_types = set(
            connection.execute(
                "SELECT database_name, schema_name, type_name FROM duckdb_types()"
            ).fetchall()
        )

        connection.execute(f"LOAD '{artifact.as_posix()}'")

        extension = connection.execute(
            """
            SELECT extension_name, extension_version, loaded, installed, install_mode
            FROM duckdb_extensions()
            WHERE extension_name = 'duckdb_api'
            """
        ).fetchone()
        if extension != EXPECTED_EXTENSION:
            raise AssertionError(f"unexpected extension identity: {extension!r}")

        added_functions = set(
            connection.execute(
                "SELECT function_name, function_type FROM duckdb_functions()"
            ).fetchall()
        ) - before_functions
        if added_functions != {("duckdb_api_scan", "table")}:
            raise AssertionError(f"unexpected public function inventory: {added_functions!r}")

        functions = connection.execute(
            """
            SELECT parameters, parameter_types
            FROM duckdb_functions()
            WHERE function_name = 'duckdb_api_scan'
            """
        ).fetchall()
        if len(functions) != 1 or set(zip(functions[0][0], functions[0][1])) != {
            ("connector", "VARCHAR"),
            ("relation", "VARCHAR"),
        }:
            raise AssertionError(f"unexpected named function signature: {functions!r}")

        added_settings = set(
            connection.execute("SELECT name FROM duckdb_settings()").fetchall()
        ) - before_settings
        if added_settings:
            raise AssertionError(f"extension registered settings: {added_settings!r}")
        added_types = set(
            connection.execute(
                "SELECT database_name, schema_name, type_name FROM duckdb_types()"
            ).fetchall()
        ) - before_types
        if added_types:
            raise AssertionError(f"extension registered types: {added_types!r}")

        query = connection.execute(
            """
            SELECT id, name, active
            FROM duckdb_api_scan(connector := 'example', relation := 'items')
            ORDER BY id
            """
        )
        rows = query.fetchall()
        schema = [(description[0], str(description[1])) for description in query.description]
        if rows != EXPECTED_ROWS or schema != EXPECTED_SCHEMA:
            raise AssertionError(f"public query contract drifted: {schema!r} {rows!r}")
        if any(value is None for row in rows for value in row):
            raise AssertionError("non-null example schema produced a NULL")

        filtered = connection.execute(
            """
            SELECT id
            FROM duckdb_api_scan(connector := 'example', relation := 'items')
            WHERE NOT active
            """
        ).fetchall()
        ordered = connection.execute(
            """
            SELECT id
            FROM duckdb_api_scan(connector := 'example', relation := 'items')
            ORDER BY id DESC
            """
        ).fetchall()
        offset = connection.execute(
            """
            SELECT id
            FROM duckdb_api_scan(connector := 'example', relation := 'items')
            ORDER BY id
            LIMIT 1 OFFSET 1
            """
        ).fetchall()
        dependent = connection.execute(
            """
            SELECT id
            FROM duckdb_api_scan(connector := 'example', relation := 'items')
            WHERE active
            ORDER BY id
            LIMIT 1 OFFSET 1
            """
        ).fetchall()
        if filtered != [(2,)] or ordered != [(3,), (2,), (1,)] or offset != [(2,)] or dependent != [(3,)]:
            raise AssertionError("DuckDB-local relational ownership drifted")

        behavior = {
            "duckdb": list(EXPECTED_DUCKDB[:2]),
            "extension": ["duckdb_api", "0.2.0"],
            "function": {
                "name": "duckdb_api_scan",
                "named_parameters": {"connector": "VARCHAR", "relation": "VARCHAR"},
            },
            "schema": EXPECTED_SCHEMA,
            "rows": EXPECTED_ROWS,
            "added_settings": [],
            "added_types": [],
        }
        print(
            json.dumps(
                {
                    "behavior": behavior,
                    "behavior_sha256": canonical_digest(behavior),
                },
                sort_keys=True,
            )
        )
        connection.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

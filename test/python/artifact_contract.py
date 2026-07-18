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
EXPECTED_EXTENSION = ("duckdb_api", "0.3.0", True, False, "NOT_INSTALLED")
EXPECTED_SCHEMA = [
    ("id", "BIGINT"),
    ("login", "VARCHAR"),
    ("site_admin", "BOOLEAN"),
]
FORBIDDEN_ARTIFACT_MARKERS = (
    b"127.0.0.1",
    b"BuildControlledHttpRuntime",
    b"controlled_duckdb_api",
    b"DUCKDB_API_CONNECTOR_PATH",
    b"DUCKDB_API_FIXTURE_SCENARIO",
    b"DUCKDB_API_LIVE_PROOF_AUTHORITY",
)


def canonical_digest(value: object) -> str:
    encoded = json.dumps(value, sort_keys=True, separators=(",", ":")).encode()
    return hashlib.sha256(encoded).hexdigest()


def expect_bind_error(connection: duckdb.DuckDBPyConnection, sql: str, suffix: str) -> None:
    try:
        connection.execute(sql).fetchall()
    except duckdb.BinderException as error:
        diagnostic = str(error)
        if not diagnostic.endswith(suffix):
            raise AssertionError(f"unexpected bind diagnostic: {diagnostic!r}") from error
        if "top-secret" in diagnostic:
            raise AssertionError(f"bind diagnostic leaked ambient context: {diagnostic!r}")
    else:
        raise AssertionError(f"query unexpectedly bound: {sql}")


def validate_rows(rows: list[tuple[object, ...]]) -> None:
    if len(rows) > 3:
        raise AssertionError(f"public relation exceeded its three-row domain: {rows!r}")
    for row in rows:
        if len(row) != 3 or any(value is None for value in row):
            raise AssertionError(f"public relation violated its required schema: {row!r}")
        if (
            not isinstance(row[0], int)
            or isinstance(row[0], bool)
            or not isinstance(row[1], str)
            or not isinstance(row[2], bool)
        ):
            raise AssertionError(f"public relation returned an incompatible scalar: {row!r}")


def main() -> int:
    if len(sys.argv) != 2:
        raise SystemExit("usage: artifact_contract.py PATH_TO_EXTENSION")

    repository_root = pathlib.Path(__file__).resolve().parents[2]
    expected_behavior = json.loads(
        (repository_root / "release/0.3.0/public_contract.json").read_text()
    )
    source_artifact = pathlib.Path(sys.argv[1]).resolve(strict=True)
    artifact_bytes = source_artifact.read_bytes()
    present_markers = [
        marker.decode()
        for marker in FORBIDDEN_ARTIFACT_MARKERS
        if marker in artifact_bytes
    ]
    if present_markers:
        raise AssertionError(
            f"installed artifact contains private composition markers: {present_markers!r}"
        )

    with tempfile.TemporaryDirectory(prefix="duckdb-api-artifact-") as directory:
        isolated = pathlib.Path(directory)
        artifact = isolated / "duckdb_api.duckdb_extension"
        shutil.copyfile(source_artifact, artifact)
        home = isolated / "home"
        home.mkdir()
        (home / ".curlrc").write_text(
            "url = http://127.0.0.1:1/top-secret-curlrc\n", encoding="utf-8"
        )
        (home / ".netrc").write_text(
            "machine api.github.com login top-secret password top-secret\n",
            encoding="utf-8",
        )
        (isolated / "connector.yaml").write_text("top-secret-decoy", encoding="utf-8")
        (isolated / "items.json").write_text("top-secret-decoy", encoding="utf-8")
        (isolated / "ca-bundle.pem").write_text("top-secret-decoy", encoding="utf-8")
        os.chdir(isolated)

        os.environ.update(
            {
                "ALL_PROXY": "http://127.0.0.1:1",
                "CURL_CA_BUNDLE": str(isolated / "ca-bundle.pem"),
                "DUCKDB_API_CONNECTOR_PATH": "/top-secret/connector.yaml",
                "DUCKDB_API_FIXTURE_SCENARIO": "malformed-top-secret",
                "DUCKDB_API_LIVE_PROOF_AUTHORITY": "http://127.0.0.1:1",
                "GITHUB_TOKEN": "top-secret-token",
                "HOME": str(home),
                "HTTPS_PROXY": "http://127.0.0.1:1",
                "HTTP_PROXY": "http://127.0.0.1:1",
                "NO_PROXY": "",
                "SSL_CERT_FILE": str(isolated / "ca-bundle.pem"),
            }
        )

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

        diagnostics = expected_behavior["diagnostics"]
        expect_bind_error(
            connection,
            "SELECT * FROM duckdb_api_scan(connector := 'example', relation := 'items')",
            diagnostics["unknown_connector"],
        )
        expect_bind_error(
            connection,
            "SELECT * FROM duckdb_api_scan(connector := 'github', relation := 'items')",
            diagnostics["unknown_relation"],
        )
        expect_bind_error(
            connection,
            "SELECT * FROM duckdb_api_scan(connector := 'github')",
            diagnostics["missing_relation"],
        )

        description = connection.execute(
            """
            DESCRIBE SELECT * FROM duckdb_api_scan(
                connector := 'github', relation := 'duckdb_login_search_page'
            )
            """
        ).fetchall()
        described_schema = [(row[0], row[1]) for row in description]
        if described_schema != EXPECTED_SCHEMA:
            raise AssertionError(f"offline bind schema drifted: {description!r}")

        connection.execute(
            """
            PREPARE public_scan AS
            SELECT id, login, site_admin
            FROM duckdb_api_scan(
                connector := 'github', relation := 'duckdb_login_search_page'
            )
            ORDER BY id
            """
        )
        query = connection.execute("EXECUTE public_scan")
        rows = query.fetchall()
        schema = [(column[0], str(column[1])) for column in query.description]
        connection.execute("DEALLOCATE public_scan")
        if schema != EXPECTED_SCHEMA:
            raise AssertionError(f"public query schema drifted: {schema!r}")
        validate_rows(rows)

        behavior = {
            "added_settings": [],
            "added_types": [],
            "authority_inputs": [],
            "diagnostics": diagnostics,
            "duckdb": list(EXPECTED_DUCKDB[:2]),
            "extension": ["duckdb_api", "0.3.0"],
            "function": {
                "name": "duckdb_api_scan",
                "named_parameters": {
                    "connector": "VARCHAR",
                    "relation": "VARCHAR",
                },
            },
            "relation": {
                "cardinality": {"maximum": 3, "minimum": 0},
                "connector": "github",
                "domain": "single_fixed_github_search_response_page",
                "duckdb_visible_not_null": False,
                "name": "duckdb_login_search_page",
                "public_row_identity": "not_guaranteed",
                "public_row_order": "not_guaranteed",
                "required_values": True,
                "schema": [list(column) for column in EXPECTED_SCHEMA],
            },
            "relational_ownership": {
                "filter": "duckdb",
                "limit": "duckdb",
                "offset": "duckdb",
                "ordering": "duckdb",
            },
            "removed_relations": [{"connector": "example", "relation": "items"}],
        }
        if behavior != expected_behavior:
            raise AssertionError("observed public inventory disagrees with the 0.3.0 contract")
        print(
            json.dumps(
                {
                    "behavior": behavior,
                    "behavior_sha256": canonical_digest(behavior),
                    "public_observation": {"row_count": len(rows)},
                },
                sort_keys=True,
            )
        )
        connection.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

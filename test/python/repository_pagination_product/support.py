"""SQL, request, credential, and redaction helpers for repository oracles."""

from __future__ import annotations

import os
import pathlib
from typing import Iterable

import duckdb

from live_rest_product.support import load_controlled_extension

from .service import (
    HOSTILE_NEXT,
    RESPONSE_SECRET,
    RepositoryOracleServer,
    repository_response,
)


TOKEN = "query-repository-product-token"
REPOSITORY_SCHEMA = [
    ("id", "BIGINT"),
    ("full_name", "VARCHAR"),
    ("private", "BOOLEAN"),
    ("fork", "BOOLEAN"),
    ("archived", "BOOLEAN"),
]
REPOSITORY_SCAN = (
    "FROM duckdb_api_scan(connector := 'github', "
    "relation := 'authenticated_repositories', secret := 'github_default')"
)
ORDERED_SQL = (
    "SELECT id, full_name, private, fork, archived "
    f"{REPOSITORY_SCAN} ORDER BY id"
)


def configure_controlled_environment(server: RepositoryOracleServer) -> None:
    os.environ["DUCKDB_API_CONTROLLED_PORT"] = str(server.server_port)
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
    os.environ["DUCKDB_API_CONNECTOR_PATH"] = "/private/ignored-connector.yaml"
    os.environ["DUCKDB_API_FIXTURE_SCENARIO"] = "ignored-repository-mode"
    os.environ["DUCKDB_API_LIVE_PROOF_AUTHORITY"] = "https://rejected.invalid"
    os.environ["GITHUB_TOKEN"] = "ambient-token-must-not-be-used"


def create_temporary_secret(
    connection: duckdb.DuckDBPyConnection, token: str = TOKEN
) -> None:
    escaped = token.replace("'", "''")
    connection.execute(
        "CREATE TEMPORARY SECRET github_default "
        f"(TYPE duckdb_api, PROVIDER config, TOKEN '{escaped}')"
    )


def load_repository_connection(
    extension_path: pathlib.Path, server: RepositoryOracleServer
) -> duckdb.DuckDBPyConnection:
    configure_controlled_environment(server)
    connection = load_controlled_extension(extension_path)
    create_temporary_secret(connection)
    return connection


def page_paths(pages: Iterable[int]) -> list[str]:
    return [f"/user/repos?per_page=100&page={page}" for page in pages]


def assert_exact_requests(
    server: RepositoryOracleServer, expected_paths: list[str]
) -> None:
    requests = server.requests()
    if len(requests) != len(expected_paths):
        raise AssertionError("repository request count drifted")
    for request, expected_path in zip(requests, expected_paths, strict=True):
        if request["method"] != "GET" or request["path"] != expected_path:
            raise AssertionError("repository request sequence drifted")
        headers = request["headers"]
        required = {
            "host": [f"127.0.0.1:{server.server_port}"],
            "accept": ["application/vnd.github+json"],
            "authorization": [f"Bearer {TOKEN}"],
            "user-agent": ["duckdb-api/0.5.0"],
            "x-github-api-version": ["2022-11-28"],
        }
        for name, expected in required.items():
            if headers.get(name) != expected:
                raise AssertionError(f"repository request header {name!r} drifted")
        if "proxy-authorization" in headers or "cookie" in headers:
            raise AssertionError("repository request carried ambient credentials")


def assert_safe_failure(
    error: BaseException,
    server: RepositoryOracleServer,
    category: str,
    field: str = "",
) -> None:
    diagnostic = str(error)
    required = [category, "connector=github", "relation=authenticated_repositories"]
    if field:
        required.append(f"field={field}")
    if any(value not in diagnostic for value in required):
        raise AssertionError("repository failure diagnostic category drifted") from None
    assert_redacted(error, server)


def assert_redacted(
    error: BaseException, server: RepositoryOracleServer
) -> None:
    diagnostic = str(error)
    forbidden = (
        TOKEN,
        RESPONSE_SECRET,
        HOSTILE_NEXT,
        "Authorization",
        "Bearer ",
        "/user/repos",
        "api.github.com",
        "credential-canary",
        "synthetic/",
        str(server.server_port),
    )
    if any(value in diagnostic for value in forbidden):
        raise AssertionError("repository failure diagnostic exposed private data") from None


def assert_recovery(
    connection: duckdb.DuckDBPyConnection, server: RepositoryOracleServer
) -> None:
    recovered_row = (909, "synthetic/recovered", False, False, False)
    server.configure([repository_response([recovered_row])])
    rows = connection.execute(ORDERED_SQL).fetchall()
    if rows != [recovered_row]:
        raise AssertionError("repository scan did not recover independently")
    assert_exact_requests(server, page_paths([1]))

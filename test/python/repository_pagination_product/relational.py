"""Successful bag, schema, ordering, and conservative relational oracles."""

from __future__ import annotations

import pathlib

from .service import RepositoryOracleServer, ResponseSpec, repository_response
from .support import (
    ORDERED_SQL,
    REPOSITORY_SCAN,
    REPOSITORY_SCHEMA,
    assert_exact_requests,
    load_repository_connection,
    page_paths,
)


EXPECTED_BAG = [
    (10, "synthetic/first", False, False, False),
    (10, "synthetic/first", False, False, False),
    (20, "synthetic/second", False, True, True),
    (30, "synthetic/third", True, False, False),
]


def multi_page_responses() -> list[ResponseSpec]:
    return [
        repository_response(
            [EXPECTED_BAG[3], EXPECTED_BAG[0]],
            next_page=2,
        ),
        repository_response([], next_page=3),
        repository_response([EXPECTED_BAG[1], EXPECTED_BAG[2]]),
    ]


def run_relational_contract(
    extension_path: pathlib.Path, server: RepositoryOracleServer
) -> None:
    connection = load_repository_connection(extension_path, server)
    try:
        server.configure(multi_page_responses())
        result = connection.execute(ORDERED_SQL)
        rows = result.fetchall()
        schema = [(column[0], str(column[1])) for column in result.description]
        if schema != REPOSITORY_SCHEMA:
            raise AssertionError("repository relation schema drifted")
        if rows != EXPECTED_BAG:
            raise AssertionError("repository duplicate-preserving ordered bag drifted")
        assert_exact_requests(server, page_paths([1, 2, 3]))

        server.configure(multi_page_responses())
        local_rows = connection.execute(
            "SELECT id, full_name "
            f"{REPOSITORY_SCAN} WHERE NOT private ORDER BY id DESC LIMIT 2 OFFSET 1"
        ).fetchall()
        if local_rows != [
            (10, "synthetic/first"),
            (10, "synthetic/first"),
        ]:
            raise AssertionError("DuckDB-local repository operators changed the row bag")
        assert_exact_requests(server, page_paths([1, 2, 3]))

        single = (7, "synthetic/single", False, False, False)
        server.configure([repository_response([single])])
        if connection.execute(ORDERED_SQL).fetchall() != [single]:
            raise AssertionError("single-page repository exhaustion drifted")
        assert_exact_requests(server, page_paths([1]))
    finally:
        connection.close()

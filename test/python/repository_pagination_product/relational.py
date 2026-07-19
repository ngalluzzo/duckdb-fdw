"""Successful bag, schema, ordering, and conservative relational oracles."""

from __future__ import annotations

import pathlib
import threading

from .service import RepositoryOracleServer, ResponseSpec, repository_response
from .support import (
    ORDERED_SQL,
    REPOSITORY_SCAN,
    REPOSITORY_SCHEMA,
    assert_exact_requests,
    assert_request_paths_unordered,
    load_repository_connection,
    page_paths,
)


EXPECTED_BAG = [
    (10, "synthetic/first", False, False, False, "public"),
    (10, "synthetic/first", False, False, False, "public"),
    (20, "synthetic/second", False, True, True, "public"),
    (25, "synthetic/internal", True, False, False, "internal"),
    (30, "synthetic/private-a", True, False, False, "private"),
    (40, "synthetic/private-b", True, False, True, "private"),
]


def multi_page_responses() -> list[ResponseSpec]:
    return [
        repository_response(
            [EXPECTED_BAG[5], EXPECTED_BAG[0]],
            next_page=2,
        ),
        repository_response([], next_page=3),
        repository_response(
            [EXPECTED_BAG[1], EXPECTED_BAG[2], EXPECTED_BAG[3], EXPECTED_BAG[4]]
        ),
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
        forced_local = connection.execute(
            "SELECT id, full_name, visibility "
            f"{REPOSITORY_SCAN} "
            "WHERE concat(visibility, '') = 'private' ORDER BY id"
        ).fetchall()
        if forced_local != [
            (30, "synthetic/private-a", "private"),
            (40, "synthetic/private-b", "private"),
        ]:
            raise AssertionError("forced-local visibility baseline drifted")
        assert_exact_requests(server, page_paths([1, 2, 3]))

        server.configure(multi_page_responses())
        ambiguous_fallback = connection.execute(
            "SELECT id, full_name, visibility "
            f"{REPOSITORY_SCAN} "
            "WHERE visibility = 'private' OR archived = FALSE ORDER BY id"
        ).fetchall()
        if ambiguous_fallback != [
            (10, "synthetic/first", "public"),
            (10, "synthetic/first", "public"),
            (25, "synthetic/internal", "internal"),
            (30, "synthetic/private-a", "private"),
            (40, "synthetic/private-b", "private"),
        ]:
            raise AssertionError(
                "ambiguous predicate fallback lost rows from the complete remote domain"
            )
        assert_exact_requests(server, page_paths([1, 2, 3]))

        mapping_absent_connection = load_repository_connection(
            extension_path, server, predicate_mapping="absent"
        )
        try:
            server.configure(multi_page_responses())
            mapping_absent = mapping_absent_connection.execute(
                "SELECT id, full_name, visibility "
                f"{REPOSITORY_SCAN} WHERE visibility = 'private' ORDER BY id"
            ).fetchall()
            if mapping_absent != forced_local:
                raise AssertionError(
                    "mapping-absent composition changed the approved SQL result"
                )
            assert_exact_requests(server, page_paths([1, 2, 3]))
        finally:
            mapping_absent_connection.close()

        server.configure(
            [
                repository_response(
                    [EXPECTED_BAG[5]], next_page=2, selective=True
                ),
                repository_response([EXPECTED_BAG[4]], selective=True),
            ]
        )
        selective = connection.execute(
            "SELECT id, full_name, visibility "
            f"{REPOSITORY_SCAN} WHERE visibility = 'private' ORDER BY id"
        ).fetchall()
        if selective != forced_local:
            raise AssertionError(
                "selective repository result differs from complete traversal plus local filtering"
            )
        assert_exact_requests(server, page_paths([1, 2], selective=True))

        server.configure(
            [
                repository_response(
                    [EXPECTED_BAG[5]], next_page=2, selective=True
                ),
                repository_response([EXPECTED_BAG[4]], selective=True),
            ]
        )
        compound_selective = connection.execute(
            "SELECT id, full_name, visibility "
            f"{REPOSITORY_SCAN} "
            "WHERE visibility = 'private' AND NOT archived ORDER BY id"
        ).fetchall()
        if compound_selective != [(30, "synthetic/private-a", "private")]:
            raise AssertionError(
                "compound selective repository query lost its complete DuckDB residual"
            )
        assert_exact_requests(server, page_paths([1, 2], selective=True))

        connection.execute(
            "PREPARE visibility_parameter AS "
            "SELECT id, visibility "
            f"{REPOSITORY_SCAN} WHERE visibility = $1 ORDER BY id"
        )
        private_responses = [
            repository_response([EXPECTED_BAG[5]], next_page=2, selective=True),
            repository_response([EXPECTED_BAG[4]], selective=True),
        ]
        server.configure(private_responses)
        first_private = connection.execute(
            "EXECUTE visibility_parameter('private')"
        ).fetchall()
        if first_private != [(30, "private"), (40, "private")]:
            raise AssertionError("prepared private execution returned the wrong rows")
        assert_exact_requests(server, page_paths([1, 2], selective=True))

        server.configure(multi_page_responses())
        public_rows = connection.execute(
            "EXECUTE visibility_parameter('public')"
        ).fetchall()
        if public_rows != [(10, "public"), (10, "public"), (20, "public")]:
            raise AssertionError("prepared public fallback returned the wrong rows")
        assert_exact_requests(server, page_paths([1, 2, 3]))

        server.configure(multi_page_responses())
        if connection.execute("EXECUTE visibility_parameter(NULL)").fetchall() != []:
            raise AssertionError("prepared NULL fallback did not preserve SQL null semantics")
        # DuckDB can prove `visibility = NULL` is never true and omit the scan
        # entirely. This is distinct from remote predicate optimization: no
        # request is admitted, so no selective state can leak into execution.
        assert_exact_requests(server, [])

        server.configure(private_responses)
        second_private = connection.execute(
            "EXECUTE visibility_parameter('private')"
        ).fetchall()
        if second_private != first_private:
            raise AssertionError("prepared private plan leaked across fallback executions")
        assert_exact_requests(server, page_paths([1, 2], selective=True))
        connection.execute("DEALLOCATE visibility_parameter")

        first_concurrent = load_repository_connection(extension_path, server)
        second_concurrent = load_repository_connection(extension_path, server)
        try:
            for concurrent in (first_concurrent, second_concurrent):
                concurrent.execute(
                    "PREPARE concurrent_visibility AS "
                    "SELECT id, visibility "
                    f"{REPOSITORY_SCAN} WHERE visibility = $1 ORDER BY id"
                )
            public_paths = page_paths([1, 2, 3])
            private_paths = page_paths([1, 2], selective=True)
            server.configure_routes(
                {
                    public_paths[0]: multi_page_responses()[0],
                    public_paths[1]: multi_page_responses()[1],
                    public_paths[2]: multi_page_responses()[2],
                    private_paths[0]: repository_response(
                        [EXPECTED_BAG[5]], next_page=2, selective=True
                    ),
                    private_paths[1]: repository_response(
                        [EXPECTED_BAG[4]], selective=True
                    ),
                },
                synchronized_paths=(public_paths[0], private_paths[0]),
            )
            results: dict[str, list[tuple[object, ...]]] = {}
            errors: list[BaseException] = []

            def execute_prepared(
                name: str,
                concurrent: object,
                value: str,
            ) -> None:
                try:
                    results[name] = concurrent.execute(
                        f"EXECUTE concurrent_visibility('{value}')"
                    ).fetchall()
                except BaseException as error:  # surfaced after both workers join
                    errors.append(error)

            private_worker = threading.Thread(
                target=execute_prepared,
                args=("private", first_concurrent, "private"),
            )
            public_worker = threading.Thread(
                target=execute_prepared,
                args=("public", second_concurrent, "public"),
            )
            private_worker.start()
            public_worker.start()
            private_worker.join(timeout=10)
            public_worker.join(timeout=10)
            if private_worker.is_alive() or public_worker.is_alive() or errors:
                raise AssertionError(
                    f"concurrent prepared executions did not complete safely: {errors!r}"
                )
            if results.get("private") != [(30, "private"), (40, "private")]:
                raise AssertionError("concurrent private execution inherited fallback state")
            if results.get("public") != [
                (10, "public"),
                (10, "public"),
                (20, "public"),
            ]:
                raise AssertionError("concurrent public execution inherited selective state")
            assert_request_paths_unordered(server, public_paths + private_paths)
        finally:
            first_concurrent.close()
            second_concurrent.close()

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

        single = (7, "synthetic/single", False, False, False, "public")
        server.configure([repository_response([single])])
        if connection.execute(ORDERED_SQL).fetchall() != [single]:
            raise AssertionError("single-page repository exhaustion drifted")
        assert_exact_requests(server, page_paths([1]))
    finally:
        connection.close()

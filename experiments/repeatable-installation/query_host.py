#!/usr/bin/env python3
"""Observe one DuckDB process for the repeatable-installation trial.

Query Experience owns this DuckDB-facing edge. Each invocation creates one
connection in one process and emits one JSON observation. The coordinating
oracle deliberately invokes this file as a subprocess so installation,
restart, and rejection cannot be collapsed into hidden in-process state.

This is experimental evidence code, not a supported installation interface.
It may load only the fixture-backed 0.1.0 artifact supplied by the trial.
"""

from __future__ import annotations

import argparse
import json
import os
import pathlib
from collections.abc import Sequence

import duckdb


EXTENSION_NAME = "duckdb_api"
FUNCTION_NAME = "duckdb_api_scan"
QUERY = """
SELECT id, name, active
FROM duckdb_api_scan(connector := 'example', relation := 'items')
ORDER BY id
"""


def sql_literal(value: str) -> str:
    """Quote a filesystem path as one DuckDB string literal."""

    return "'" + value.replace("'", "''") + "'"


def connection_config(
    extension_directory: pathlib.Path, allow_unsigned: bool
) -> dict[str, str]:
    """Return an isolated extension policy for one trial host.

    Automatic installation and loading stay disabled so a successful probe can
    only result from the action requested by the coordinating oracle.
    """

    config = {
        "autoinstall_known_extensions": "false",
        "autoload_known_extensions": "false",
        "extension_directory": str(extension_directory),
    }
    if allow_unsigned:
        config["allow_unsigned_extensions"] = "true"
    return config


def extension_record(
    connection: duckdb.DuckDBPyConnection,
) -> dict[str, object] | None:
    """Return the installed extension record visible to the current host."""

    row = connection.execute(
        """
        SELECT extension_name, extension_version, loaded, installed,
               install_mode, installed_from, install_path
        FROM duckdb_extensions()
        WHERE extension_name = ?
        """,
        [EXTENSION_NAME],
    ).fetchone()
    if row is None:
        return None
    return {
        "name": row[0],
        "version": row[1],
        "loaded": row[2],
        "installed": row[3],
        "install_mode": row[4],
        "installed_from": row[5],
        "install_path": row[6],
    }


def registered_functions(connection: duckdb.DuckDBPyConnection) -> list[list[str]]:
    """Inventory only extension-namespaced functions after an attempted action."""

    rows = connection.execute(
        """
        SELECT function_name, function_type
        FROM duckdb_functions()
        WHERE function_name LIKE 'duckdb_api%'
        ORDER BY function_name, function_type
        """
    ).fetchall()
    return [[str(name), str(function_type)] for name, function_type in rows]


def setting_inventory(connection: duckdb.DuckDBPyConnection) -> set[str]:
    rows = connection.execute("SELECT name FROM duckdb_settings()").fetchall()
    return {str(row[0]) for row in rows}


def type_inventory(connection: duckdb.DuckDBPyConnection) -> set[tuple[str, str, str]]:
    return {
        (str(database), str(schema), str(name))
        for database, schema, name in connection.execute(
            "SELECT database_name, schema_name, type_name FROM duckdb_types()"
        ).fetchall()
    }


def base_observation(
    action: str, connection: duckdb.DuckDBPyConnection
) -> dict[str, object]:
    """Identify the concrete process and DuckDB build behind an observation."""

    version = connection.execute("PRAGMA version").fetchone()
    if version is None:
        raise AssertionError("DuckDB did not report a version")
    return {
        "action": action,
        "duckdb": [str(value) for value in version[:2]],
        "pid": os.getpid(),
    }


def observe_install(
    connection: duckdb.DuckDBPyConnection, artifact: pathlib.Path
) -> dict[str, object]:
    """Attempt one INSTALL and report state even when DuckDB rejects it."""

    observation = base_observation("install", connection)
    try:
        connection.execute(f"INSTALL {sql_literal(artifact.as_posix())}")
    except Exception as error:  # DuckDB exception classes vary across pinned hosts.
        observation.update(
            {
                "diagnostic": str(error),
                "error_type": type(error).__name__,
                "extension": extension_record(connection),
                "ok": False,
                "registered_functions": registered_functions(connection),
            }
        )
        return observation

    observation.update(
        {
            "diagnostic": None,
            "error_type": None,
            "extension": extension_record(connection),
            "ok": True,
            "registered_functions": registered_functions(connection),
        }
    )
    return observation


def function_contract(connection: duckdb.DuckDBPyConnection) -> dict[str, object]:
    rows = connection.execute(
        """
        SELECT parameters, parameter_types
        FROM duckdb_functions()
        WHERE function_name = ?
        """,
        [FUNCTION_NAME],
    ).fetchall()
    if len(rows) != 1:
        raise AssertionError(f"unexpected {FUNCTION_NAME} inventory: {rows!r}")
    parameters, parameter_types = rows[0]
    return {
        "name": FUNCTION_NAME,
        "named_parameters": {
            str(name): str(data_type)
            for name, data_type in zip(parameters, parameter_types, strict=True)
        },
    }


def observe_load_and_query(
    connection: duckdb.DuckDBPyConnection,
) -> dict[str, object]:
    """Load by name and return the complete 0.1.0 public behavior contract."""

    observation = base_observation("load-query", connection)
    before_settings = setting_inventory(connection)
    before_types = type_inventory(connection)
    try:
        connection.execute(f"LOAD {EXTENSION_NAME}")
        query = connection.execute(QUERY)
        rows = query.fetchall()
        schema = [[column[0], str(column[1])] for column in query.description]
        extension = extension_record(connection)
        if extension is None:
            raise AssertionError("loaded extension is absent from DuckDB inventory")
        behavior = {
            "added_settings": sorted(setting_inventory(connection) - before_settings),
            "added_types": [
                list(value) for value in sorted(type_inventory(connection) - before_types)
            ],
            "duckdb": observation["duckdb"],
            "extension": [extension["name"], extension["version"]],
            "function": function_contract(connection),
            "rows": [list(row) for row in rows],
            "schema": schema,
        }
    except Exception as error:
        observation.update(
            {
                "behavior": None,
                "diagnostic": str(error),
                "error_type": type(error).__name__,
                "extension": extension_record(connection),
                "ok": False,
                "registered_functions": registered_functions(connection),
            }
        )
        return observation

    observation.update(
        {
            "behavior": behavior,
            "diagnostic": None,
            "error_type": None,
            "extension": extension,
            "ok": True,
            "registered_functions": registered_functions(connection),
        }
    )
    return observation


def parse_args(argv: Sequence[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Observe one DuckDB process for the installation trial."
    )
    parser.add_argument("action", choices=("install", "load-query"))
    parser.add_argument("--database", required=True)
    parser.add_argument("--extension-directory", required=True)
    parser.add_argument("--artifact", help="artifact path required by install")
    parser.add_argument("--allow-unsigned", action="store_true")
    return parser.parse_args(argv)


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(argv)
    database = pathlib.Path(args.database).expanduser().absolute()
    extension_directory = pathlib.Path(args.extension_directory).expanduser().absolute()
    database.parent.mkdir(parents=True, exist_ok=True)
    extension_directory.mkdir(parents=True, exist_ok=True)

    artifact: pathlib.Path | None = None
    if args.action == "install":
        if args.artifact is None:
            raise SystemExit("--artifact is required for install")
        artifact = pathlib.Path(args.artifact).resolve(strict=True)
    elif args.artifact is not None:
        raise SystemExit("--artifact is not valid for load-query")

    connection = duckdb.connect(
        database=str(database),
        config=connection_config(extension_directory, args.allow_unsigned),
    )
    try:
        if args.action == "install":
            assert artifact is not None
            observation = observe_install(connection, artifact)
        else:
            observation = observe_load_and_query(connection)
    finally:
        connection.close()

    print(json.dumps(observation, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

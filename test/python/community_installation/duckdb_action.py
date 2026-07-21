#!/usr/bin/env python3
"""Execute one DuckDB Community action and emit one protocol observation.

This child owns exactly one connection, closes it on every path, and contains
the DuckDB-facing SQL/catalog coupling.  It deliberately has no unsigned-policy
option.  Build, custody, support eligibility, process cleanup, and evidence
retention belong to other Query or provider boundaries.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import pathlib
import stat
from collections.abc import Sequence
from typing import Any

try:
    from .host_protocol import HOST_OBSERVATION_SCHEMA
except ImportError:
    from host_protocol import HOST_OBSERVATION_SCHEMA


EXTENSION_NAME = "duckdb_api"
# Accepted RFC 0012 removed the generic duckdb_api_scan dispatcher before the
# 0.9.0 API candidate froze. duckdb_api_load_connector is a management
# function registered unconditionally at Load(), so it remains a valid
# representative for "the extension's query surface is registered."
FUNCTION_NAME = "duckdb_api_load_connector"


class ActionError(ValueError):
    """The child received an unusable state capability or action input."""


def inherited_artifact_path(value: str) -> pathlib.Path:
    """Admit only one inherited regular-file descriptor without resolving it."""

    candidate = pathlib.PurePosixPath(value)
    if (
        len(candidate.parts) != 4
        or candidate.parts[:3] != ("/", "dev", "fd")
        or not candidate.parts[3].isdigit()
    ):
        raise ActionError("incompatible artifact must be an inherited descriptor")
    descriptor = int(candidate.parts[3])
    try:
        metadata = os.fstat(descriptor)
    except OSError as error:
        raise ActionError("incompatible artifact descriptor is unavailable") from error
    if descriptor <= 2 or not stat.S_ISREG(metadata.st_mode):
        raise ActionError("incompatible artifact descriptor is not a regular file")
    return pathlib.Path(value)


def connection_config(extension_directory: pathlib.Path) -> dict[str, str]:
    """Return the complete Query-owned config with default signature policy."""

    return {
        "autoinstall_known_extensions": "false",
        "autoload_known_extensions": "false",
        "extension_directory": str(extension_directory),
    }


def sql_literal(value: str) -> str:
    return "'" + value.replace("'", "''") + "'"


def file_sha256(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def extension_record(connection: Any) -> dict[str, object] | None:
    row = connection.execute(
        """
        SELECT extension_name, extension_version, loaded, installed,
               install_mode, installed_from, install_path
        FROM duckdb_extensions()
        WHERE extension_name = 'duckdb_api'
        """
    ).fetchone()
    if row is None or row[3] is not True:
        return None
    install_path = pathlib.Path(str(row[6])).resolve(strict=True)
    installed_from = str(row[5])
    return {
        "artifact_sha256": file_sha256(install_path),
        "install_path": str(install_path),
        "install_source": (
            "community" if installed_from == "community" else installed_from
        ),
        "installed": bool(row[3]),
        "loaded": bool(row[2]),
        "name": str(row[0]),
        "version": str(row[1]),
    }


def function_registered(connection: Any) -> bool:
    count = connection.execute(
        "SELECT count(*) FROM duckdb_functions() WHERE function_name = ?",
        [FUNCTION_NAME],
    ).fetchone()
    return bool(count and int(count[0]) > 0)


def setting_names(connection: Any) -> set[str]:
    return {
        str(row[0])
        for row in connection.execute("SELECT name FROM duckdb_settings()").fetchall()
    }


def type_names(connection: Any) -> set[tuple[str, str, str]]:
    return {
        (str(database), str(schema), str(name))
        for database, schema, name in connection.execute(
            "SELECT database_name, schema_name, type_name FROM duckdb_types()"
        ).fetchall()
    }


def function_contract(connection: Any) -> dict[str, object]:
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


def public_behavior(
    connection: Any,
    before_settings: set[str],
    before_types: set[tuple[str, str, str]],
    duckdb_identity: list[str],
    extension: dict[str, object],
) -> dict[str, object]:
    # No connector package is loaded in this stock lifecycle, so the query
    # surface is proved through the always-registered introspection function
    # rather than a package-generated relation, which needs an explicit
    # absolute package_root this smoke test does not have.
    query = connection.execute(
        """
        SELECT connector, relation, sql_name, package_version
        FROM system.main.duckdb_api_loaded_relations()
        ORDER BY connector, relation
        """
    )
    rows = [list(row) for row in query.fetchall()]
    schema = [[column[0], str(column[1])] for column in query.description]
    return {
        "added_settings": sorted(setting_names(connection) - before_settings),
        "added_types": [
            list(value) for value in sorted(type_names(connection) - before_types)
        ],
        "duckdb": duckdb_identity,
        "extension": [extension["name"], extension["version"]],
        "function": function_contract(connection),
        "rows": rows,
        "schema": schema,
    }


def allow_unsigned(connection: Any) -> bool:
    row = connection.execute(
        "SELECT value FROM duckdb_settings() WHERE name = 'allow_unsigned_extensions'"
    ).fetchone()
    if row is None:
        raise AssertionError("DuckDB did not expose its unsigned-extension policy")
    return str(row[0]).lower() == "true"


def diagnostic_category(diagnostic: str) -> str:
    lowered = diagnostic.lower()
    if "platform" in lowered:
        return "platform"
    if "version" in lowered or "built specifically" in lowered:
        return "version"
    return "installation"


def execute_action(
    action: str,
    database: pathlib.Path,
    extension_directory: pathlib.Path,
    incompatible_artifact: pathlib.Path | None,
) -> dict[str, object]:
    """Execute one stock action and close the connection before returning."""

    import duckdb  # The explicit provider launcher owns this stock package.

    connection = duckdb.connect(
        database=str(database), config=connection_config(extension_directory)
    )
    try:
        version = connection.execute("PRAGMA version").fetchone()
        platform = connection.execute("PRAGMA platform").fetchone()
        if version is None or platform is None:
            raise AssertionError("DuckDB did not expose its complete host identity")
        duckdb_identity = [str(version[0]), str(version[1])]
        observation: dict[str, object] = {
            "action": action,
            "allow_unsigned_extensions": allow_unsigned(connection),
            "behavior": None,
            "diagnostic": None,
            "diagnostic_category": None,
            "duckdb": duckdb_identity,
            "extension": None,
            "function_registered": False,
            "ok": True,
            "platform": str(platform[0]),
            "process_token": str(os.getpid()),
            "schema": HOST_OBSERVATION_SCHEMA,
        }
        before_settings = setting_names(connection)
        before_types = type_names(connection)
        try:
            if action == "pre_install":
                pass
            elif action in {"install", "repeat_install"}:
                connection.execute("INSTALL duckdb_api FROM community")
            elif action == "load_query":
                connection.execute("LOAD duckdb_api")
            elif action == "incompatible":
                if incompatible_artifact is None:
                    raise AssertionError("incompatible action lacks its artifact")
                connection.execute(
                    f"INSTALL {sql_literal(incompatible_artifact.as_posix())}"
                )
            else:
                raise AssertionError("unknown stock host action")
        except Exception as error:
            observation.update(
                {
                    "diagnostic": str(error),
                    "diagnostic_category": diagnostic_category(str(error)),
                    "extension": extension_record(connection),
                    "function_registered": function_registered(connection),
                    "ok": False,
                }
            )
            return observation

        extension = extension_record(connection)
        observation["extension"] = extension
        observation["function_registered"] = function_registered(connection)
        if action == "load_query":
            if extension is None:
                raise AssertionError("loaded extension is absent from DuckDB inventory")
            observation["behavior"] = public_behavior(
                connection,
                before_settings,
                before_types,
                duckdb_identity,
                extension,
            )
        return observation
    finally:
        connection.close()


def parse_args(argv: Sequence[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Run one stock Community action")
    parser.add_argument(
        "action",
        choices=(
            "pre_install",
            "install",
            "repeat_install",
            "load_query",
            "incompatible",
        ),
    )
    parser.add_argument("--database", required=True)
    parser.add_argument("--extension-directory", required=True)
    parser.add_argument("--state-directory-fd", required=True, type=int)
    parser.add_argument("--incompatible-artifact")
    return parser.parse_args(argv)


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(argv)
    try:
        os.fchdir(args.state_directory_fd)
    except OSError as error:
        raise ActionError("admitted stock state directory is unavailable") from error
    database = pathlib.Path(args.database)
    extension_directory = pathlib.Path(args.extension_directory)
    if (
        database.is_absolute()
        or extension_directory.is_absolute()
        or len(database.parts) != 1
        or len(extension_directory.parts) != 1
    ):
        raise ActionError("stock state paths must be relative to the admitted directory")
    incompatible = (
        inherited_artifact_path(args.incompatible_artifact)
        if args.incompatible_artifact is not None
        else None
    )
    if args.action == "incompatible" and incompatible is None:
        raise SystemExit("--incompatible-artifact is required for incompatible")
    if args.action != "incompatible" and incompatible is not None:
        raise SystemExit("--incompatible-artifact is valid only for incompatible")
    extension_directory.mkdir(mode=0o700, parents=True, exist_ok=True)
    database.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
    observation = execute_action(
        args.action, database, extension_directory, incompatible
    )
    print(json.dumps(observation, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

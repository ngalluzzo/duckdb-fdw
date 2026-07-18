#!/usr/bin/env python3

from __future__ import annotations

import json
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile


EXPECTED_STATIC_SUMMARY = {
    "duckdb": ["v1.5.4", "08e34c447b"],
    "extension": {
        "install_mode": "NOT_INSTALLED",
        "installed": False,
        "loaded": True,
        "name": "duckdb_api",
        "version": "0.4.0",
    },
    "relation": {
        "connector": "github",
        "maximum_rows": 3,
        "name": "duckdb_login_search_page",
    },
    "schema": [
        ["id", "BIGINT"],
        ["login", "VARCHAR"],
        ["site_admin", "BOOLEAN"],
    ],
}
EXPECTED_FAILURE = "Binder Error: [duckdb_api][bind] unknown connector identifier"
FAILURE_PROGRAM = r'''
import duckdb
import sys

connection = duckdb.connect(config={"allow_unsigned_extensions": "true"})
connection.execute("LOAD 'duckdb_api.duckdb_extension'")
try:
    connection.execute(
        "SELECT * FROM duckdb_api_scan(connector := 'example', relation := 'items')"
    ).fetchall()
except Exception as error:
    print(str(error), file=sys.stderr)
    raise SystemExit(1)
finally:
    connection.close()
'''


def run(
    command: list[str], directory: pathlib.Path, environment: dict[str, str]
) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        command,
        cwd=directory,
        env=environment,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        check=False,
    )


def validate_summary(summary: object) -> None:
    if not isinstance(summary, dict):
        raise AssertionError(f"example did not return a JSON object: {summary!r}")
    rows = summary.get("rows")
    static = {key: value for key, value in summary.items() if key != "rows"}
    if static != EXPECTED_STATIC_SUMMARY:
        raise AssertionError(f"first-live-relation example drifted: {summary!r}")
    if not isinstance(rows, list) or len(rows) > 3:
        raise AssertionError(f"public row cardinality drifted: {rows!r}")
    for row in rows:
        if (
            not isinstance(row, list)
            or len(row) != 3
            or isinstance(row[0], bool)
            or not isinstance(row[0], int)
            or not isinstance(row[1], str)
            or not isinstance(row[2], bool)
        ):
            raise AssertionError(f"public row type drifted: {row!r}")


def main() -> int:
    if len(sys.argv) != 3:
        raise SystemExit("usage: source_demo_contract.py PINNED_PYTHON PATH_TO_EXTENSION")

    pinned_python = pathlib.Path(sys.argv[1]).expanduser().absolute()
    source_artifact = pathlib.Path(sys.argv[2]).resolve(strict=True)
    if not pinned_python.is_file() or not os.access(pinned_python, os.X_OK):
        raise AssertionError(f"pinned Python is not executable: {pinned_python}")

    repository_root = pathlib.Path(__file__).resolve().parents[2]
    example_runner = repository_root / "examples/first_live_rest_relation.py"
    authenticated_runner = repository_root / "examples/authenticated_user.py"
    authenticated_sql = repository_root / "examples/authenticated-user.sql"

    with tempfile.TemporaryDirectory(prefix="duckdb-api-source-demo-") as directory:
        isolated = pathlib.Path(directory)
        artifact = isolated / "duckdb_api.duckdb_extension"
        shutil.copyfile(source_artifact, artifact)
        (isolated / "connector.yaml").write_text("top-secret-source-demo", encoding="utf-8")
        (isolated / "items.json").write_text("top-secret-source-demo", encoding="utf-8")

        home = isolated / "home"
        temporary = isolated / "tmp"
        cache = isolated / "cache"
        config = isolated / "config"
        poisoned_python_path = isolated / "python-path-poison"
        for path in (home, temporary, cache, config, poisoned_python_path):
            path.mkdir()
        (poisoned_python_path / "duckdb.py").write_text(
            "raise RuntimeError('ambient PYTHONPATH was imported')\n",
            encoding="utf-8",
        )
        (home / ".curlrc").write_text(
            "url = http://127.0.0.1:1/top-secret-source-demo\n", encoding="utf-8"
        )

        environment = os.environ.copy()
        environment["ALL_PROXY"] = "http://127.0.0.1:1"
        environment["DUCKDB_API_CONNECTOR_PATH"] = "/top-secret-source-demo/connector.yaml"
        environment["DUCKDB_API_FIXTURE_SCENARIO"] = "malformed-top-secret-source-demo"
        environment["DUCKDB_API_LIVE_PROOF_AUTHORITY"] = "http://127.0.0.1:1"
        environment["HOME"] = str(home)
        environment["HTTPS_PROXY"] = "http://127.0.0.1:1"
        environment["HTTP_PROXY"] = "http://127.0.0.1:1"
        environment["NO_PROXY"] = ""
        environment["TMPDIR"] = str(temporary)
        environment["XDG_CACHE_HOME"] = str(cache)
        environment["XDG_CONFIG_HOME"] = str(config)
        environment["PYTHONHOME"] = str(isolated / "missing-python-home")
        environment["PYTHONPATH"] = str(poisoned_python_path)

        success = run(
            [str(pinned_python), "-I", str(example_runner), str(artifact), "--json"],
            isolated,
            environment,
        )
        if success.returncode != 0:
            raise AssertionError(
                "first-live-relation example failed:\n"
                f"stdout:\n{success.stdout}\n"
                f"stderr:\n{success.stderr}"
            )
        summary = json.loads(success.stdout)
        validate_summary(summary)

        authenticated_help = run(
            [str(pinned_python), "-I", str(authenticated_runner), "--help"],
            isolated,
            environment,
        )
        if authenticated_help.returncode != 0:
            raise AssertionError(
                "authenticated example help failed:\n"
                f"stdout:\n{authenticated_help.stdout}\n"
                f"stderr:\n{authenticated_help.stderr}"
            )
        if "--token" in authenticated_help.stdout or "GITHUB_TOKEN" in authenticated_help.stdout:
            raise AssertionError("authenticated example exposed a CLI or environment token path")
        authenticated_source = authenticated_runner.read_text(encoding="utf-8")
        if "getpass.getpass" not in authenticated_source or "os.environ" in authenticated_source:
            raise AssertionError("authenticated example does not use only the interactive token path")
        accepted_sql = authenticated_sql.read_text(encoding="utf-8")
        for fragment in (
            "relation := 'authenticated_user'",
            "secret := 'github_default'",
            "SELECT id, login, site_admin",
        ):
            if fragment not in accepted_sql:
                raise AssertionError("authenticated example SQL drifted")

        failure = run(
            [str(pinned_python), "-I", "-c", FAILURE_PROGRAM],
            isolated,
            environment,
        )
        if failure.returncode == 0:
            raise AssertionError("removed fixture relation unexpectedly succeeded")
        diagnostic = failure.stderr.strip()
        if not diagnostic or diagnostic.splitlines()[0] != EXPECTED_FAILURE:
            raise AssertionError(f"unexpected removed-relation diagnostic: {diagnostic!r}")
        forbidden = (
            "top-secret-source-demo",
            str(isolated),
            "connector.yaml",
            "items.json",
        )
        if any(value in diagnostic for value in forbidden):
            raise AssertionError(
                "removed-relation diagnostic disclosed private context: "
                f"{diagnostic!r}"
            )

    print(json.dumps(summary, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

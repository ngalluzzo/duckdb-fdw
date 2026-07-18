#!/usr/bin/env python3

from __future__ import annotations

import json
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile


EXPECTED_SUMMARY = {
    "duckdb": ["v1.5.4", "08e34c447b"],
    "extension": {
        "install_mode": "NOT_INSTALLED",
        "installed": False,
        "loaded": True,
        "name": "duckdb_api",
        "version": "0.2.0",
    },
    "rows": [[1, "alpha", True], [2, "beta", False], [3, "gamma", True]],
    "schema": [["id", "BIGINT"], ["name", "VARCHAR"], ["active", "BOOLEAN"]],
}
EXPECTED_FAILURE = (
    "Binder Error: [duckdb_api][bind] connector=example: unknown relation identifier"
)
FAILURE_PROGRAM = r'''
import duckdb
import sys

connection = duckdb.connect(config={"allow_unsigned_extensions": "true"})
connection.execute("LOAD 'duckdb_api.duckdb_extension'")
try:
    connection.execute(
        "SELECT * FROM duckdb_api_scan(connector := 'example', relation := 'missing')"
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


def main() -> int:
    if len(sys.argv) != 3:
        raise SystemExit("usage: source_demo_contract.py PINNED_PYTHON PATH_TO_EXTENSION")

    pinned_python = pathlib.Path(sys.argv[1]).expanduser().absolute()
    source_artifact = pathlib.Path(sys.argv[2]).resolve(strict=True)
    if not pinned_python.is_file() or not os.access(pinned_python, os.X_OK):
        raise AssertionError(f"pinned Python is not executable: {pinned_python}")

    repository_root = pathlib.Path(__file__).resolve().parents[2]
    example_runner = repository_root / "examples/first_trustworthy_query.py"

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

        environment = os.environ.copy()
        environment["HOME"] = str(home)
        environment["TMPDIR"] = str(temporary)
        environment["XDG_CACHE_HOME"] = str(cache)
        environment["XDG_CONFIG_HOME"] = str(config)
        environment["PYTHONHOME"] = str(isolated / "missing-python-home")
        environment["PYTHONPATH"] = str(poisoned_python_path)
        environment["DUCKDB_API_CONNECTOR_PATH"] = "/top-secret-source-demo/connector.yaml"
        environment["DUCKDB_API_FIXTURE_SCENARIO"] = "malformed-top-secret-source-demo"

        success = run(
            [str(pinned_python), "-I", str(example_runner), str(artifact), "--json"],
            isolated,
            environment,
        )
        if success.returncode != 0:
            raise AssertionError(
                "first-query example failed:\n"
                f"stdout:\n{success.stdout}\n"
                f"stderr:\n{success.stderr}"
            )
        summary = json.loads(success.stdout)
        if summary != EXPECTED_SUMMARY:
            raise AssertionError(f"first-query example drifted: {summary!r}")

        failure = run(
            [str(pinned_python), "-I", "-c", FAILURE_PROGRAM],
            isolated,
            environment,
        )
        if failure.returncode == 0:
            raise AssertionError("unknown relation unexpectedly succeeded")
        diagnostic = failure.stderr.strip()
        if not diagnostic or diagnostic.splitlines()[0] != EXPECTED_FAILURE:
            raise AssertionError(f"unexpected unknown-relation diagnostic: {diagnostic!r}")
        forbidden = (
            "top-secret-source-demo",
            str(isolated),
            "connector.yaml",
            "items.json",
        )
        if any(value in diagnostic for value in forbidden):
            raise AssertionError(
                "unknown-relation diagnostic disclosed private context: "
                f"{diagnostic!r}"
            )

    print(json.dumps(EXPECTED_SUMMARY, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

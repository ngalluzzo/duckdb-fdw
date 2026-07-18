from __future__ import annotations

import json
import pathlib
import subprocess
import sys
import tempfile
import types
import unittest
from unittest import mock

try:
    from .duckdb_action import execute_action
    from .duckdb_action_test_support import FakeConnection
    from .test_support import GIT_C, public_contract
except ImportError:
    from duckdb_action import execute_action
    from duckdb_action_test_support import FakeConnection
    from test_support import GIT_C, public_contract


class DuckDbQueryActionTests(unittest.TestCase):
    def test_load_query_cannot_invent_a_missing_installation(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            connection = FakeConnection(root / "absent.duckdb_extension")
            module = types.SimpleNamespace(connect=lambda **_: connection)
            with mock.patch.dict(sys.modules, {"duckdb": module}):
                observation = execute_action(
                    "load_query",
                    root / "query.duckdb",
                    root / "extensions",
                    None,
                )
            self.assertFalse(observation["ok"])
            self.assertIn("not installed", observation["diagnostic"])
            self.assertIsNone(observation["extension"])
            self.assertFalse(observation["function_registered"])
            self.assertTrue(connection.closed)

    def test_load_query_observes_exact_catalog_and_public_contract(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            artifact = root / "duckdb_api.duckdb_extension"
            artifact.write_bytes(b"signed artifact")
            connection = FakeConnection(artifact)
            connection.installed = True
            module = types.SimpleNamespace(connect=lambda **_: connection)
            with mock.patch.dict(sys.modules, {"duckdb": module}):
                observation = execute_action(
                    "load_query",
                    root / "query.duckdb",
                    root / "extensions",
                    None,
                )
            self.assertTrue(observation["ok"])
            self.assertTrue(observation["function_registered"])
            self.assertTrue(observation["extension"]["loaded"])
            self.assertEqual(observation["behavior"], public_contract())
            self.assertIn("LOAD duckdb_api", connection.commands)
            self.assertTrue(
                any("FROM duckdb_functions()" in command for command in connection.commands)
            )
            self.assertTrue(
                any("FROM duckdb_types()" in command for command in connection.commands)
            )
            self.assertTrue(connection.closed)

    def test_pinned_stock_154_catalog_probe_when_developer_cell_exists(self) -> None:
        repository = pathlib.Path(__file__).resolve().parents[3]
        python = repository / ".build/dev/python-1.5.4/bin/python3"
        if not python.is_file():
            self.skipTest("pinned DuckDB 1.5.4 developer cell is absent")
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            extensions = root / "extensions"
            extensions.mkdir()
            program = f"""
import duckdb, json
connection = duckdb.connect(database=':memory:', config={{
    'autoinstall_known_extensions': 'false',
    'autoload_known_extensions': 'false',
    'extension_directory': {str(extensions)!r},
}})
result = {{
    'version': list(connection.execute('PRAGMA version').fetchone()[:2]),
    'platform': connection.execute('PRAGMA platform').fetchone()[0],
    'unsigned': connection.execute(
        "SELECT value FROM duckdb_settings() "
        "WHERE name='allow_unsigned_extensions'"
    ).fetchone()[0],
    'extensions': [
        column[0]
        for column in connection.execute(
            'SELECT * FROM duckdb_extensions() LIMIT 0'
        ).description
    ],
    'functions': [
        column[0]
        for column in connection.execute(
            'SELECT * FROM duckdb_functions() LIMIT 0'
        ).description
    ],
}}
print(json.dumps(result, sort_keys=True))
connection.close()
"""
            completed = subprocess.run(
                [str(python), "-I", "-B", "-c", program],
                cwd=root,
                env={
                    "HOME": str(root),
                    "LANG": "C",
                    "LC_ALL": "C",
                    "PATH": "/usr/bin:/bin",
                    "TMPDIR": str(root),
                },
                stdin=subprocess.DEVNULL,
                capture_output=True,
                text=True,
                timeout=5,
                check=True,
            )
            observed = json.loads(completed.stdout)
            self.assertEqual(observed["version"], ["v1.5.4", GIT_C[:10]])
            self.assertRegex(
                observed["platform"],
                r"^(?:osx|linux|windows)_[a-z0-9]+(?:_[a-z0-9]+)*$",
            )
            self.assertEqual(observed["unsigned"], "false")
            self.assertEqual(
                observed["extensions"],
                [
                    "extension_name",
                    "loaded",
                    "installed",
                    "install_path",
                    "description",
                    "aliases",
                    "extension_version",
                    "install_mode",
                    "installed_from",
                ],
            )
            self.assertTrue(
                {"function_name", "parameters", "parameter_types"}
                <= set(observed["functions"])
            )


if __name__ == "__main__":
    unittest.main()

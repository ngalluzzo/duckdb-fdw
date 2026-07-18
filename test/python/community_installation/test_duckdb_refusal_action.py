from __future__ import annotations

import pathlib
import sys
import tempfile
import types
import unittest
from unittest import mock

try:
    from .duckdb_action import execute_action
    from .duckdb_action_test_support import FakeConnection
except ImportError:
    from duckdb_action import execute_action
    from duckdb_action_test_support import FakeConnection


class DuckDbRefusalActionTests(unittest.TestCase):
    def test_incompatible_artifact_returns_categorized_empty_refusal(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            artifact = root / "incompatible.duckdb_extension"
            artifact.write_bytes(b"signed incompatible artifact")
            connection = FakeConnection(artifact, incompatible=True)
            module = types.SimpleNamespace(connect=lambda **_: connection)
            with mock.patch.dict(sys.modules, {"duckdb": module}):
                observation = execute_action(
                    "incompatible",
                    root / "query.duckdb",
                    root / "extensions",
                    artifact,
                )
            self.assertFalse(observation["ok"])
            self.assertEqual(observation["diagnostic_category"], "version")
            self.assertIsNone(observation["extension"])
            self.assertFalse(observation["function_registered"])
            self.assertTrue(
                any(command.startswith("INSTALL '") for command in connection.commands)
            )
            self.assertNotIn("LOAD duckdb_api", connection.commands)
            self.assertTrue(connection.closed)


if __name__ == "__main__":
    unittest.main()

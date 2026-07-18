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


class DuckDbInstallActionTests(unittest.TestCase):
    def test_install_uses_community_and_closes_the_connection(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            artifact = root / "duckdb_api.duckdb_extension"
            artifact.write_bytes(b"signed artifact")
            connection = FakeConnection(artifact)
            captured_config = None

            def connect(*, database: str, config: dict[str, str]):
                nonlocal captured_config
                captured_config = config
                return connection

            module = types.SimpleNamespace(connect=connect)
            with mock.patch.dict(sys.modules, {"duckdb": module}):
                observation = execute_action(
                    "install", root / "query.duckdb", root / "extensions", None
                )
            self.assertTrue(observation["ok"])
            self.assertIn("INSTALL duckdb_api FROM community", connection.commands)
            self.assertFalse(observation["allow_unsigned_extensions"])
            self.assertEqual(observation["extension"]["install_source"], "community")
            self.assertNotIn("allow_unsigned_extensions", captured_config)
            self.assertTrue(connection.closed)


if __name__ == "__main__":
    unittest.main()

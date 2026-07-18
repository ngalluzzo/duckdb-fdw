from __future__ import annotations

import pathlib
import tempfile
import unittest
from unittest import mock

try:
    from .duckdb_action import (
        ActionError,
        connection_config,
        inherited_artifact_path,
        main,
        sql_literal,
    )
except ImportError:
    from duckdb_action import (
        ActionError,
        connection_config,
        inherited_artifact_path,
        main,
        sql_literal,
    )


class DuckDbActionBoundaryTests(unittest.TestCase):
    def test_main_rejects_unavailable_fd_and_nonlocal_state_paths(self) -> None:
        arguments = [
            "pre_install",
            "--database",
            "query.duckdb",
            "--extension-directory",
            "extensions",
            "--state-directory-fd",
            "9",
        ]
        with mock.patch(
            f"{main.__module__}.os.fchdir", side_effect=OSError("closed")
        ):
            with self.assertRaisesRegex(ActionError, "directory is unavailable"):
                main(arguments)
        for original in ("query.duckdb", "extensions"):
            for invalid in ("nested/state", "/outside/state"):
                with self.subTest(original=original, invalid=invalid):
                    changed = list(arguments)
                    changed[changed.index(original)] = invalid
                    with mock.patch(f"{main.__module__}.os.fchdir"):
                        with self.assertRaisesRegex(ActionError, "must be relative"):
                            main(changed)

    def test_connection_policy_has_no_unsigned_override(self) -> None:
        config = connection_config(pathlib.Path("/isolated/extensions"))
        self.assertEqual(
            config,
            {
                "autoinstall_known_extensions": "false",
                "autoload_known_extensions": "false",
                "extension_directory": "/isolated/extensions",
            },
        )
        self.assertNotIn("allow_unsigned_extensions", config)
        self.assertEqual(sql_literal("/tmp/it's"), "'/tmp/it''s'")

    def test_incompatible_artifact_requires_a_live_regular_descriptor(self) -> None:
        for value in (
            "artifact.duckdb_extension",
            "/tmp/artifact",
            "/dev/fd/not-a-fd",
        ):
            with self.subTest(value=value):
                with self.assertRaisesRegex(ActionError, "inherited descriptor"):
                    inherited_artifact_path(value)
        with self.assertRaisesRegex(ActionError, "unavailable"):
            inherited_artifact_path("/dev/fd/999999")

        with tempfile.TemporaryFile() as artifact:
            value = f"/dev/fd/{artifact.fileno()}"
            self.assertEqual(inherited_artifact_path(value), pathlib.Path(value))


if __name__ == "__main__":
    unittest.main()

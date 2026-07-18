from __future__ import annotations

import os
import pathlib
import tempfile
import unittest
from unittest import mock

try:
    from .host_environment import HostEnvironmentError, isolated_environment, private_root
except ImportError:
    from host_environment import HostEnvironmentError, isolated_environment, private_root


class HostEnvironmentTests(unittest.TestCase):
    def test_requires_new_real_caller_owned_root(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            occupied = root / "occupied"
            occupied.mkdir()
            (occupied / "state").write_text("existing", encoding="utf-8")
            with self.assertRaisesRegex(HostEnvironmentError, "new and empty"):
                private_root(occupied, "state root")
            real = root / "real"
            real.mkdir()
            link = root / "link"
            link.symlink_to(real, target_is_directory=True)
            with self.assertRaisesRegex(HostEnvironmentError, "real directory"):
                private_root(link, "state root")

    def test_minimal_environment_does_not_inherit_parent_secrets(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            with mock.patch.dict(
                os.environ,
                {
                    "DUCKDB_API_SECRET": "must-not-cross",
                    "PYTHONPATH": "/unsafe",
                    "DUCKDB_ALLOW_UNSIGNED_EXTENSIONS": "true",
                },
            ):
                environment = isolated_environment(
                    root, venv_launcher=pathlib.Path("/admitted/venv/python3")
                )
            self.assertEqual(
                set(environment),
                {
                    "HOME",
                    "LANG",
                    "LC_ALL",
                    "PATH",
                    "TMPDIR",
                    "XDG_CACHE_HOME",
                    "XDG_CONFIG_HOME",
                    "__PYVENV_LAUNCHER__",
                },
            )
            self.assertEqual(environment["LC_ALL"], "C")
            self.assertNotIn("DUCKDB_API_SECRET", environment)
            self.assertEqual(
                environment["__PYVENV_LAUNCHER__"],
                "/admitted/venv/python3",
            )


if __name__ == "__main__":
    unittest.main()

from __future__ import annotations

import hashlib
import pathlib
import sys
import tempfile
import unittest

try:
    from .launcher import Launcher, LauncherError, stock_host_inventory_sha256
except ImportError:
    from launcher import Launcher, LauncherError, stock_host_inventory_sha256


class LauncherPolicyTests(unittest.TestCase):
    def executable_sha256(self) -> str:
        return hashlib.sha256(
            pathlib.Path(sys.executable).resolve().read_bytes()
        ).hexdigest()

    def inventory_sha256(self, executable: object = sys.executable) -> str:
        return stock_host_inventory_sha256(executable)

    def test_preserves_explicit_virtual_environment_launcher(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            link = pathlib.Path(directory) / "python3"
            link.symlink_to(sys.executable)
            launcher = Launcher.admit(
                link,
                ("-I",),
                executable_sha256=self.executable_sha256(),
                stock_host_inventory_sha256=self.inventory_sha256(link),
            )
            self.assertEqual(launcher.executable, link.absolute())
            self.assertNotEqual(launcher.executable, link.resolve())
            staged = launcher.stage(pathlib.Path(directory))
            command = staged.command(
                pathlib.Path("/query/action.py"), ("install",)
            )
            self.assertEqual(
                command.arguments,
                (str(link.absolute()), "-I", "/query/action.py", "install"),
            )
            self.assertNotEqual(command.executable, launcher.executable)
            (pathlib.Path(directory) / ".launcher-capability").chmod(0o700)

    def test_rejects_unsigned_policy_or_nonexecutable_launcher(self) -> None:
        with self.assertRaisesRegex(LauncherError, "signature policy"):
            Launcher.admit(
                sys.executable,
                ("--allow-unsigned-extensions",),
                executable_sha256=self.executable_sha256(),
                stock_host_inventory_sha256=self.inventory_sha256(),
            )
        with self.assertRaisesRegex(LauncherError, "not executable"):
            Launcher.admit(
                "/definitely/absent/duckdb",
                executable_sha256=self.executable_sha256(),
                stock_host_inventory_sha256=self.inventory_sha256(),
            )
        with self.assertRaisesRegex(LauncherError, "identity"):
            Launcher.admit(
                sys.executable,
                executable_sha256="not-a-digest",
                stock_host_inventory_sha256=self.inventory_sha256(),
            )
        with self.assertRaisesRegex(LauncherError, "bytes changed"):
            Launcher.admit(
                sys.executable,
                executable_sha256="0" * 64,
                stock_host_inventory_sha256=self.inventory_sha256(),
            )

    def test_staged_executable_is_independent_of_source_mutation(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            executable = pathlib.Path(directory) / "launcher"
            executable.write_text("#!/bin/sh\nexit 0\n", encoding="utf-8")
            executable.chmod(0o700)
            digest = hashlib.sha256(executable.read_bytes()).hexdigest()
            launcher = Launcher.admit(
                executable,
                executable_sha256=digest,
                stock_host_inventory_sha256=self.inventory_sha256(executable),
            )
            staged = launcher.stage(pathlib.Path(directory))
            executable.write_text("#!/bin/sh\nexit 1\n", encoding="utf-8")
            self.assertEqual(
                hashlib.sha256(staged.executable.read_bytes()).hexdigest(), digest
            )
            (pathlib.Path(directory) / ".launcher-capability").chmod(0o700)

    def test_rejects_changed_duckdb_module_inventory(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            venv = pathlib.Path(directory) / "venv"
            (venv / "bin").mkdir(parents=True)
            (venv / "bin/python3").symlink_to(sys.executable)
            (venv / "pyvenv.cfg").write_text("home = /admitted\n", encoding="utf-8")
            site = venv / "lib/python3.14/site-packages"
            (site / "duckdb").mkdir(parents=True)
            (site / "duckdb/__init__.py").write_text("VERSION = 1\n", encoding="utf-8")
            (site / "duckdb-1.5.4.dist-info").mkdir()
            (site / "duckdb-1.5.4.dist-info/METADATA").write_text(
                "Name: duckdb\nVersion: 1.5.4\n", encoding="utf-8"
            )
            (site / "_duckdb.fixture.so").write_bytes(b"native")
            python = venv / "bin/python3"
            expected = self.inventory_sha256(python)
            (site / "duckdb/__init__.py").write_text("VERSION = 2\n", encoding="utf-8")
            with self.assertRaisesRegex(LauncherError, "inventory bytes changed"):
                Launcher.admit(
                    python,
                    executable_sha256=self.executable_sha256(),
                    stock_host_inventory_sha256=expected,
                )

if __name__ == "__main__":
    unittest.main()

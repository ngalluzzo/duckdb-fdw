from __future__ import annotations

import os
import hashlib
import pathlib
import sys
import tempfile
import unittest
from unittest import mock

try:
    from .host_action import HostActionError, StockHostRunner
    from .launcher import Launcher, stock_host_inventory_sha256
    from .process_lifecycle import run_bounded_process
    from .test_support import GIT_C, row
except ImportError:
    from host_action import HostActionError, StockHostRunner
    from launcher import Launcher, stock_host_inventory_sha256
    from process_lifecycle import run_bounded_process
    from test_support import GIT_C, row


FAKE_ACTION = f"""
import argparse, json, os
parser = argparse.ArgumentParser()
parser.add_argument('action')
parser.add_argument('--database', required=True)
parser.add_argument('--extension-directory', required=True)
parser.add_argument('--state-directory-fd', required=True, type=int)
parser.add_argument('--incompatible-artifact')
args = parser.parse_args()
os.fchdir(args.state_directory_fd)
__import__('pathlib').Path(args.database).touch(exist_ok=True)
unsafe = (
    'DUCKDB_API_SECRET' in os.environ
    or any('unsigned' in value.lower() for value in __import__('sys').argv)
)
diagnostic = None
category = None
ok = True
if args.action == 'incompatible':
    ok = False
    category = 'version'
    artifact = __import__('pathlib').Path(
        args.incompatible_artifact
    ).read_bytes()
    target = 'v1.5.4' if artifact == b'signed' else 'v9.9.9'
    diagnostic = f'artifact targets {{target}}; current host is v1.5.3 at {{args.database}}'
print(json.dumps({{
    'action': args.action,
    'allow_unsigned_extensions': unsafe,
    'behavior': None,
    'diagnostic': diagnostic,
    'diagnostic_category': category,
    'duckdb': ['v1.5.4', {GIT_C[:10]!r}],
    'extension': None,
    'function_registered': False,
    'ok': ok,
    'platform': 'osx_arm64',
    'process_token': str(os.getpid()),
    'schema': 'duckdb_api/community-host-observation/v1',
}}))
"""


class StockHostRunnerTests(unittest.TestCase):
    def launcher(self) -> Launcher:
        executable = pathlib.Path(sys.executable).resolve()
        digest = hashlib.sha256(executable.read_bytes()).hexdigest()
        return Launcher.admit(
            sys.executable,
            ("-I",),
            executable_sha256=digest,
            stock_host_inventory_sha256=stock_host_inventory_sha256(sys.executable),
        )

    def roots(self, root: pathlib.Path, prefix: str) -> tuple[pathlib.Path, pathlib.Path]:
        state = root / f"{prefix}-state"
        environment = root / f"{prefix}-environment"
        state.mkdir()
        environment.mkdir()
        return state, environment

    def test_runs_one_fresh_process_with_minimal_environment(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            program = root / "fake_action.py"
            program.write_text(FAKE_ACTION, encoding="utf-8")
            state, environment = self.roots(root, "supported")
            with mock.patch.dict(os.environ, {"DUCKDB_API_SECRET": "do-not-inherit"}):
                runner = StockHostRunner(
                    launcher=self.launcher(),
                    row=row(),
                    state_root=state,
                    environment_root=environment,
                    action_program=program,
                )
                observation = runner.run("pre_install", "supported")
            self.assertTrue(observation.ok)
            self.assertFalse(observation.allow_unsigned_extensions)
            self.assertEqual(runner.calls, [("pre_install", "supported")])
            self.assertEqual(
                set(path.name for path in environment.iterdir()),
                {"home", "tmp", "cache", "config", ".launcher-capability"},
            )

    def test_pinned_stock_venv_runs_through_private_launcher_copy(self) -> None:
        repository = pathlib.Path(__file__).resolve().parents[3]
        python = repository / ".build/dev/python-1.5.4/bin/python3"
        if not python.is_file():
            self.skipTest("pinned DuckDB 1.5.4 developer cell is absent")
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            state, environment = self.roots(root, "pinned")
            executable = python.resolve()
            runner = StockHostRunner(
                launcher=Launcher.admit(
                    python,
                    ("-B",),
                    executable_sha256=hashlib.sha256(
                        executable.read_bytes()
                    ).hexdigest(),
                    stock_host_inventory_sha256=stock_host_inventory_sha256(python),
                ),
                row=row(),
                state_root=state,
                environment_root=environment,
            )
            observation = runner.run("pre_install", "pinned")
            self.assertTrue(observation.ok)
            self.assertEqual(observation.row, row())
            self.assertFalse(observation.allow_unsigned_extensions)
            runner.close()

    def test_incompatible_diagnostic_is_path_normalized(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            program = root / "fake_action.py"
            program.write_text(FAKE_ACTION, encoding="utf-8")
            artifact = root / "incompatible.duckdb_extension"
            artifact.write_bytes(b"signed")
            state, environment = self.roots(root, "refusal")
            runner = StockHostRunner(
                launcher=self.launcher(),
                row=row(),
                state_root=state,
                environment_root=environment,
                incompatible_artifact=artifact,
                incompatible_artifact_size=6,
                incompatible_artifact_sha256=hashlib.sha256(b"signed").hexdigest(),
                action_program=program,
            )
            self.assertNotEqual(runner.incompatible_artifact.path, artifact)
            self.assertEqual(
                runner.incompatible_artifact.path.read_bytes(), b"signed"
            )
            artifact.write_bytes(b"changed after admission")
            runner.incompatible_artifact.path.unlink()
            runner.incompatible_artifact.path.write_bytes(b"replacement")
            observation = runner.run("incompatible", "incompatible")
            self.assertFalse(observation.ok)
            self.assertEqual(
                observation.diagnostic,
                "version refusal: v1.5.4, v1.5.3",
            )
            self.assertNotIn(str(state.resolve()), observation.diagnostic)
            runner.close()

    def test_executes_private_launcher_copy_after_source_symlink_swap(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            program = root / "fake_action.py"
            program.write_text(FAKE_ACTION, encoding="utf-8")
            launcher_path = root / "python3"
            launcher_path.symlink_to(sys.executable)
            executable = pathlib.Path(sys.executable).resolve()
            launcher = Launcher.admit(
                launcher_path,
                ("-I",),
                executable_sha256=hashlib.sha256(
                    executable.read_bytes()
                ).hexdigest(),
                stock_host_inventory_sha256=stock_host_inventory_sha256(
                    launcher_path
                ),
            )
            alternate = root / "alternate"
            alternate.write_text("#!/bin/sh\nexit 91\n", encoding="utf-8")
            alternate.chmod(0o700)
            state, environment = self.roots(root, "launcher-swap")
            runner = StockHostRunner(
                launcher=launcher,
                row=row(),
                state_root=state,
                environment_root=environment,
                action_program=program,
            )

            def swap_then_run(command, **kwargs):
                launcher_path.unlink()
                launcher_path.symlink_to(alternate)
                return run_bounded_process(command, **kwargs)

            with mock.patch(
                f"{StockHostRunner.__module__}.run_bounded_process",
                side_effect=swap_then_run,
            ):
                observation = runner.run("pre_install", "supported")
            self.assertTrue(observation.ok)

    def test_rejects_unbounded_or_mismatched_incompatible_artifact(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            program = root / "fake_action.py"
            program.write_text(FAKE_ACTION, encoding="utf-8")
            artifact = root / "incompatible.duckdb_extension"
            artifact.write_bytes(b"signed")
            for prefix, digest, size, limit, message in (
                ("oversized", hashlib.sha256(b"signed").hexdigest(), 6, 5, "authority"),
                ("size", hashlib.sha256(b"signed").hexdigest(), 5, 6, "bounded"),
                ("digest", "0" * 64, 6, 6, "authority"),
            ):
                state, environment = self.roots(root, prefix)
                with self.assertRaisesRegex(HostActionError, message):
                    StockHostRunner(
                        launcher=self.launcher(),
                        row=row(),
                        state_root=state,
                        environment_root=environment,
                        incompatible_artifact=artifact,
                        incompatible_artifact_size=size,
                        incompatible_artifact_sha256=digest,
                        incompatible_artifact_limit_bytes=limit,
                        action_program=program,
                    )

    def test_rejects_missing_incompatible_authority_and_invalid_json(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            program = root / "invalid.py"
            program.write_text("print('not json')\n", encoding="utf-8")
            state, environment = self.roots(root, "invalid")
            runner = StockHostRunner(
                launcher=self.launcher(),
                row=row(),
                state_root=state,
                environment_root=environment,
                action_program=program,
            )
            with self.assertRaisesRegex(HostActionError, "requires an explicit"):
                runner.run("incompatible", "incompatible")
            with self.assertRaisesRegex(HostActionError, "one JSON"):
                runner.run("pre_install", "supported")

if __name__ == "__main__":
    unittest.main()

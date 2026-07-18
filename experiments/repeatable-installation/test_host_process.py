"""Isolation and process-group lifecycle tests for DuckDB host actions."""

from __future__ import annotations

import os
import pathlib
import sys
import tempfile
import time
import unittest
from unittest import mock

from host_process import HostRunner, isolated_environment


class HostProcessTests(unittest.TestCase):
    def test_host_runner_uses_minimal_environment_for_json_exchange(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            host = root / "fake_host.py"
            host.write_text(
                """
import argparse
import json
import os

parser = argparse.ArgumentParser()
parser.add_argument("action")
parser.add_argument("--database", required=True)
parser.add_argument("--extension-directory", required=True)
parser.add_argument("--artifact")
parser.add_argument("--allow-unsigned", action="store_true")
args = parser.parse_args()
print(json.dumps({
    "action": args.action,
    "allow_unsigned": args.allow_unsigned,
    "artifact": args.artifact,
    "database": args.database,
    "secret": os.environ.get("DUCKDB_API_SECRET_CANARY"),
}))
""".lstrip(),
                encoding="utf-8",
            )
            state = root / "state"
            state.mkdir()
            extensions = state / "extensions"
            extensions.mkdir()
            artifact = root / "artifact.duckdb_extension"
            artifact.write_bytes(b"artifact")
            environment_root = root / "environment"
            environment_root.mkdir()
            with mock.patch.dict(
                os.environ,
                {"DUCKDB_API_SECRET_CANARY": "must-not-cross-process-boundary"},
            ):
                environment = isolated_environment(environment_root)
            runner = HostRunner(host, environment)

            observation = runner.run(
                pathlib.Path(sys.executable),
                "install",
                state / "trial.duckdb",
                extensions,
                artifact=artifact,
                allow_unsigned=True,
            )

            self.assertEqual(runner.invocations, 1)
            self.assertEqual(observation["action"], "install")
            self.assertTrue(observation["allow_unsigned"])
            self.assertEqual(observation["artifact"], str(artifact))
            self.assertIsNone(observation["secret"])
            self.assertEqual(
                set(environment),
                {
                    "HOME",
                    "LC_ALL",
                    "PATH",
                    "TMPDIR",
                    "XDG_CACHE_HOME",
                    "XDG_CONFIG_HOME",
                },
            )

    def test_host_runner_times_out_and_reaps_sleeping_child(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            host = root / "sleeping_host.py"
            host.write_text("import time\ntime.sleep(10)\n", encoding="utf-8")
            state = root / "state"
            state.mkdir()
            extensions = state / "extensions"
            extensions.mkdir()
            environment_root = root / "environment"
            environment_root.mkdir()
            runner = HostRunner(
                host,
                isolated_environment(environment_root),
                timeout_seconds=0.05,
            )

            with self.assertRaisesRegex(
                AssertionError,
                "timed out after 0.05 seconds.*terminated and reaped",
            ):
                runner.run(
                    pathlib.Path(sys.executable),
                    "install",
                    state / "trial.duckdb",
                    extensions,
                    allow_unsigned=False,
                )
            self.assertEqual(runner.invocations, 1)

    def test_host_output_limit_kills_the_process_group(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            host = root / "noisy_host.py"
            host.write_text(
                "import os, time\nos.write(1, b'x' * 4096)\ntime.sleep(10)\n",
                encoding="utf-8",
            )
            state = root / "state"
            state.mkdir()
            extensions = state / "extensions"
            extensions.mkdir()
            environment_root = root / "environment"
            environment_root.mkdir()
            runner = HostRunner(
                host,
                isolated_environment(environment_root),
                timeout_seconds=2,
                output_limit_bytes=1024,
            )

            with self.assertRaisesRegex(
                AssertionError,
                "exceeded the 1024-byte output limit.*process group was killed",
            ):
                runner.run(
                    pathlib.Path(sys.executable),
                    "install",
                    state / "trial.duckdb",
                    extensions,
                    allow_unsigned=False,
                )

    def test_host_timeout_kills_descendant_that_closes_inherited_pipes(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            host = root / "forking_host.py"
            host.write_text(
                """
import argparse
import os
import pathlib
import signal
import time

parser = argparse.ArgumentParser()
parser.add_argument("action")
parser.add_argument("--database", required=True)
parser.add_argument("--extension-directory", required=True)
parser.add_argument("--artifact")
parser.add_argument("--allow-unsigned", action="store_true")
args = parser.parse_args()
child = os.fork()
if child == 0:
    signal.signal(signal.SIGTERM, signal.SIG_IGN)
    os.close(1)
    os.close(2)
    time.sleep(10)
    os._exit(0)
pathlib.Path(args.database + ".child-pid").write_text(str(child))
time.sleep(10)
""".lstrip(),
                encoding="utf-8",
            )
            state = root / "state"
            state.mkdir()
            extensions = state / "extensions"
            extensions.mkdir()
            database = state / "trial.duckdb"
            environment_root = root / "environment"
            environment_root.mkdir()
            runner = HostRunner(
                host,
                isolated_environment(environment_root),
                timeout_seconds=0.2,
            )

            with self.assertRaisesRegex(AssertionError, "process group was killed"):
                runner.run(
                    pathlib.Path(sys.executable),
                    "install",
                    database,
                    extensions,
                    allow_unsigned=False,
                )

            descendant_pid = int(
                pathlib.Path(f"{database}.child-pid").read_text(encoding="utf-8")
            )
            deadline = time.monotonic() + 2
            while time.monotonic() < deadline:
                try:
                    os.kill(descendant_pid, 0)
                except ProcessLookupError:
                    break
                time.sleep(0.01)
            else:
                self.fail(f"descendant process {descendant_pid} survived timeout")

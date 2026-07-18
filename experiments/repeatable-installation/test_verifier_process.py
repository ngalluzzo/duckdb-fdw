"""Isolation and lifecycle tests for provider verifier processes."""

from __future__ import annotations

import os
import pathlib
import tempfile
import unittest
from dataclasses import replace
from unittest import mock

from query_oracle_test_support import trial_package
from trial_inputs import run_verifier, verify_trial_inputs


class VerifierProcessTests(unittest.TestCase):
    def test_verifier_ignores_pythonpath_and_parent_secret(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            inputs, _, _ = trial_package(root)
            injection = root / "injection"
            injection.mkdir()
            marker = root / "sitecustomize-ran"
            (injection / "sitecustomize.py").write_text(
                f"from pathlib import Path\nPath({str(marker)!r}).write_text('ran')\n",
                encoding="utf-8",
            )
            inputs.verifier.write_text(
                """
import os
import sys
raise SystemExit(
    0 if len(sys.argv) == 4 and "DUCKDB_API_SECRET_CANARY" not in os.environ else 9
)
""".lstrip(),
                encoding="utf-8",
            )

            with mock.patch.dict(
                os.environ,
                {
                    "DUCKDB_API_SECRET_CANARY": "must-not-cross-process-boundary",
                    "PYTHONPATH": str(injection),
                },
            ):
                verify_trial_inputs(inputs)

            self.assertFalse(marker.exists())

    def test_verifier_times_out_and_reaps_sleeping_process(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            inputs, _, _ = trial_package(pathlib.Path(directory))
            inputs.verifier.write_text(
                "import time\ntime.sleep(10)\n",
                encoding="utf-8",
            )

            with self.assertRaisesRegex(
                AssertionError,
                "provider verifier timed out after 0.05 seconds",
            ):
                run_verifier(inputs, inputs.artifact, timeout_seconds=0.05)

    def test_verifier_output_limit_kills_the_process_group(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            inputs, _, _ = trial_package(pathlib.Path(directory))
            inputs.verifier.write_text(
                "import os, time\nos.write(1, b'x' * 4096)\ntime.sleep(10)\n",
                encoding="utf-8",
            )

            with self.assertRaisesRegex(
                AssertionError,
                "exceeded the 1024-byte output limit.*process group was killed",
            ):
                run_verifier(
                    inputs,
                    inputs.artifact,
                    timeout_seconds=2,
                    output_limit_bytes=1024,
                )

    def test_verifier_diagnostics_normalize_file_and_parent_roots(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            inputs, _, _ = trial_package(root)
            provider_root = root / "provider"
            provider_root.mkdir()
            verifier = provider_root / "verifier.py"
            verifier.write_text(
                """
import pathlib
import sys
print(pathlib.Path(__file__).resolve().parent)
print(pathlib.Path(sys.argv[2]).resolve().parent)
raise SystemExit(1)
""".lstrip(),
                encoding="utf-8",
            )
            inputs = replace(inputs, verifier=verifier)

            completed = run_verifier(inputs, inputs.artifact)

            self.assertEqual(completed.returncode, 1)
            self.assertEqual(
                completed.stdout.splitlines(),
                ["<provider-root>", "<fixture-root>"],
            )
            self.assertNotIn(str(inputs.verifier.parent), completed.stdout)
            self.assertNotIn(str(inputs.artifact.parent), completed.stdout)
            self.assertNotIn(
                str(inputs.verifier.parent.resolve(strict=True)),
                completed.stdout,
            )
            self.assertNotIn(
                str(inputs.artifact.parent.resolve(strict=True)),
                completed.stdout,
            )

"""Focused tests for composed trial input admission."""

from __future__ import annotations

import os
import pathlib
import sys
import tempfile
import unittest

from query_oracle_test_support import digest, trial_package
from trial_inputs import (
    MAX_INPUT_FILE_BYTES,
    executable_file,
    existing_file,
    run_identity_bound_verifier,
    verify_trial_inputs,
)


class TrialInputTests(unittest.TestCase):
    def test_special_and_oversized_inputs_are_rejected_before_snapshot(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            fifo = root / "input.fifo"
            os.mkfifo(fifo)
            with self.assertRaisesRegex(AssertionError, "not a regular file"):
                existing_file(str(fifo))

            oversized = root / "oversized.bin"
            with oversized.open("wb") as output:
                output.truncate(MAX_INPUT_FILE_BYTES + 1)
            with self.assertRaisesRegex(AssertionError, "outside"):
                existing_file(str(oversized))

            if pathlib.Path("/dev/zero").exists():
                with self.assertRaisesRegex(AssertionError, "not a regular file"):
                    existing_file("/dev/zero")

    def test_executable_admission_preserves_virtual_environment_symlink(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            link = pathlib.Path(directory) / "python3"
            link.symlink_to(sys.executable)

            admitted = executable_file(str(link))

            self.assertEqual(admitted, link.absolute())
            self.assertNotEqual(admitted, link.resolve())

    def test_bundle_admission_composes_manifest_and_fixture_contracts(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            inputs, _, public_contract = trial_package(pathlib.Path(directory))

            verified = verify_trial_inputs(inputs)

            self.assertEqual(
                verified.artifact_sha256,
                digest(inputs.artifact.read_bytes()),
            )
            self.assertEqual(verified.public_contract, public_contract)
            self.assertEqual(
                verified.supported_duckdb_identity,
                ("v1.5.4", "08e34c447b"),
            )
            self.assertEqual(
                verified.mismatched_duckdb_identity,
                ("v1.5.3", "14eca11bd9"),
            )
            self.assertEqual(verified.source_platform, "osx_arm64")
            self.assertEqual(verified.wrong_platform, "linux_amd64")

    def test_verifier_cannot_change_identity_between_original_and_corrupt_checks(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            inputs, _, _ = trial_package(pathlib.Path(directory))
            inputs.verifier.write_text(
                """
import pathlib
import sys

if pathlib.Path(sys.argv[2]).parent.name == "corrupted":
    verifier = pathlib.Path(__file__)
    verifier.chmod(0o644)
    verifier.write_text("raise SystemExit(1)\\n")
    print("release artifact does not match the tracked trust record", file=sys.stderr)
    raise SystemExit(1)
raise SystemExit(0)
""".lstrip(),
                encoding="utf-8",
            )
            verified = verify_trial_inputs(inputs)

            with self.assertRaisesRegex(
                AssertionError,
                "provider verifier identity changed during verification",
            ):
                run_identity_bound_verifier(
                    inputs,
                    inputs.corrupted_artifact,
                    expected_sha256=verified.verifier_sha256,
                )

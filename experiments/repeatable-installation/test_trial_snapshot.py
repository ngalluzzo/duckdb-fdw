"""Tests for the private verification-to-use snapshot boundary."""

from __future__ import annotations

import pathlib
import tempfile
import unittest

from query_oracle_test_support import digest, trial_package
from trial_inputs import verify_trial_inputs
from trial_snapshot import create_trial_snapshot


class TrialSnapshotTests(unittest.TestCase):
    def test_source_changes_cannot_change_verified_or_host_bytes(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            source = root / "source"
            source.mkdir()
            inputs, _, _ = trial_package(source)
            query_host = source / "query_host.py"
            query_host.write_text("print('frozen host')\n", encoding="utf-8")

            snapshot = create_trial_snapshot(inputs, query_host, root / "snapshot")
            expected_artifact = snapshot.inputs.artifact.read_bytes()
            expected_manifest = snapshot.inputs.manifest.read_bytes()
            expected_inventory = snapshot.inputs.negative_fixture_inventory.read_bytes()
            expected_host = snapshot.query_host.read_bytes()
            expected_verifier = snapshot.inputs.verifier.read_bytes()

            inputs.artifact.write_bytes(b"replaced after snapshot")
            inputs.manifest.write_bytes(b"{}")
            inputs.negative_fixture_inventory.write_bytes(b"{}")
            inputs.wrong_platform_artifact.unlink()
            inputs.corrupted_artifact.unlink()
            query_host.unlink()
            inputs.verifier.unlink()

            verified = verify_trial_inputs(snapshot.inputs)

            self.assertEqual(snapshot.inputs.artifact.read_bytes(), expected_artifact)
            self.assertEqual(snapshot.inputs.manifest.read_bytes(), expected_manifest)
            self.assertEqual(
                snapshot.inputs.negative_fixture_inventory.read_bytes(),
                expected_inventory,
            )
            self.assertEqual(snapshot.query_host.read_bytes(), expected_host)
            self.assertEqual(snapshot.inputs.verifier.read_bytes(), expected_verifier)
            self.assertEqual(verified.artifact_sha256, digest(expected_artifact))
            self.assertTrue(
                all(
                    (path.stat().st_mode & 0o222) == 0
                    for path in (
                        snapshot.inputs.artifact,
                        snapshot.inputs.manifest,
                        snapshot.inputs.negative_fixture_inventory,
                        snapshot.inputs.wrong_platform_artifact,
                        snapshot.inputs.corrupted_artifact,
                        snapshot.query_host,
                        snapshot.inputs.verifier,
                    )
                )
            )
            with self.assertRaises(PermissionError):
                snapshot.inputs.artifact.unlink()

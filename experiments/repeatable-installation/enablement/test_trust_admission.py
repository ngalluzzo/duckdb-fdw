#!/usr/bin/env python3
"""Trust-record and release-anchor admission tests."""

from __future__ import annotations

import importlib.util
import json
import pathlib
import sys
import tempfile
import unittest


sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from enablement_test_support import EnablementTestCase, write_release_triple


class TrustAdmissionTest(EnablementTestCase):
    def test_self_contained_trial_verifier_matches_tracked_authority(self) -> None:
        verifier_path = pathlib.Path(__file__).with_name("verify_trial_bundle.py")
        spec = importlib.util.spec_from_file_location("trial_verifier", verifier_path)
        self.assertIsNotNone(spec)
        assert spec is not None and spec.loader is not None
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        trust = json.loads(
            pathlib.Path(__file__).with_name("trusted-release.json").read_text(
                encoding="utf-8"
            )
        )
        self.assertEqual(
            (module.MANIFEST_NAME, module.MANIFEST_SHA256, module.MANIFEST_SIZE),
            (
                trust["manifest"]["filename"],
                trust["manifest"]["sha256"],
                trust["manifest"]["size"],
            ),
        )
        self.assertEqual(
            (module.ARTIFACT_NAME, module.ARTIFACT_SHA256, module.ARTIFACT_SIZE),
            (
                trust["artifact"]["filename"],
                trust["artifact"]["sha256"],
                trust["artifact"]["size"],
            ),
        )
        manifest, artifact, anchor = self.real_triple()
        accepted = self.run_script(
            "verify_trial_bundle.py",
            manifest,
            artifact,
            anchor,
        )
        self.assertEqual(accepted.returncode, 0, accepted.stderr)

    def test_fabricated_self_anchored_release_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            manifest, artifact, anchor = write_release_triple(pathlib.Path(directory))
            refused = self.run_script("verify_bundle.py", manifest, artifact, anchor)
            self.assertNotEqual(refused.returncode, 0)
            self.assertIn("does not match the tracked trust record", refused.stderr)

    def test_exact_retained_release_and_anchor_are_accepted(self) -> None:
        manifest, artifact, anchor = self.real_triple()
        accepted = self.run_script("verify_bundle.py", manifest, artifact, anchor)
        self.assertEqual(accepted.returncode, 0, accepted.stderr)

        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            copied_manifest = root / "manifest.json"
            copied_artifact = root / "duckdb_api.duckdb_extension"
            copied_anchor = root / "manifest.sha256"
            copied_manifest.write_bytes(manifest.read_bytes())
            copied_artifact.write_bytes(artifact.read_bytes())
            copied_anchor.write_text(anchor.read_text() + "\n", encoding="utf-8")
            refused = self.run_script(
                "verify_bundle.py", copied_manifest, copied_artifact, copied_anchor
            )
            self.assertNotEqual(refused.returncode, 0)
            self.assertIn("anchor syntax drifted", refused.stderr)


if __name__ == "__main__":
    unittest.main()

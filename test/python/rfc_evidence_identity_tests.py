#!/usr/bin/env python3

from __future__ import annotations

import copy
import hashlib
import importlib.util
import json
import pathlib
import shutil
import tempfile
import unittest


REPOSITORY = pathlib.Path(__file__).resolve().parents[2]
VERIFIER_PATH = REPOSITORY / "scripts/verify-rfc-evidence.py"


def load_verifier():
    spec = importlib.util.spec_from_file_location("rfc_evidence_verifier", VERIFIER_PATH)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot import {VERIFIER_PATH}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


VERIFIER = load_verifier()


class RfcEvidenceIdentityTests(unittest.TestCase):
    def setUp(self) -> None:
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = pathlib.Path(temporary.name)
        for relative in ("docs/rfcs/evidence/0013", "docs/rfcs/evidence/0022"):
            shutil.copytree(REPOSITORY / relative, self.root / relative)
        for relative in (
            "scripts/verify-rfc-0013-evidence.rb",
            "scripts/verify-rfc-evidence.py",
            "scripts/rfc_evidence_authorities.py",
            "src/connector/package/assets/connector-package-v1.schema.json",
            "src/connector/package/assets/fixture-coverage-v1.json",
            "src/connector/package/assets/fixture-index-v1.schema.json",
        ):
            target = self.root / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(REPOSITORY / relative, target)

    def test_accepts_both_externally_anchored_generations(self) -> None:
        result = VERIFIER.verify(self.root)
        self.assertEqual(set(result["authorities"]), {"0013", "0022"})
        self.assertEqual(result["current"], "0022")

    def test_rejects_artifact_byte_change(self) -> None:
        path = self.root / "docs/rfcs/evidence/0022/fixture-index-v1.schema.json"
        path.write_bytes(path.read_bytes() + b"\n")
        with self.assertRaisesRegex(AssertionError, "artifact digest differs"):
            VERIFIER.verify(self.root)

    def test_rejects_added_removed_and_renamed_artifacts(self) -> None:
        evidence = self.root / "docs/rfcs/evidence/0022"
        for label, mutate in (
            ("added", lambda: (evidence / "ambient.json").write_text("{}\n")),
            ("removed", lambda: (evidence / "fixture-coverage-v1.json").unlink()),
            (
                "renamed",
                lambda: (evidence / "fixture-coverage-v1.json").rename(
                    evidence / "renamed-coverage.json"
                ),
            ),
        ):
            with self.subTest(label=label):
                fresh = pathlib.Path(tempfile.mkdtemp(dir=self.root))
                shutil.copytree(evidence, fresh / "evidence")
                candidate = fresh / "evidence"
                if label == "added":
                    (candidate / "ambient.json").write_text("{}\n")
                elif label == "removed":
                    (candidate / "fixture-coverage-v1.json").unlink()
                else:
                    (candidate / "fixture-coverage-v1.json").rename(
                        candidate / "renamed-coverage.json"
                    )
                authority = copy.deepcopy(VERIFIER.AUTHORITIES["0022"])
                authority["directory"] = candidate.relative_to(self.root).as_posix()
                with self.assertRaisesRegex(
                    AssertionError, "inventory differs|cannot be read"
                ):
                    VERIFIER.verify_authority(self.root, "0022", authority)

    def test_rejects_artifact_and_manifest_repin(self) -> None:
        evidence = self.root / "docs/rfcs/evidence/0022"
        artifact = evidence / "fixture-coverage-v1.json"
        artifact.write_bytes(artifact.read_bytes() + b"\n")
        manifest_path = evidence / "evidence-manifest.json"
        manifest = json.loads(manifest_path.read_text())
        manifest["artifacts"]["fixture-coverage-v1.json"] = (
            "sha256." + hashlib.sha256(artifact.read_bytes()).hexdigest()
        )
        manifest_path.write_text(json.dumps(manifest, indent=2) + "\n")
        with self.assertRaisesRegex(AssertionError, "manifest digest differs"):
            VERIFIER.verify(self.root)

    def test_rejects_historical_0013_drift_and_verifier_drift(self) -> None:
        artifact = self.root / "docs/rfcs/evidence/0013/graphql-query-golden.yaml"
        original = artifact.read_bytes()
        artifact.write_bytes(original + b"\n")
        with self.assertRaisesRegex(AssertionError, "artifact digest differs"):
            VERIFIER.verify(self.root)
        artifact.write_bytes(original)

        legacy_verifier = self.root / "scripts/verify-rfc-0013-evidence.rb"
        legacy_verifier.write_bytes(legacy_verifier.read_bytes() + b"\n")
        with self.assertRaisesRegex(AssertionError, "verifier digest differs"):
            VERIFIER.verify(self.root)

    def test_rejects_production_current_authority_mismatch(self) -> None:
        production = self.root / "product/connector-package-v1.schema.json"
        production.parent.mkdir(parents=True)
        evidence = self.root / "docs/rfcs/evidence/0022/connector-package-v1.schema.json"
        shutil.copyfile(evidence, production)
        authority = copy.deepcopy(VERIFIER.AUTHORITIES["0022"])
        authority["production_mirrors"] = {
            "connector-package-v1.schema.json": "product/connector-package-v1.schema.json"
        }
        VERIFIER.verify_authority(self.root, "0022", authority)
        production.write_bytes(production.read_bytes() + b"\n")
        with self.assertRaisesRegex(AssertionError, "production/current-authority mirror differs"):
            VERIFIER.verify_authority(self.root, "0022", authority)


if __name__ == "__main__":
    unittest.main()

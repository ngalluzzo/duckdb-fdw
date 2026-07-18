#!/usr/bin/env python3
"""Community description.yml proposal semantics tests."""

from __future__ import annotations

import json
import pathlib
import sys


sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from descriptor_test_support import (  # noqa: E402
    PUBLISHED_COMMIT,
    DescriptorFixture,
)


class DescriptorProposalTest(DescriptorFixture):
    def test_exact_proposal_emits_local_only_anchored_admission(self) -> None:
        output = self.root / "descriptor-admission"
        result = self.run_descriptor(output)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "descriptor-admission.json\n")
        record = json.loads(
            (output / "descriptor-admission.json").read_text(encoding="utf-8")
        )
        self.assertEqual(record["status"], "proposal_admitted")
        self.assertEqual(record["authority"], "local_provider_admission_only")
        self.assertEqual(record["publication_status"], "not_submitted")
        self.assertEqual(record["support_claims"], [])
        self.assertEqual(record["candidate"]["source"]["commit"], PUBLISHED_COMMIT)
        self.assertEqual(record["proposal"]["filename"], "description.yml")
        for forbidden in (
            "submission",
            "ci",
            "signing",
            "deployment",
            "artifact",
            "platform",
            "support",
        ):
            self.assertNotIn(forbidden, record)
        self.assertRegex(
            (output / "descriptor-admission.sha256").read_text(encoding="ascii"),
            r"\A[0-9a-f]{64}  descriptor-admission\.json\n\Z",
        )

    def test_wrong_or_missing_descriptor_fields_are_rejected(self) -> None:
        mutations = (
            ("name", b"name: duckdb_api", b"name: other"),
            ("description", b"Exposes a typed example relation", b"Claims every API"),
            ("version", b"version: 0.2.0", b"version: 0.2.1"),
            ("language", b"language: C++", b"language: Rust"),
            ("build", b"build: cmake", b"build: make"),
            ("license", b"license: MIT", b"license: Apache-2.0"),
            ("maintainer", b"- ngalluzzo", b"- unapproved"),
            ("repository", b"ngalluzzo/duckdb-fdw", b"someone/fork"),
            ("mutable-ref", PUBLISHED_COMMIT.encode(), b"main"),
            ("missing-license", b"  license: MIT\n", b""),
            (
                "extra-exclusion",
                b"  maintainers:\n",
                b"  excluded_platforms: linux_amd64\n  maintainers:\n",
            ),
        )
        original = self.proposal_bytes()
        for label, before, after in mutations:
            with self.subTest(label=label):
                proposal = self.root / label / "description.yml"
                proposal.parent.mkdir()
                proposal.write_bytes(original.replace(before, after))
                result = self.run_descriptor(
                    self.root / f"{label}-output", proposal=proposal
                )
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("proposal", result.stderr)

    def test_duplicate_and_ambiguous_yaml_are_rejected(self) -> None:
        mutations = (
            (
                "duplicate-key",
                self.proposal_bytes().replace(
                    b"  version: 0.2.0\n",
                    b"  version: 0.2.0\n  version: 0.2.0\n",
                ),
            ),
            (
                "duplicate-section",
                self.proposal_bytes() + b"repo:\n  github: ngalluzzo/duckdb-fdw\n",
            ),
            (
                "alias",
                self.proposal_bytes().replace(
                    b"  version: 0.2.0", b"  version: &version 0.2.0"
                ),
            ),
            (
                "merge-key",
                self.proposal_bytes().replace(
                    b"  name: duckdb_api", b"  <<: *extension\n  name: duckdb_api"
                ),
            ),
        )
        for label, payload in mutations:
            with self.subTest(label=label):
                proposal = self.root / label / "description.yml"
                proposal.parent.mkdir()
                proposal.write_bytes(payload)
                result = self.run_descriptor(
                    self.root / f"{label}-output", proposal=proposal
                )
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("canonical exact YAML", result.stderr)


if __name__ == "__main__":
    import unittest

    unittest.main()

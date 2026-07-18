#!/usr/bin/env python3
"""Reviewed Community build identity and authority tests."""

from __future__ import annotations

import copy
import json
import pathlib
import sys


sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from build_evidence_authority_fixture import digest  # noqa: E402
from build_evidence_test_support import BuildEvidenceFixture  # noqa: E402
from test_support import canonical_write  # noqa: E402


class BuildEvidenceAuthorityTest(BuildEvidenceFixture):
    def test_wrong_external_identities_fail_against_reviewed_authority(self) -> None:
        cases = (
            ("pull_request", lambda value: value.__setitem__("repository", "other/repo"), "repository"),
            ("pull_request", lambda value: value.__setitem__("number", 2257), "number"),
            ("pull_request", lambda value: value["head"].__setitem__("sha", "f" * 40), "head"),
            ("pull_request", lambda value: value["base"].__setitem__("sha", "c" * 40), "base"),
            ("run", lambda value: value.__setitem__("attempt", 3), "run identity"),
            ("run", lambda value: value["workflow"].__setitem__("id", 78), "workflow"),
            ("run", lambda value: value.__setitem__("head_ref", "main"), "head ref"),
        )
        for index, (name, mutate, message) in enumerate(cases):
            with self.subTest(name=name, index=index):
                original = copy.deepcopy(self.exports[name])
                mutate(self.exports[name])
                canonical_write(self.export_paths()[name], self.exports[name])
                self.refresh_approval()
                result = self.run_collector(self.root / f"identity-output-{index}")
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(message, result.stderr)
                self.reset_export(name, original)

    def test_caller_cannot_self_approve_alternate_registry(self) -> None:
        self.authority["pull_request_number"] = 2257
        canonical_write(
            self.registry,
            {
                "approved": [self.authority],
                "schema": "duckdb_api/community-build-authorities/v1",
            },
        )
        result = self.run_collector(self.root / "self-approved-output")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("exact reviewed policy", result.stderr)

    def test_descriptor_admission_must_name_the_loaded_pins(self) -> None:
        descriptor = json.loads(self.descriptor.read_text(encoding="utf-8"))
        descriptor["pins_sha256"] = "0" * 64
        canonical_write(self.descriptor, descriptor)
        self.descriptor_anchor.write_text(
            f"{digest(self.descriptor)}  descriptor-admission.json\n",
            encoding="ascii",
        )
        self.authority["descriptor_admission_sha256"] = digest(self.descriptor)
        self.refresh_approval()
        result = self.run_collector(self.root / "wrong-descriptor-pins-output")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("different Community pins", result.stderr)


if __name__ == "__main__":
    import unittest

    unittest.main()

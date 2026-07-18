#!/usr/bin/env python3
"""Community descriptor authority and custody tests."""

from __future__ import annotations

import hashlib
import json
import pathlib
import sys


sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from descriptor_test_support import (  # noqa: E402
    PUBLISHED_COMMIT,
    DescriptorFixture,
)


class DescriptorAuthorityTest(DescriptorFixture):
    def test_self_anchored_candidate_semantic_drift_is_rejected(self) -> None:
        self.rewrite_candidate(
            lambda record: record["project"].__setitem__("version", "0.2.1")
        )
        result = self.run_descriptor(self.root / "candidate-drift-output")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("candidate project identity drifted", result.stderr)

    def test_candidate_and_dependency_anchor_drift_are_rejected(self) -> None:
        candidate_anchor = self.candidate / "candidate.sha256"
        valid_candidate_anchor = candidate_anchor.read_text(encoding="ascii")
        candidate_anchor.chmod(0o644)
        candidate_anchor.write_text("0" * 64 + "  candidate.json\n", encoding="ascii")
        result = self.run_descriptor(self.root / "candidate-anchor-output")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("candidate anchor syntax or digest drifted", result.stderr)

        candidate_anchor.write_text(valid_candidate_anchor, encoding="ascii")
        audit_anchor = self.audit / "dependency-audit.sha256"
        audit_anchor.chmod(0o644)
        audit_anchor.write_text(
            "0" * 64 + "  dependency-audit.json\n", encoding="ascii"
        )
        result = self.run_descriptor(self.root / "audit-anchor-output")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("dependency audit anchor syntax or digest drifted", result.stderr)

    def test_candidate_dependency_binding_drift_is_rejected(self) -> None:
        self.rewrite_candidate(
            lambda record: record["dependency_audit"].__setitem__("sha256", "0" * 64)
        )
        result = self.run_descriptor(self.root / "dependency-binding-output")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("different dependency audit", result.stderr)

    def test_jointly_reanchored_alternate_handoff_is_not_authorized(self) -> None:
        alternate_commit = "f" * 40
        alternate_tree = "e" * 40
        audit_record = self.audit / "dependency-audit.json"
        audit_anchor = self.audit / "dependency-audit.sha256"
        audit = json.loads(audit_record.read_text(encoding="utf-8"))
        audit["project_source"]["commit"] = alternate_commit
        audit["project_source"]["tree"] = alternate_tree
        audit_payload = (json.dumps(audit, indent=2, sort_keys=True) + "\n").encode()
        audit_record.chmod(0o644)
        audit_anchor.chmod(0o644)
        audit_record.write_bytes(audit_payload)
        audit_anchor.write_text(
            f"{hashlib.sha256(audit_payload).hexdigest()}  dependency-audit.json\n",
            encoding="ascii",
        )

        candidate_record = self.candidate / "candidate.json"
        candidate_anchor = self.candidate / "candidate.sha256"
        candidate = json.loads(candidate_record.read_text(encoding="utf-8"))
        candidate["source"] = {"commit": alternate_commit, "tree": alternate_tree}
        candidate["dependency_audit"]["sha256"] = self.digest(audit_record)
        candidate["dependency_audit"]["anchor_sha256"] = self.digest(audit_anchor)
        candidate_payload = (
            json.dumps(candidate, indent=2, sort_keys=True) + "\n"
        ).encode()
        candidate_record.chmod(0o644)
        candidate_anchor.chmod(0o644)
        candidate_record.write_bytes(candidate_payload)
        candidate_anchor.write_text(
            f"{hashlib.sha256(candidate_payload).hexdigest()}  candidate.json\n",
            encoding="ascii",
        )
        self.proposal.write_bytes(
            self.proposal_bytes().replace(
                PUBLISHED_COMMIT.encode(), alternate_commit.encode()
            )
        )
        self.write_cycle(alternate_commit, alternate_tree)

        result = self.run_descriptor(self.root / "alternate-handoff-output")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("exact reviewed authority", result.stderr)


if __name__ == "__main__":
    import unittest

    unittest.main()

"""Focused tests for retained-evidence normalization."""

from __future__ import annotations

import json
import pathlib
import unittest

from evidence_output import redact_evidence


class EvidenceOutputTests(unittest.TestCase):
    def test_redaction_prefers_specific_roots_and_recurses(self) -> None:
        repository = pathlib.Path("/private/work/repository")
        inputs = repository / "bundle"
        evidence = {
            "diagnostic": f"failed to install {inputs}/artifact",
            "paths": [str(repository / "release"), str(inputs / "manifest")],
        }

        redacted = redact_evidence(
            evidence,
            ((repository, "<repository-root>"), (inputs, "<input-root>")),
        )

        self.assertEqual(
            redacted,
            {
                "diagnostic": "failed to install <input-root>/artifact",
                "paths": [
                    "<repository-root>/release",
                    "<input-root>/manifest",
                ],
            },
        )
        self.assertNotIn("/private/", json.dumps(redacted))

#!/usr/bin/env python3
"""Pending descriptor expectation admission tests."""

from __future__ import annotations

import pathlib
import sys


sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from test_support import ProviderFixture, canonical_write  # noqa: E402


class DescriptorAdmissionTest(ProviderFixture):
    def test_pending_expectation_is_admitted_without_source_or_support_claim(self) -> None:
        result = self.run_script(
            "verify_descriptor.py",
            "--pins",
            self.pins_path,
            "--descriptor-expectation",
            self.descriptor_path,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "descriptor.json\n")

    def test_source_identity_or_support_claim_is_rejected_while_pending(self) -> None:
        expected = self.descriptor["expected"]
        assert isinstance(expected, dict)
        expected["source_commit"] = self.project_commit
        self.descriptor["support_claims"] = ["linux_amd64"]
        canonical_write(self.descriptor_path, self.descriptor)
        result = self.run_script(
            "verify_descriptor.py",
            "--pins",
            self.pins_path,
            "--descriptor-expectation",
            self.descriptor_path,
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("no support claims", result.stderr)

    def test_repository_and_ref_metadata_must_match_exact_pins(self) -> None:
        mutations = (
            ("repository", "duckdb", "repository", "https://github.com/duckdb/duckdb-fork"),
            ("duckdb-ref", "duckdb", "ref", "main"),
            ("community-ref-type", "community_extensions", "ref_type", "tag"),
            ("template-ref", "extension_template", "ref", "0" * 40),
        )
        for label, pin_name, field, value in mutations:
            with self.subTest(label=label):
                pins = self.pins[pin_name]
                assert isinstance(pins, dict)
                original = pins[field]
                pins[field] = value
                canonical_write(self.pins_path, self.pins)
                result = self.run_script(
                    "verify_descriptor.py",
                    "--pins",
                    self.pins_path,
                    "--descriptor-expectation",
                    self.descriptor_path,
                )
                self.assertNotEqual(result.returncode, 0)
                pins[field] = original
                canonical_write(self.pins_path, self.pins)


if __name__ == "__main__":
    import unittest

    unittest.main()

"""Focused tests for the installation-oracle CLI contract."""

from __future__ import annotations

import unittest

from install_oracle import parse_args


class PublicCliTests(unittest.TestCase):
    def test_cli_retains_every_explicit_provider_path(self) -> None:
        arguments = [
            "--supported-python",
            "supported",
            "--mismatch-python",
            "mismatch",
            "--artifact",
            "artifact",
            "--manifest",
            "manifest",
            "--manifest-anchor",
            "anchor",
            "--verifier",
            "verifier",
            "--negative-fixture-inventory",
            "negative-fixtures",
            "--wrong-platform-artifact",
            "wrong-platform",
            "--corrupted-artifact",
            "corrupted",
        ]

        parsed = parse_args(arguments)

        self.assertEqual(
            vars(parsed),
            {
                "artifact": "artifact",
                "corrupted_artifact": "corrupted",
                "manifest": "manifest",
                "manifest_anchor": "anchor",
                "mismatch_python": "mismatch",
                "negative_fixture_inventory": "negative-fixtures",
                "supported_python": "supported",
                "verifier": "verifier",
                "wrong_platform_artifact": "wrong-platform",
            },
        )

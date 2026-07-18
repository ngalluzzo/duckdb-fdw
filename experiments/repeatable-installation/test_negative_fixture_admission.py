"""Counterexample tests for negative-fixture admission."""

from __future__ import annotations

import pathlib
import tempfile
import unittest

from query_oracle_test_support import digest, trial_package, write_inventory
from trial_inputs import verify_trial_inputs


class NegativeFixtureAdmissionTests(unittest.TestCase):
    def test_rejects_same_size_counterfeit_even_with_matching_inventory_hash(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            inputs, inventory, _ = trial_package(pathlib.Path(directory))
            counterfeit = bytearray(inputs.wrong_platform_artifact.read_bytes())
            counterfeit[0] ^= 0x01
            inputs.wrong_platform_artifact.write_bytes(counterfeit)
            record = inventory["fixtures"][inputs.wrong_platform_artifact.name]
            record["sha256"] = digest(counterfeit)
            write_inventory(inputs.negative_fixture_inventory, inventory)

            with self.assertRaisesRegex(AssertionError, "outside its recorded"):
                verify_trial_inputs(inputs)

    def test_rejects_wrong_negative_fixture_inventory(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            inputs, inventory, _ = trial_package(pathlib.Path(directory))
            inventory["source"]["filename"] = "counterfeit.duckdb_extension"
            write_inventory(inputs.negative_fixture_inventory, inventory)

            with self.assertRaisesRegex(AssertionError, "source identity"):
                verify_trial_inputs(inputs)

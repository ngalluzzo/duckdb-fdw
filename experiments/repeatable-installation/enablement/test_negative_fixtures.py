#!/usr/bin/env python3
"""Deterministic negative-artifact construction tests."""

from __future__ import annotations

import json
import pathlib
import sys
import tempfile
import unittest


sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from enablement_test_support import EnablementTestCase, synthetic_extension


class NegativeFixtureTest(EnablementTestCase):
    def test_negative_fixtures_change_only_the_recorded_region(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            artifact = root / "duckdb_api.duckdb_extension"
            original = synthetic_extension()
            artifact.write_bytes(original)
            output = root / "fixtures"
            completed = self.run_script(
                "make_negative_fixture.py", "--artifact", artifact, "--output", output
            )
            self.assertEqual(completed.returncode, 0, completed.stderr)

            wrong = (output / "wrong-platform.duckdb_extension").read_bytes()
            corrupted_path = output / "corrupted/duckdb_api.duckdb_extension"
            corrupted = corrupted_path.read_bytes()
            self.assertEqual(corrupted_path.name, artifact.name)
            wrong_differences = [
                index
                for index, values in enumerate(zip(original, wrong, strict=True))
                if values[0] != values[1]
            ]
            corrupted_differences = [
                index
                for index, values in enumerate(zip(original, corrupted, strict=True))
                if values[0] != values[1]
            ]
            footer_start = len(original) - 512
            self.assertEqual(
                wrong_differences,
                list(range(footer_start + 192, footer_start + 192 + 11)),
            )
            self.assertEqual(corrupted_differences, [footer_start // 2])
            self.assertEqual(corrupted[-512:], original[-512:])

            inventory_path = output / "negative-fixtures.json"
            inventory = json.loads(inventory_path.read_text())
            self.assertEqual(
                inventory_path.read_bytes(),
                (json.dumps(inventory, indent=2, sort_keys=True) + "\n").encode(),
            )
            self.assertEqual(
                inventory["schema"], "duckdb_api/installability-negative-fixtures/v1"
            )
            self.assertEqual(
                inventory["fixtures"]["duckdb_api.duckdb_extension"]["mutation"][
                    "offset"
                ],
                footer_start // 2,
            )

    def test_existing_fixture_output_is_refused(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            artifact = root / "duckdb_api.duckdb_extension"
            artifact.write_bytes(synthetic_extension())
            output = root / "existing"
            output.mkdir()
            completed = self.run_script(
                "make_negative_fixture.py", "--artifact", artifact, "--output", output
            )
            self.assertNotEqual(completed.returncode, 0)
            self.assertIn("output already exists", completed.stderr)


if __name__ == "__main__":
    unittest.main()

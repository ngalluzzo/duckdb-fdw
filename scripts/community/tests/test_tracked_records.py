#!/usr/bin/env python3
"""Admission coverage for the tracked 0.2.0 provider records."""

from __future__ import annotations

import pathlib
import subprocess
import sys
import unittest


HERE = pathlib.Path(__file__).resolve().parent
REPOSITORY = HERE.parents[2]
COMMUNITY = HERE.parent
ENABLEMENT = REPOSITORY / "release/0.2.0/enablement"
sys.path.insert(0, str(COMMUNITY))

from audit_dependencies import _validate_expectations  # noqa: E402
from candidate_pins import validate_pins  # noqa: E402
from record_format import load_canonical_object  # noqa: E402
from verify_descriptor import validate_expectation  # noqa: E402


class TrackedRecordTest(unittest.TestCase):
    def test_tracked_records_are_canonical_and_mutually_admitted(self) -> None:
        pins, _pins_digest = load_canonical_object(
            (ENABLEMENT / "pins.json").resolve(), "tracked Community pins"
        )
        dependencies, dependencies_digest = load_canonical_object(
            (ENABLEMENT / "dependencies.json").resolve(),
            "tracked dependency expectations",
        )
        descriptor, _descriptor_digest = load_canonical_object(
            (ENABLEMENT / "descriptor.json").resolve(),
            "tracked descriptor expectation",
        )
        validate_pins(pins)
        self.assertEqual(
            dependencies_digest, pins["dependency_expectations_sha256"]
        )
        _validate_expectations(dependencies)
        validate_expectation(descriptor, pins)

        result = subprocess.run(
            [
                sys.executable,
                "-I",
                "-B",
                str(COMMUNITY / "verify_descriptor.py"),
                "--pins",
                str(ENABLEMENT / "pins.json"),
                "--descriptor-expectation",
                str(ENABLEMENT / "descriptor.json"),
            ],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "descriptor.json\n")


if __name__ == "__main__":
    unittest.main()

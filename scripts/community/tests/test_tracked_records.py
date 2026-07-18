#!/usr/bin/env python3
"""Admission coverage for the tracked 0.2.0 provider records."""

from __future__ import annotations

import hashlib
import pathlib
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
from descriptor_expectation import validate_expectation  # noqa: E402
from descriptor_cycle import APPROVED_CYCLE_SHA256, validate_descriptor_cycle  # noqa: E402
from descriptor_proposal import validate_proposal  # noqa: E402


class TrackedRecordTest(unittest.TestCase):
    def test_tracked_descriptor_proposal_is_exact_for_published_candidate(self) -> None:
        pins, _pins_digest = load_canonical_object(
            (ENABLEMENT / "pins.json").resolve(), "tracked Community pins"
        )
        candidate = {
            "source": {
                "commit": "47dc6169ae820f70beb0c2722b8a8f5288cd1469",
                "tree": "6356b5296276aff08f81a6ec3ef9da6d0a6b8f7a",
            }
        }
        validate_proposal(
            (ENABLEMENT / "description.yml").read_bytes(), candidate, pins
        )
        cycle, cycle_digest = load_canonical_object(
            (ENABLEMENT / "descriptor-cycle.json").resolve(),
            "tracked descriptor cycle",
        )
        self.assertEqual(cycle_digest, APPROVED_CYCLE_SHA256)
        validate_descriptor_cycle(cycle, cycle_digest)
        self.assertEqual(cycle["source"], candidate["source"])
        for name, path in (
            ("pins_sha256", ENABLEMENT / "pins.json"),
            ("descriptor_expectation_sha256", ENABLEMENT / "descriptor.json"),
            ("proposal_sha256", ENABLEMENT / "description.yml"),
        ):
            self.assertEqual(cycle[name], hashlib.sha256(path.read_bytes()).hexdigest())

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


if __name__ == "__main__":
    unittest.main()

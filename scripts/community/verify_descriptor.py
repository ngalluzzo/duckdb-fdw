#!/usr/bin/env python3
"""Admit only a non-authoritative pending Community descriptor expectation."""

from __future__ import annotations

import argparse
import pathlib
import sys
from typing import Any


HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

from candidate_pins import validate_pins
from record_format import (  # noqa: E402
    AdmissionError,
    load_canonical_object,
    require,
)


DESCRIPTOR_SCHEMA = "duckdb_api/community-descriptor-expectation/v1"


def validate_expectation(
    descriptor: dict[str, Any], pins: dict[str, Any]
) -> dict[str, Any]:
    validate_pins(pins)
    require(descriptor.get("schema") == DESCRIPTOR_SCHEMA,
            "descriptor expectation schema is unsupported")
    require(
        set(descriptor)
        == {
            "schema",
            "status",
            "authority",
            "publication_status",
            "support_claims",
            "expected",
        },
        "descriptor expectation fields drifted",
    )
    require(descriptor.get("status") == "pending_non_authoritative",
            "descriptor expectation must remain pending and non-authoritative")
    require(descriptor.get("authority") == "none",
            "pending descriptor expectation must claim no authority")
    require(descriptor.get("publication_status") == "not_submitted",
            "pending descriptor expectation must not claim submission")
    require(descriptor.get("support_claims") == [],
            "pending descriptor expectation must contain no support claims")

    expected = descriptor.get("expected")
    require(isinstance(expected, dict), "descriptor expected fields must be an object")
    require(
        set(expected)
        == {
            "extension",
            "version",
            "license",
            "repository",
            "build_language",
            "source_ref",
            "source_commit",
            "maintainers",
            "maintainers_status",
        },
        "descriptor expected fields drifted",
    )
    project = pins["project"]
    assert isinstance(project, dict)
    require(expected.get("extension") == project["extension"],
            "descriptor extension expectation drifted")
    require(expected.get("version") == project["version"],
            "descriptor version expectation drifted")
    license_pin = project["license"]
    assert isinstance(license_pin, dict)
    require(expected.get("license") == license_pin["spdx"],
            "descriptor license expectation drifted")
    require(expected.get("repository") == project["repository"],
            "descriptor repository expectation drifted")
    require(expected.get("build_language") == "C++",
            "descriptor build language expectation drifted")
    require(expected.get("source_ref") is None and expected.get("source_commit") is None,
            "pending descriptor expectation must not invent a source identity")
    require(expected.get("maintainers") == [],
            "pending descriptor expectation must not invent maintainers")
    require(expected.get("maintainers_status") == "pending",
            "descriptor maintainers must remain explicitly pending")
    return descriptor


def parser() -> argparse.ArgumentParser:
    value = argparse.ArgumentParser()
    value.add_argument("--pins", type=pathlib.Path, required=True)
    value.add_argument("--descriptor-expectation", type=pathlib.Path, required=True)
    return value


def main() -> int:
    arguments = parser().parse_args()
    try:
        pins, _pins_digest = load_canonical_object(arguments.pins, "Community pins")
        descriptor, _descriptor_digest = load_canonical_object(
            arguments.descriptor_expectation, "descriptor expectation"
        )
        validate_expectation(descriptor, pins)
        print("descriptor.json")
        return 0
    except AdmissionError as error:
        print(f"descriptor expectation admission failed: {error}", file=sys.stderr)
        return 1
    except (OSError, ValueError):
        print(
            "descriptor expectation admission failed: filesystem operation failed",
            file=sys.stderr,
        )
        return 1


if __name__ == "__main__":
    raise SystemExit(main())

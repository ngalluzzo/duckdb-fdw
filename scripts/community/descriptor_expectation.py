"""Validate the non-authoritative descriptor expectation bound into a candidate."""

from __future__ import annotations

from typing import Any

from candidate_pins import validate_pins
from record_format import require


DESCRIPTOR_SCHEMA = "duckdb_api/community-descriptor-expectation/v1"


def _mapping(value: object, label: str) -> dict[str, Any]:
    require(isinstance(value, dict), f"{label} must be an object")
    return value


def validate_expectation(
    descriptor: dict[str, Any], pins: dict[str, Any]
) -> dict[str, Any]:
    """Keep pre-proposal expectation bytes explicitly non-authoritative."""

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

    expected = _mapping(descriptor.get("expected"), "descriptor expected fields")
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
    project = _mapping(pins["project"], "project pins")
    require(expected.get("extension") == project["extension"],
            "descriptor extension expectation drifted")
    require(expected.get("version") == project["version"],
            "descriptor version expectation drifted")
    license_pin = _mapping(project["license"], "project license pin")
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

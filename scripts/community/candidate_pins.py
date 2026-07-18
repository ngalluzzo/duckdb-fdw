#!/usr/bin/env python3
"""Validate the complete immutable pin set for one Community candidate cycle."""

from __future__ import annotations

from typing import Any

from git_snapshot import require_commit
from record_format import require


PIN_SCHEMA = "duckdb_api/community-enablement-pins/v1"
OFFICIAL_REPOSITORIES = {
    "duckdb": "https://github.com/duckdb/duckdb",
    "community_extensions": "https://github.com/duckdb/community-extensions",
    "extension_template": "https://github.com/duckdb/extension-template",
    "extension_ci_tools": "https://github.com/duckdb/extension-ci-tools",
}


def _mapping(value: object, label: str) -> dict[str, Any]:
    require(isinstance(value, dict), f"{label} must be an object")
    return value


def _sha256(value: object, label: str) -> str:
    require(
        isinstance(value, str)
        and len(value) == 64
        and all(character in "0123456789abcdef" for character in value),
        f"{label} must be lowercase SHA-256",
    )
    return value


def _license(value: object, label: str, expected_digest: str | None = None) -> None:
    license_pin = _mapping(value, label)
    require(set(license_pin) == {"path", "sha256", "spdx"},
            f"{label} fields drifted")
    require(license_pin.get("path") == "LICENSE", f"{label} path drifted")
    require(license_pin.get("spdx") == "MIT", f"{label} must be MIT")
    digest = _sha256(license_pin.get("sha256"), f"{label} digest")
    if expected_digest is not None:
        require(digest == expected_digest, f"{label} digest drifted")


def validate_pins(pins: dict[str, Any]) -> dict[str, Any]:
    require(pins.get("schema") == PIN_SCHEMA, "pin schema is unsupported")
    required = {
        "schema",
        "project",
        "dependency_expectations_sha256",
        "duckdb",
        "community_extensions",
        "extension_template",
        "extension_ci_tools",
    }
    require(set(pins) == required, "pin object fields drifted")
    _sha256(pins.get("dependency_expectations_sha256"),
            "dependency expectation digest")

    project = _mapping(pins["project"], "project pins")
    require(
        set(project)
        == {"extension", "license", "repository", "tag", "version"},
        "project pin fields drifted",
    )
    require(project.get("extension") == "duckdb_api", "extension pin drifted")
    require(project.get("version") == "0.2.0", "project version pin drifted")
    require(project.get("tag") == "v0.2.0", "project tag pin drifted")
    require(project.get("repository") == "https://github.com/ngalluzzo/duckdb-fdw",
            "project repository pin drifted")
    _license(project.get("license"), "project license pin")

    for name in ("duckdb", "community_extensions", "extension_template", "extension_ci_tools"):
        entry = _mapping(pins[name], f"{name} pins")
        require_commit(entry.get("commit"), f"{name} commit")
        require_commit(entry.get("tree"), f"{name} tree")
        require(
            entry.get("repository") == OFFICIAL_REPOSITORIES[name],
            f"{name} repository pin drifted",
        )
    duckdb = _mapping(pins["duckdb"], "DuckDB pins")
    require(
        set(duckdb)
        == {"commit", "license", "ref", "ref_type", "repository", "tree", "version"},
        "DuckDB pin fields drifted",
    )
    require(duckdb.get("version") == "1.5.4", "DuckDB version pin drifted")
    require(duckdb.get("ref") == "v1.5.4", "DuckDB release ref drifted")
    require(duckdb.get("ref_type") == "tag", "DuckDB ref type drifted")
    _license(duckdb.get("license"), "DuckDB license pin")

    community = _mapping(pins["community_extensions"], "Community pins")
    require(
        set(community) == {"commit", "ref", "ref_type", "repository", "tree"},
        "Community pin fields drifted",
    )
    require(community.get("ref") == "main", "Community ref drifted")
    require(community.get("ref_type") == "branch", "Community ref type drifted")

    template = _mapping(pins["extension_template"], "extension-template pins")
    require(
        set(template)
        == {"commit", "license", "ref", "ref_type", "repository", "tree"},
        "extension-template pin fields drifted",
    )
    require(template.get("ref") == template.get("commit"),
            "extension-template commit ref drifted")
    require(template.get("ref_type") == "commit",
            "extension-template ref type drifted")
    _license(template.get("license"), "extension-template license pin")

    ci_tools = _mapping(pins["extension_ci_tools"], "extension-ci-tools pins")
    require(
        set(ci_tools) == {"commit", "ref", "ref_type", "repository", "tree"},
        "extension-ci-tools pin fields drifted",
    )
    require(ci_tools.get("ref") == "v1.5-variegata", "ci-tools ref drifted")
    require(ci_tools.get("ref_type") == "branch", "ci-tools ref type must be explicit")
    return pins

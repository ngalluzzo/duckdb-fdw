"""Validate the complete provider candidate record at consumer boundaries."""

from __future__ import annotations

import re
from typing import Any

from audit_dependencies import AUDIT_SCHEMA
from candidate_pins import validate_pins
from git_snapshot import require_commit
from record_format import require


CANDIDATE_SCHEMA = "duckdb_api/community-candidate/v1"
SHA256 = re.compile(r"[0-9a-f]{64}")


def _mapping(value: object, label: str) -> dict[str, Any]:
    require(isinstance(value, dict), f"{label} must be an object")
    return value


def _fields(value: object, expected: set[str], label: str) -> dict[str, Any]:
    result = _mapping(value, label)
    require(set(result) == expected, f"{label} fields drifted")
    return result


def _sha256(value: object, label: str) -> str:
    require(isinstance(value, str) and SHA256.fullmatch(value) is not None,
            f"{label} must be lowercase SHA-256")
    return value


def validate_candidate_record(
    candidate: dict[str, Any],
    pins: dict[str, Any],
    pins_digest: str,
    expectation_digest: str,
) -> dict[str, Any]:
    """Re-admit one immutable candidate without trusting its self-description."""

    validate_pins(pins)
    root = _fields(
        candidate,
        {
            "community",
            "dependency_audit",
            "descriptor_expectation",
            "duckdb",
            "pins_sha256",
            "project",
            "schema",
            "source",
            "status",
        },
        "candidate record",
    )
    require(root["schema"] == CANDIDATE_SCHEMA,
            "candidate record schema is unsupported")
    require(root["status"] == "admitted_candidate",
            "candidate record status drifted")
    require(root["pins_sha256"] == pins_digest,
            "candidate record names different pins")

    project_pin = _mapping(pins["project"], "project pins")
    project_license = _mapping(project_pin["license"], "project license pin")
    project = _fields(
        root["project"],
        {"extension", "license", "repository", "tag", "tag_state", "version"},
        "candidate project",
    )
    require(
        project
        == {
            "extension": project_pin["extension"],
            "license": project_license["spdx"],
            "repository": project_pin["repository"],
            "tag": project_pin["tag"],
            "tag_state": "pending",
            "version": project_pin["version"],
        },
        "candidate project identity drifted",
    )
    source = _fields(root["source"], {"commit", "tree"}, "candidate source")
    require_commit(source["commit"], "candidate source commit")
    require_commit(source["tree"], "candidate source tree")

    duckdb_pin = _mapping(pins["duckdb"], "DuckDB pins")
    require(
        _fields(root["duckdb"], {"commit", "version"}, "candidate DuckDB")
        == {"commit": duckdb_pin["commit"], "version": duckdb_pin["version"]},
        "candidate DuckDB identity drifted",
    )
    community_pin = _mapping(pins["community_extensions"], "Community pins")
    template_pin = _mapping(pins["extension_template"], "template pins")
    ci_tools_pin = _mapping(pins["extension_ci_tools"], "ci-tools pins")
    community = _fields(
        root["community"],
        {"commit", "extension_ci_tools", "extension_template", "repository"},
        "candidate Community identity",
    )
    require(
        community["repository"] == community_pin["repository"]
        and community["commit"] == community_pin["commit"],
        "candidate Community identity drifted",
    )
    require(
        _fields(
            community["extension_template"],
            {"commit", "repository"},
            "candidate template identity",
        )
        == {"commit": template_pin["commit"], "repository": template_pin["repository"]},
        "candidate template identity drifted",
    )
    require(
        _fields(
            community["extension_ci_tools"],
            {"commit", "ref", "repository"},
            "candidate ci-tools identity",
        )
        == {
            "commit": ci_tools_pin["commit"],
            "ref": ci_tools_pin["ref"],
            "repository": ci_tools_pin["repository"],
        },
        "candidate ci-tools identity drifted",
    )
    require(
        _fields(
            root["descriptor_expectation"],
            {"sha256", "status"},
            "candidate descriptor expectation",
        )
        == {"sha256": expectation_digest, "status": "pending_non_authoritative"},
        "candidate descriptor expectation drifted",
    )
    dependency = _fields(
        root["dependency_audit"],
        {"anchor_sha256", "schema", "sha256"},
        "candidate dependency audit",
    )
    require(dependency["schema"] == AUDIT_SCHEMA,
            "candidate dependency audit schema drifted")
    _sha256(dependency["sha256"], "candidate dependency audit digest")
    _sha256(dependency["anchor_sha256"], "candidate dependency anchor digest")
    return root

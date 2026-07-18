"""Validate the reviewed authority for one immutable descriptor handoff."""

from __future__ import annotations

import re
from typing import Any

from git_snapshot import require_commit
from record_format import require


CYCLE_SCHEMA = "duckdb_api/community-descriptor-cycle/v1"
SHA256 = re.compile(r"[0-9a-f]{64}")
APPROVED_CYCLE_SHA256 = (
    "70f81ca09599cffaef4a57e7abf6d5bdabed2c980c01beac03dc65f28c3a6730"
)
APPROVED_SOURCE = {
    "commit": "47dc6169ae820f70beb0c2722b8a8f5288cd1469",
    "tree": "6356b5296276aff08f81a6ec3ef9da6d0a6b8f7a",
}


def _mapping(value: object, label: str) -> dict[str, Any]:
    require(isinstance(value, dict), f"{label} must be an object")
    return value


def _digest(value: object, label: str) -> str:
    require(isinstance(value, str) and SHA256.fullmatch(value) is not None,
            f"{label} must be lowercase SHA-256")
    return value


def validate_descriptor_cycle(
    cycle: dict[str, Any], cycle_digest: str
) -> dict[str, Any]:
    """Admit reviewed identities; adjacent self-anchors are custody only."""

    require(
        cycle_digest == APPROVED_CYCLE_SHA256,
        "descriptor cycle is not the exact reviewed authority",
    )
    require(
        set(cycle)
        == {
            "candidate",
            "dependency_audit",
            "descriptor_expectation_sha256",
            "pins_sha256",
            "proposal_sha256",
            "schema",
            "source",
        },
        "descriptor cycle fields drifted",
    )
    require(cycle.get("schema") == CYCLE_SCHEMA,
            "descriptor cycle schema is unsupported")
    source = _mapping(cycle.get("source"), "descriptor cycle source")
    require(set(source) == {"commit", "tree"},
            "descriptor cycle source fields drifted")
    require_commit(source.get("commit"), "descriptor cycle source commit")
    require_commit(source.get("tree"), "descriptor cycle source tree")
    require(
        source == APPROVED_SOURCE,
        "descriptor cycle source is not the approved published candidate",
    )
    for name in ("candidate", "dependency_audit"):
        identity = _mapping(cycle.get(name), f"descriptor cycle {name}")
        require(set(identity) == {"anchor_sha256", "sha256"},
                f"descriptor cycle {name} fields drifted")
        _digest(identity.get("sha256"), f"descriptor cycle {name} digest")
        _digest(
            identity.get("anchor_sha256"),
            f"descriptor cycle {name} anchor digest",
        )
    for name in (
        "descriptor_expectation_sha256",
        "pins_sha256",
        "proposal_sha256",
    ):
        _digest(cycle.get(name), f"descriptor cycle {name}")
    return cycle

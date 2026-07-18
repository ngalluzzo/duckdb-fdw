#!/usr/bin/env python3
"""Compose anchored provider inputs into one local descriptor admission."""

from __future__ import annotations

import argparse
import pathlib
import sys
from typing import Any


HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

from audit_dependencies import validate_audit_record  # noqa: E402
from candidate_record import validate_candidate_record  # noqa: E402
from descriptor_cycle import validate_descriptor_cycle  # noqa: E402
from descriptor_expectation import validate_expectation  # noqa: E402
from descriptor_proposal import validate_proposal  # noqa: E402
from record_format import (  # noqa: E402
    AdmissionError,
    load_canonical_object,
    prepare_output_root,
    read_regular_bytes,
    require,
    sha256_bytes,
    verify_anchored_object,
    write_anchored_json,
)


ADMISSION_SCHEMA = "duckdb_api/community-descriptor-admission/v1"


def _mapping(value: object, label: str) -> dict[str, Any]:
    require(isinstance(value, dict), f"{label} must be an object")
    return value


def descriptor_admission(
    pins: dict[str, Any],
    pins_digest: str,
    expectation: dict[str, Any],
    expectation_digest: str,
    candidate: dict[str, Any],
    candidate_digest: str,
    candidate_anchor_digest: str,
    dependency_audit: dict[str, Any],
    dependency_digest: str,
    dependency_anchor_digest: str,
    proposal: bytes,
    cycle: dict[str, Any],
    cycle_digest: str,
) -> dict[str, Any]:
    """Bind proposal bytes to admitted source and dependency authority.

    This transition grants local proposal admission only. Submission, upstream
    CI, signing, deployment, artifact, platform, and support authority remain
    outside this record and require later gates.
    """

    validate_descriptor_cycle(cycle, cycle_digest)
    validate_expectation(expectation, pins)
    validate_candidate_record(candidate, pins, pins_digest, expectation_digest)
    validate_audit_record(dependency_audit, pins)
    require(dependency_audit.get("result") == "input_admitted",
            "dependency inputs were not admitted")
    require(dependency_audit.get("pins_sha256") == pins_digest,
            "dependency audit was produced from different pins")
    audit_source = _mapping(
        dependency_audit.get("project_source"), "dependency audit source"
    )
    source = _mapping(candidate["source"], "candidate source")
    require(audit_source.get("commit") == source["commit"]
            and audit_source.get("tree") == source["tree"],
            "dependency audit names a different candidate source")
    require(
        candidate["dependency_audit"]
        == {
            "anchor_sha256": dependency_anchor_digest,
            "schema": dependency_audit["schema"],
            "sha256": dependency_digest,
        },
        "candidate record names a different dependency audit",
    )
    parsed = validate_proposal(proposal, candidate, pins)
    require(
        cycle["pins_sha256"] == pins_digest
        and cycle["descriptor_expectation_sha256"] == expectation_digest
        and cycle["proposal_sha256"] == sha256_bytes(proposal)
        and cycle["source"] == source
        and cycle["candidate"]
        == {"anchor_sha256": candidate_anchor_digest, "sha256": candidate_digest}
        and cycle["dependency_audit"]
        == {
            "anchor_sha256": dependency_anchor_digest,
            "sha256": dependency_digest,
        },
        "descriptor inputs do not match the reviewed descriptor cycle",
    )
    return {
        "authority": "local_provider_admission_only",
        "candidate": {
            "anchor_sha256": candidate_anchor_digest,
            "sha256": candidate_digest,
            "source": source,
        },
        "dependency_audit": candidate["dependency_audit"],
        "descriptor_expectation_sha256": expectation_digest,
        "descriptor_cycle_sha256": cycle_digest,
        "pins_sha256": pins_digest,
        "proposal": {
            "filename": "description.yml",
            "sha256": sha256_bytes(proposal),
            **parsed,
        },
        "publication_status": "not_submitted",
        "schema": ADMISSION_SCHEMA,
        "status": "proposal_admitted",
        "support_claims": [],
    }


def parser() -> argparse.ArgumentParser:
    value = argparse.ArgumentParser()
    value.add_argument("--pins", type=pathlib.Path, required=True)
    value.add_argument("--descriptor-expectation", type=pathlib.Path, required=True)
    value.add_argument("--descriptor-cycle", type=pathlib.Path, required=True)
    value.add_argument("--proposal", type=pathlib.Path, required=True)
    value.add_argument("--candidate", type=pathlib.Path, required=True)
    value.add_argument("--candidate-anchor", type=pathlib.Path, required=True)
    value.add_argument("--dependency-audit", type=pathlib.Path, required=True)
    value.add_argument("--dependency-anchor", type=pathlib.Path, required=True)
    value.add_argument("--output-root", type=pathlib.Path, required=True)
    return value


def main() -> int:
    arguments = parser().parse_args()
    try:
        pins, pins_digest = load_canonical_object(arguments.pins, "Community pins")
        expectation, expectation_digest = load_canonical_object(
            arguments.descriptor_expectation, "descriptor expectation"
        )
        cycle, cycle_digest = load_canonical_object(
            arguments.descriptor_cycle, "descriptor cycle"
        )
        candidate, candidate_digest, candidate_anchor_digest = verify_anchored_object(
            arguments.candidate,
            arguments.candidate_anchor,
            "candidate.json",
            "candidate",
        )
        audit, audit_digest, audit_anchor_digest = verify_anchored_object(
            arguments.dependency_audit,
            arguments.dependency_anchor,
            "dependency-audit.json",
            "dependency audit",
        )
        require(arguments.proposal.name == "description.yml",
                "Community descriptor proposal filename is invalid")
        proposal = read_regular_bytes(
            arguments.proposal, "Community descriptor proposal", maximum_bytes=65536
        )
        admission = descriptor_admission(
            pins,
            pins_digest,
            expectation,
            expectation_digest,
            candidate,
            candidate_digest,
            candidate_anchor_digest,
            audit,
            audit_digest,
            audit_anchor_digest,
            proposal,
            cycle,
            cycle_digest,
        )
        output_root = prepare_output_root(arguments.output_root)
        write_anchored_json(output_root, "descriptor-admission.json", admission)
        print("descriptor-admission.json")
        return 0
    except AdmissionError as error:
        print(f"descriptor admission failed: {error}", file=sys.stderr)
        return 1
    except (OSError, ValueError):
        print("descriptor admission failed: filesystem operation failed", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())

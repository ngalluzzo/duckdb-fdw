#!/usr/bin/env python3
"""Normalize reviewed local Community run exports and downloaded bytes."""

from __future__ import annotations

import argparse
import os
import pathlib
import sys
from typing import Any


HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

from build_evidence_authority import (  # noqa: E402
    select_authority,
    validate_descriptor_admission,
    validate_registry,
)
from build_evidence_downloads import collect_downloads  # noqa: E402
from build_evidence_exports import (  # noqa: E402
    validate_artifacts_export,
    validate_jobs_export,
    validate_matrix_export,
    validate_pull_request_export,
    validate_run_export,
)
from candidate_pins import validate_pins  # noqa: E402
from record_format import (  # noqa: E402
    AdmissionError,
    canonical_json_bytes,
    load_canonical_object,
    prepare_output_root,
    require,
    sha256_bytes,
    verify_anchored_object,
    write_anchored_json,
)


ROW_SCHEMA = "duckdb_api/community-build/v1"
INVENTORY_SCHEMA = "duckdb_api/community-builds/v1"


def _export_digests(
    pull_request_digest: str,
    run_digest: str,
    jobs_digest: str,
    matrix_digest: str,
    artifacts_digest: str,
) -> dict[str, str]:
    return {
        "artifacts": artifacts_digest,
        "jobs": jobs_digest,
        "matrix": matrix_digest,
        "pull_request": pull_request_digest,
        "run": run_digest,
    }


def _origin(authority: dict[str, Any]) -> dict[str, Any]:
    return {
        "base": authority["base"],
        "extension_source": authority["extension_source"],
        "head": authority["head"],
        "pull_request_number": authority["pull_request_number"],
        "repository": authority["repository"],
        "run": authority["run"],
        "workflow": authority["workflow"],
    }


def normalize_build_evidence(
    authority: dict[str, Any],
    registry_digest: str,
    pins_digest: str,
    descriptor_digest: str,
    descriptor_anchor_digest: str,
    export_digests: dict[str, str],
    jobs: list[dict[str, Any]],
    artifacts_by_job: dict[int, list[dict[str, Any]]],
    logs: dict[int, dict[str, Any]],
    matrix_exclusions: list[dict[str, object]],
    output_root: pathlib.Path,
) -> dict[str, Any]:
    """Write complete provider rows without interpreting compatibility."""

    destination = prepare_output_root(output_root)
    jobs_root = destination / "jobs"
    os.mkdir(jobs_root, mode=0o700)
    rows: list[dict[str, Any]] = []
    origin = _origin(authority)
    for job in jobs:
        row = {
            "artifacts": sorted(
                artifacts_by_job[job["id"]], key=lambda artifact: artifact["id"]
            ),
            "authority": "local_community_build_evidence_only",
            "descriptor_admission": {
                "anchor_sha256": descriptor_anchor_digest,
                "sha256": descriptor_digest,
            },
            "job": job,
            "log": logs[job["id"]],
            "origin": origin,
            "pins_sha256": pins_digest,
            "schema": ROW_SCHEMA,
            "status": "evidence_normalized",
            "support_claims": [],
        }
        row_payload = canonical_json_bytes(row)
        row_directory = jobs_root / f"job-{job['id']}"
        os.mkdir(row_directory, mode=0o700)
        write_anchored_json(row_directory, "community-build.json", row)
        row_digest = sha256_bytes(row_payload)
        anchor_payload = f"{row_digest}  community-build.json\n".encode("ascii")
        rows.append(
            {
                "anchor_path": f"jobs/job-{job['id']}/community-build.sha256",
                "anchor_sha256": sha256_bytes(anchor_payload),
                "conclusion": job["conclusion"],
                "job_id": job["id"],
                "record_path": f"jobs/job-{job['id']}/community-build.json",
                "sha256": row_digest,
            }
        )
    inventory = {
        "authority": "local_community_build_evidence_only",
        "build_authority_registry_sha256": registry_digest,
        "descriptor_admission": {
            "anchor_sha256": descriptor_anchor_digest,
            "sha256": descriptor_digest,
        },
        "exports": export_digests,
        "matrix_exclusions": matrix_exclusions,
        "origin": origin,
        "pins_sha256": pins_digest,
        "rows": rows,
        "schema": INVENTORY_SCHEMA,
        "status": "complete_workflow_inventory_normalized",
        "support_claims": [],
    }
    write_anchored_json(destination, "community-builds.json", inventory)
    return inventory


def parser() -> argparse.ArgumentParser:
    value = argparse.ArgumentParser()
    value.add_argument("--authority-registry", type=pathlib.Path, required=True)
    value.add_argument("--pins", type=pathlib.Path, required=True)
    value.add_argument("--descriptor-admission", type=pathlib.Path, required=True)
    value.add_argument("--descriptor-anchor", type=pathlib.Path, required=True)
    value.add_argument("--pull-request-export", type=pathlib.Path, required=True)
    value.add_argument("--run-export", type=pathlib.Path, required=True)
    value.add_argument("--jobs-export", type=pathlib.Path, required=True)
    value.add_argument("--matrix-export", type=pathlib.Path, required=True)
    value.add_argument("--artifacts-export", type=pathlib.Path, required=True)
    value.add_argument("--artifacts-root", type=pathlib.Path, required=True)
    value.add_argument("--logs-root", type=pathlib.Path, required=True)
    value.add_argument("--output-root", type=pathlib.Path, required=True)
    return value


def main() -> int:
    arguments = parser().parse_args()
    try:
        for path, expected, label in (
            (arguments.authority_registry, "build-authorities.json", "build authority registry"),
            (arguments.pins, "pins.json", "Community pins"),
            (arguments.pull_request_export, "pull-request.json", "pull request export"),
            (arguments.run_export, "run.json", "workflow run export"),
            (arguments.jobs_export, "jobs.json", "jobs export"),
            (arguments.matrix_export, "matrix.json", "matrix export"),
            (arguments.artifacts_export, "artifacts.json", "artifacts export"),
        ):
            require(path.name == expected, f"{label} filename is invalid")
        registry, registry_digest = load_canonical_object(
            arguments.authority_registry, "build authority registry"
        )
        approved = validate_registry(registry, registry_digest)
        pins, pins_digest = load_canonical_object(arguments.pins, "Community pins")
        validate_pins(pins)
        descriptor, descriptor_digest, descriptor_anchor_digest = (
            verify_anchored_object(
                arguments.descriptor_admission,
                arguments.descriptor_anchor,
                "descriptor-admission.json",
                "descriptor admission",
            )
        )
        descriptor_identity = validate_descriptor_admission(
            descriptor, descriptor_digest
        )
        pull_request, pull_request_digest = load_canonical_object(
            arguments.pull_request_export, "pull request export"
        )
        run, run_digest = load_canonical_object(
            arguments.run_export, "workflow run export"
        )
        jobs_export, jobs_digest = load_canonical_object(
            arguments.jobs_export, "jobs export"
        )
        matrix_export, matrix_digest = load_canonical_object(
            arguments.matrix_export, "matrix export"
        )
        artifacts_export, artifacts_digest = load_canonical_object(
            arguments.artifacts_export, "artifacts export"
        )
        export_digests = _export_digests(
            pull_request_digest,
            run_digest,
            jobs_digest,
            matrix_digest,
            artifacts_digest,
        )
        authority = select_authority(
            approved, descriptor_identity, pins_digest, export_digests
        )
        validate_pull_request_export(pull_request, authority)
        validate_run_export(run, authority)
        duckdb = {
            "commit": pins["duckdb"]["commit"],
            "version": pins["duckdb"]["version"],
        }
        jobs = validate_jobs_export(jobs_export, authority, duckdb)
        matrix_exclusions = validate_matrix_export(matrix_export, authority, jobs)
        artifacts = validate_artifacts_export(artifacts_export, authority, jobs)
        artifacts_by_job, logs = collect_downloads(
            arguments.artifacts_root, arguments.logs_root, jobs, artifacts
        )
        normalize_build_evidence(
            authority,
            registry_digest,
            pins_digest,
            descriptor_digest,
            descriptor_anchor_digest,
            export_digests,
            jobs,
            artifacts_by_job,
            logs,
            matrix_exclusions,
            arguments.output_root,
        )
        print("community-builds.json")
        return 0
    except AdmissionError as error:
        print(f"Community build evidence failed: {error}", file=sys.stderr)
        return 1
    except (OSError, ValueError):
        print("Community build evidence failed: filesystem operation failed",
              file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())

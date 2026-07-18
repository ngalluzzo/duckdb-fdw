"""Reviewed descriptor and registry construction for build evidence tests."""

from __future__ import annotations

import hashlib
import pathlib

from build_evidence_export_fixture import (
    BASE_COMMIT,
    PR_HEAD_COMMIT,
    RUN_HEAD_COMMIT,
    SOURCE_COMMIT,
    SOURCE_TREE,
)
from test_support import canonical_write


def digest(path: pathlib.Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write_descriptor(
    record: pathlib.Path, anchor: pathlib.Path, pins_digest: str
) -> None:
    value = {
        "authority": "local_provider_admission_only",
        "candidate": {
            "anchor_sha256": "1" * 64,
            "sha256": "2" * 64,
            "source": {"commit": SOURCE_COMMIT, "tree": SOURCE_TREE},
        },
        "dependency_audit": {
            "anchor_sha256": "3" * 64,
            "schema": "duckdb_api/dependency-audit/v1",
            "sha256": "4" * 64,
        },
        "descriptor_cycle_sha256": "5" * 64,
        "descriptor_expectation_sha256": "6" * 64,
        "pins_sha256": pins_digest,
        "proposal": {
            "extension": {
                "build": "cmake",
                "description": "Synthetic descriptor fixture.",
                "language": "C++",
                "license": "MIT",
                "maintainers": ["ngalluzzo"],
                "name": "duckdb_api",
                "version": "0.2.0",
            },
            "filename": "description.yml",
            "repo": {
                "github": "ngalluzzo/duckdb-fdw",
                "ref": SOURCE_COMMIT,
            },
            "sha256": "7" * 64,
        },
        "publication_status": "not_submitted",
        "schema": "duckdb_api/community-descriptor-admission/v1",
        "status": "proposal_admitted",
        "support_claims": [],
    }
    canonical_write(record, value)
    anchor.write_text(
        f"{digest(record)}  descriptor-admission.json\n", encoding="ascii"
    )


def make_authority(
    descriptor: pathlib.Path,
    pins: pathlib.Path,
    exports: dict[str, pathlib.Path],
) -> dict[str, object]:
    return {
        "artifacts_export_sha256": digest(exports["artifacts"]),
        "base": {"ref": "main", "sha": BASE_COMMIT},
        "descriptor_admission_sha256": digest(descriptor),
        "extension_source": {
            "commit": SOURCE_COMMIT,
            "repository": "ngalluzzo/duckdb-fdw",
            "tree": SOURCE_TREE,
        },
        "head": {
            "ref": "add-duckdb-api",
            "repository": "ngalluzzo/community-extensions",
            "sha": PR_HEAD_COMMIT,
        },
        "jobs_export_sha256": digest(exports["jobs"]),
        "matrix_export_sha256": digest(exports["matrix"]),
        "pins_sha256": digest(pins),
        "pull_request_export_sha256": digest(exports["pull_request"]),
        "pull_request_number": 2256,
        "repository": "duckdb/community-extensions",
        "run": {"attempt": 2, "head_sha": RUN_HEAD_COMMIT, "id": 9001},
        "run_export_sha256": digest(exports["run"]),
        "status": "maintainer_approved",
        "workflow": {"id": 77, "path": ".github/workflows/Build.yml"},
    }


def write_registry(
    path: pathlib.Path,
    authority: dict[str, object],
    exports: dict[str, pathlib.Path],
) -> str:
    authority.update(
        {
            "artifacts_export_sha256": digest(exports["artifacts"]),
            "jobs_export_sha256": digest(exports["jobs"]),
            "matrix_export_sha256": digest(exports["matrix"]),
            "pull_request_export_sha256": digest(exports["pull_request"]),
            "run_export_sha256": digest(exports["run"]),
        }
    )
    canonical_write(
        path,
        {
            "approved": [authority],
            "schema": "duckdb_api/community-build-authorities/v1",
        },
    )
    return digest(path)

"""Canonical synthetic Community PR, run, job, and artifact exports."""

from __future__ import annotations

import hashlib


SOURCE_COMMIT = "47dc6169ae820f70beb0c2722b8a8f5288cd1469"
SOURCE_TREE = "6356b5296276aff08f81a6ec3ef9da6d0a6b8f7a"
BASE_COMMIT = "b" * 40
PR_HEAD_COMMIT = "a" * 40
RUN_HEAD_COMMIT = "c" * 40
DUCKDB = {
    "commit": "08e34c447bae34eaee3723cac61f2878b6bdf787",
    "version": "1.5.4",
}


def make_exports(
    artifact_bytes: bytes, log_bytes: dict[int, bytes]
) -> dict[str, dict[str, object]]:
    """Return one complete run containing success, failure, and skip rows."""

    head = {
        "ref": "add-duckdb-api",
        "repository": "ngalluzzo/community-extensions",
        "sha": PR_HEAD_COMMIT,
    }
    base = {"ref": "main", "sha": BASE_COMMIT}
    jobs = [
        _job(
            101,
            "Build (linux_amd64)",
            "success",
            ["linux-amd64"],
            {"architecture": "amd64", "os": "linux", "toolchain": "gcc"},
            {"group": "GitHub Actions", "name": "runner-1"},
            ["ubuntu-24.04", "x64"],
            DUCKDB,
            log_bytes[101],
        ),
        _job(
            102,
            "Build (windows_amd64)",
            "failure",
            [],
            {"architecture": "amd64", "os": "windows", "toolchain": "msvc"},
            {"group": "GitHub Actions", "name": "runner-2"},
            ["windows-2025", "x64"],
            DUCKDB,
            log_bytes[102],
        ),
        _job(
            103,
            "Publish",
            "skipped",
            [],
            {},
            {"group": None, "name": None},
            [],
            None,
            log_bytes[103],
        ),
    ]
    return {
        "pull_request": {
            "base": base,
            "head": head,
            "number": 2256,
            "repository": "duckdb/community-extensions",
            "schema": "duckdb_api/community-pull-request-export/v1",
            "state": "open",
        },
        "run": {
            "attempt": 2,
            "base_ref": base["ref"],
            "base_sha": base["sha"],
            "conclusion": "failure",
            "event": "pull_request",
            "head_ref": head["ref"],
            "head_sha": RUN_HEAD_COMMIT,
            "id": 9001,
            "pull_requests": [2256],
            "repository": "duckdb/community-extensions",
            "schema": "duckdb_api/community-run-export/v1",
            "status": "completed",
            "workflow": {"id": 77, "path": ".github/workflows/Build.yml"},
        },
        "jobs": {
            "jobs": jobs,
            "repository": "duckdb/community-extensions",
            "run_attempt": 2,
            "run_id": 9001,
            "schema": "duckdb_api/community-jobs-export/v1",
            "total_count": len(jobs),
        },
        "matrix": {
            "entries": [
                {
                    "disposition": "job_expected",
                    "raw_matrix": jobs[0]["raw_matrix"],
                },
                {
                    "disposition": "job_expected",
                    "raw_matrix": jobs[1]["raw_matrix"],
                },
                {
                    "disposition": "excluded_unclaimed",
                    "raw_matrix": {
                        "architecture": "arm64",
                        "os": "macos",
                        "toolchain": "clang",
                    },
                },
            ],
            "repository": "duckdb/community-extensions",
            "schema": "duckdb_api/community-matrix-export/v1",
            "total_count": 3,
            "workflow": {"id": 77, "path": ".github/workflows/Build.yml"},
        },
        "artifacts": {
            "artifacts": [
                {
                    "expired": False,
                    "filename": "linux-amd64.zip",
                    "id": 501,
                    "job_id": 101,
                    "name": "linux-amd64",
                    "sha256": hashlib.sha256(artifact_bytes).hexdigest(),
                    "size_in_bytes": len(artifact_bytes),
                }
            ],
            "repository": "duckdb/community-extensions",
            "run_attempt": 2,
            "run_id": 9001,
            "schema": "duckdb_api/community-artifacts-export/v1",
            "total_count": 1,
        },
    }


def _job(
    job_id: int,
    name: str,
    conclusion: str,
    artifact_names: list[str],
    raw_matrix: dict[str, str],
    runner: dict[str, str | None],
    runner_labels: list[str],
    duckdb: dict[str, str] | None,
    log: bytes,
) -> dict[str, object]:
    return {
        "artifact_names": artifact_names,
        "conclusion": conclusion,
        "duckdb": duckdb,
        "id": job_id,
        "log": {
            "filename": f"job-{job_id}.log",
            "sha256": hashlib.sha256(log).hexdigest(),
            "size_in_bytes": len(log),
        },
        "matrix_entry": bool(raw_matrix),
        "name": name,
        "raw_matrix": raw_matrix,
        "runner": runner,
        "runner_labels": runner_labels,
        "status": "completed",
    }

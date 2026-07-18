"""Validate minimal canonical exports of Community pull-request build state."""

from __future__ import annotations

import json
import re
from typing import Any

from build_evidence_authority import COMMUNITY_REPOSITORY
from record_format import require


PR_SCHEMA = "duckdb_api/community-pull-request-export/v1"
RUN_SCHEMA = "duckdb_api/community-run-export/v1"
JOBS_SCHEMA = "duckdb_api/community-jobs-export/v1"
ARTIFACTS_SCHEMA = "duckdb_api/community-artifacts-export/v1"
MATRIX_SCHEMA = "duckdb_api/community-matrix-export/v1"
CONCLUSIONS = {
    "action_required",
    "cancelled",
    "failure",
    "neutral",
    "skipped",
    "stale",
    "startup_failure",
    "success",
    "timed_out",
}
SAFE_FILENAME = re.compile(r"[A-Za-z0-9][A-Za-z0-9._-]{0,254}")
SAFE_TEXT = re.compile(r"[^\x00-\x1f\x7f]{1,512}")
SHA256 = re.compile(r"[0-9a-f]{64}")


def _mapping(value: object, label: str) -> dict[str, Any]:
    require(isinstance(value, dict), f"{label} must be an object")
    return value


def _fields(value: object, expected: set[str], label: str) -> dict[str, Any]:
    result = _mapping(value, label)
    require(set(result) == expected, f"{label} fields drifted")
    return result


def _integer(value: object, label: str, *, positive: bool = True) -> int:
    require(isinstance(value, int) and not isinstance(value, bool),
            f"{label} must be an integer")
    require(not positive or value > 0, f"{label} must be positive")
    require(positive or value >= 0, f"{label} must not be negative")
    return value


def _text(value: object, label: str) -> str:
    require(isinstance(value, str) and SAFE_TEXT.fullmatch(value) is not None,
            f"{label} is invalid")
    return value


def _filename(value: object, label: str) -> str:
    require(isinstance(value, str) and SAFE_FILENAME.fullmatch(value) is not None,
            f"{label} is not one safe filename")
    return value


def _validate_identity(actual: object, expected: dict[str, Any], label: str) -> None:
    require(actual == expected, f"{label} does not match reviewed authority")


def validate_pull_request_export(
    value: dict[str, Any], authority: dict[str, Any]
) -> dict[str, Any]:
    root = _fields(
        value, {"base", "head", "number", "repository", "schema", "state"},
        "pull request export",
    )
    require(root["schema"] == PR_SCHEMA, "pull request export schema is unsupported")
    require(root["repository"] == COMMUNITY_REPOSITORY,
            "pull request export repository drifted")
    require(root["state"] in {"open", "closed"},
            "pull request export state is invalid")
    _validate_identity(root["number"], authority["pull_request_number"],
                       "pull request number")
    _validate_identity(root["head"], authority["head"], "pull request head")
    _validate_identity(root["base"], authority["base"], "pull request base")
    return root


def validate_run_export(
    value: dict[str, Any], authority: dict[str, Any]
) -> dict[str, Any]:
    root = _fields(
        value,
        {
            "attempt",
            "base_ref",
            "base_sha",
            "conclusion",
            "event",
            "head_ref",
            "head_sha",
            "id",
            "pull_requests",
            "repository",
            "schema",
            "status",
            "workflow",
        },
        "workflow run export",
    )
    require(root["schema"] == RUN_SCHEMA, "workflow run export schema is unsupported")
    require(root["repository"] == COMMUNITY_REPOSITORY,
            "workflow run export repository drifted")
    require(root["event"] == "pull_request", "workflow run event is not pull_request")
    require(root["status"] == "completed" and root["conclusion"] in CONCLUSIONS,
            "workflow run is not complete")
    _validate_identity(
        {"id": root["id"], "attempt": root["attempt"]},
        {"id": authority["run"]["id"], "attempt": authority["run"]["attempt"]},
        "workflow run identity",
    )
    _validate_identity(root["workflow"], authority["workflow"], "workflow identity")
    _validate_identity(root["head_sha"], authority["run"]["head_sha"],
                       "workflow run head commit")
    _validate_identity(root["head_ref"], authority["head"]["ref"],
                       "workflow run head ref")
    _validate_identity(root["base_sha"], authority["base"]["sha"],
                       "workflow run base commit")
    _validate_identity(root["base_ref"], authority["base"]["ref"],
                       "workflow run base ref")
    require(root["pull_requests"] == [authority["pull_request_number"]],
            "workflow run pull request linkage drifted")
    return root


def _validate_matrix(value: object, label: str) -> dict[str, object]:
    matrix = _mapping(value, label)
    require(len(matrix) <= 32, f"{label} is too large")
    for name, item in matrix.items():
        _text(name, f"{label} key")
        require(
            item is None or isinstance(item, (str, int, bool)),
            f"{label} values must be raw JSON scalars",
        )
        if isinstance(item, str):
            _text(item, f"{label} value")
    return matrix


def _matrix_key(matrix: dict[str, object]) -> str:
    return json.dumps(matrix, sort_keys=True, separators=(",", ":"))


def validate_jobs_export(
    value: dict[str, Any], authority: dict[str, Any], duckdb: dict[str, str]
) -> list[dict[str, Any]]:
    root = _fields(
        value,
        {"jobs", "repository", "run_attempt", "run_id", "schema", "total_count"},
        "jobs export",
    )
    require(root["schema"] == JOBS_SCHEMA, "jobs export schema is unsupported")
    require(root["repository"] == COMMUNITY_REPOSITORY,
            "jobs export repository drifted")
    _validate_identity(
        {"id": root["run_id"], "attempt": root["run_attempt"]},
        {"id": authority["run"]["id"], "attempt": authority["run"]["attempt"]},
        "jobs export run identity",
    )
    jobs = root["jobs"]
    require(isinstance(jobs, list), "jobs export jobs must be a list")
    require(jobs, "jobs export must contain the complete nonempty inventory")
    _integer(root["total_count"], "jobs export total count", positive=False)
    require(root["total_count"] == len(jobs), "jobs export is incomplete")
    result: list[dict[str, Any]] = []
    ids: set[int] = set()
    log_names: set[str] = set()
    artifact_names: set[str] = set()
    artifact_matrices: set[str] = set()
    for index, item in enumerate(jobs):
        label = f"job {index}"
        job = _fields(
            item,
            {
                "artifact_names",
                "conclusion",
                "duckdb",
                "id",
                "log",
                "matrix_entry",
                "name",
                "raw_matrix",
                "runner",
                "runner_labels",
                "status",
            },
            label,
        )
        job_id = _integer(job["id"], f"{label} id")
        require(job_id not in ids, "jobs export contains duplicate job ids")
        ids.add(job_id)
        _text(job["name"], f"{label} name")
        require(job["status"] == "completed" and job["conclusion"] in CONCLUSIONS,
                f"{label} is not complete")
        log = _fields(job["log"], {"filename", "sha256", "size_in_bytes"},
                      f"{label} log")
        log_name = _filename(log["filename"], f"{label} log filename")
        require(log_name not in log_names, "jobs export contains duplicate log names")
        log_names.add(log_name)
        _integer(log["size_in_bytes"], f"{label} log size", positive=False)
        require(isinstance(log["sha256"], str)
                and SHA256.fullmatch(log["sha256"]) is not None,
                f"{label} log digest must be lowercase SHA-256")
        runner = _fields(job["runner"], {"group", "name"}, f"{label} runner")
        for name in ("group", "name"):
            require(runner[name] is None or isinstance(runner[name], str),
                    f"{label} runner {name} is invalid")
            if isinstance(runner[name], str):
                _text(runner[name], f"{label} runner {name}")
        labels = job["runner_labels"]
        require(isinstance(labels, list), f"{label} runner labels must be a list")
        for runner_label in labels:
            _text(runner_label, f"{label} runner label")
        require(len(labels) == len(set(labels)), f"{label} runner labels are duplicated")
        matrix = _validate_matrix(job["raw_matrix"], f"{label} raw matrix")
        require(job["matrix_entry"] is True or job["matrix_entry"] is False,
                f"{label} matrix-entry flag is invalid")
        require(job["matrix_entry"] or matrix == {},
                f"{label} has unaccounted raw matrix labels")
        names = job["artifact_names"]
        require(isinstance(names, list), f"{label} artifact names must be a list")
        for name in names:
            _filename(name, f"{label} artifact name")
            require(name not in artifact_names,
                    "jobs export contains duplicate artifact names")
            artifact_names.add(name)
        if job["duckdb"] is not None:
            _validate_identity(job["duckdb"], duckdb, f"{label} DuckDB identity")
        require(not names or job["duckdb"] == duckdb,
                f"{label} artifact output lacks exact DuckDB identity")
        require(not names or job["matrix_entry"] is True,
                f"{label} artifact output is not an expected matrix row")
        if names:
            matrix_key = _matrix_key(matrix)
            require(matrix_key not in artifact_matrices,
                    "artifact-producing jobs have colliding raw matrix labels")
            artifact_matrices.add(matrix_key)
        result.append(job)
    return sorted(result, key=lambda job: job["id"])


def validate_matrix_export(
    value: dict[str, Any], authority: dict[str, Any], jobs: list[dict[str, Any]]
) -> list[dict[str, object]]:
    """Bind expected job rows and exclusions that create no GitHub job."""

    root = _fields(
        value,
        {"entries", "repository", "schema", "total_count", "workflow"},
        "matrix export",
    )
    require(root["schema"] == MATRIX_SCHEMA, "matrix export schema is unsupported")
    require(root["repository"] == COMMUNITY_REPOSITORY,
            "matrix export repository drifted")
    _validate_identity(root["workflow"], authority["workflow"],
                       "matrix workflow identity")
    entries = root["entries"]
    require(isinstance(entries, list), "matrix export entries must be a list")
    require(entries, "matrix export must contain a nonempty reviewed inventory")
    _integer(root["total_count"], "matrix export total count", positive=False)
    require(root["total_count"] == len(entries), "matrix export is incomplete")
    expected_job_keys: list[str] = []
    excluded: list[dict[str, object]] = []
    observed: set[str] = set()
    for index, item in enumerate(entries):
        label = f"matrix entry {index}"
        entry = _fields(item, {"disposition", "raw_matrix"}, label)
        require(entry["disposition"] in {"job_expected", "excluded_unclaimed"},
                f"{label} disposition is invalid")
        matrix = _validate_matrix(entry["raw_matrix"], f"{label} raw matrix")
        require(matrix, f"{label} raw matrix must not be empty")
        key = _matrix_key(matrix)
        require(key not in observed, "matrix export contains duplicate combinations")
        observed.add(key)
        if entry["disposition"] == "job_expected":
            expected_job_keys.append(key)
        else:
            excluded.append(matrix)
    job_keys = [
        _matrix_key(job["raw_matrix"]) for job in jobs if job["matrix_entry"] is True
    ]
    require(
        sorted(job_keys) == sorted(expected_job_keys),
        "matrix export has missing, duplicate, or unaccounted job rows",
    )
    return sorted(excluded, key=_matrix_key)


def validate_artifacts_export(
    value: dict[str, Any], authority: dict[str, Any], jobs: list[dict[str, Any]]
) -> list[dict[str, Any]]:
    root = _fields(
        value,
        {"artifacts", "repository", "run_attempt", "run_id", "schema", "total_count"},
        "artifacts export",
    )
    require(root["schema"] == ARTIFACTS_SCHEMA,
            "artifacts export schema is unsupported")
    require(root["repository"] == COMMUNITY_REPOSITORY,
            "artifacts export repository drifted")
    _validate_identity(
        {"id": root["run_id"], "attempt": root["run_attempt"]},
        {"id": authority["run"]["id"], "attempt": authority["run"]["attempt"]},
        "artifacts export run identity",
    )
    artifacts = root["artifacts"]
    require(isinstance(artifacts, list), "artifacts export artifacts must be a list")
    _integer(root["total_count"], "artifacts export total count", positive=False)
    require(root["total_count"] == len(artifacts), "artifacts export is incomplete")
    jobs_by_id = {job["id"]: job for job in jobs}
    expected = {
        name: job["id"] for job in jobs for name in job["artifact_names"]
    }
    observed: dict[str, int] = {}
    ids: set[int] = set()
    filenames: set[str] = set()
    result: list[dict[str, Any]] = []
    for index, item in enumerate(artifacts):
        label = f"artifact {index}"
        artifact = _fields(
            item,
            {"expired", "filename", "id", "job_id", "name", "sha256", "size_in_bytes"},
            label,
        )
        artifact_id = _integer(artifact["id"], f"{label} id")
        require(artifact_id not in ids, "artifacts export contains duplicate ids")
        ids.add(artifact_id)
        job_id = _integer(artifact["job_id"], f"{label} job id")
        require(job_id in jobs_by_id, f"{label} names an unknown job")
        name = _filename(artifact["name"], f"{label} name")
        require(name not in observed, "artifacts export contains duplicate names")
        observed[name] = job_id
        filename = _filename(artifact["filename"], f"{label} filename")
        require(filename not in filenames,
                "artifacts export contains duplicate filenames")
        filenames.add(filename)
        _integer(artifact["size_in_bytes"], f"{label} size", positive=False)
        require(isinstance(artifact["sha256"], str)
                and SHA256.fullmatch(artifact["sha256"]) is not None,
                f"{label} digest must be lowercase SHA-256")
        require(artifact["expired"] is False, f"{label} is expired")
        result.append(artifact)
    require(observed == expected, "artifact inventory does not match all job outputs")
    return sorted(result, key=lambda artifact: artifact["id"])

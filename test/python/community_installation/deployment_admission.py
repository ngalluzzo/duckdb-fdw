"""Admit one exact anchored Community native-deployment handoff.

Engineering Enablement owns build, signing-transition, deployment-run, and
download custody. Query validates the versioned interface and binds its stock
host result to the admitted record; it does not reproduce provider internals.
"""

from __future__ import annotations

import hashlib
import json
import pathlib
import re
from typing import Any
from urllib.parse import urlsplit

try:
    from .input_admission import (
        AdmissionError,
        Candidate,
        DuckDbIdentity,
        GIT_ID,
        SHA256,
        read_regular_bytes,
    )
    from .matrix import DeploymentEvidence, RowIdentity, is_community_platform
except ImportError:
    from input_admission import (
        AdmissionError,
        Candidate,
        DuckDbIdentity,
        GIT_ID,
        SHA256,
        read_regular_bytes,
    )
    from matrix import DeploymentEvidence, RowIdentity, is_community_platform


DEPLOYMENT_SCHEMA = "duckdb_api/community-deployment/v1"
MAX_DEPLOYMENT_BYTES = 64 * 1024
ANCHOR = re.compile(r"([0-9a-f]{64})  deployment\.json\n")


def _reject_duplicate_keys(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise AdmissionError(f"deployment contains duplicate key {key!r}")
        result[key] = value
    return result


def _object(value: Any, label: str, keys: set[str]) -> dict[str, Any]:
    if not isinstance(value, dict) or set(value) != keys:
        raise AdmissionError(f"{label} fields differ from the deployment v1 contract")
    return value


def _string(value: Any, label: str) -> str:
    if not isinstance(value, str) or not value:
        raise AdmissionError(f"{label} is not a non-empty string")
    return value


def _fixed(value: Any, label: str, expected: str) -> str:
    actual = _string(value, label)
    if actual != expected:
        raise AdmissionError(f"{label} must be {expected!r}")
    return actual


def _matching(value: Any, label: str, pattern: re.Pattern[str]) -> str:
    actual = _string(value, label)
    if pattern.fullmatch(actual) is None:
        raise AdmissionError(f"{label} has an invalid identity")
    return actual


def _positive_integer(value: Any, label: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or value <= 0:
        raise AdmissionError(f"{label} is not a positive integer")
    return value


def _endpoint(value: Any, candidate: Candidate, platform: str) -> str:
    endpoint = _string(value, "deployment.community.endpoint")
    parsed = urlsplit(endpoint)
    expected_path = (
        f"/v{candidate.duckdb.version}/{platform}/"
        "duckdb_api.duckdb_extension.gz"
    )
    if (
        parsed.scheme != "http"
        or parsed.hostname != "community-extensions.duckdb.org"
        or parsed.netloc != "community-extensions.duckdb.org"
        or parsed.username is not None
        or parsed.password is not None
        or parsed.query
        or parsed.fragment
        or parsed.path != expected_path
    ):
        raise AdmissionError("deployment Community endpoint is not canonical")
    return endpoint


def admit_deployment(
    record_path: pathlib.Path,
    anchor_path: pathlib.Path,
    candidate: Candidate,
) -> DeploymentEvidence:
    """Return Query's normalized view of one anchored native deployment record."""

    record_path = pathlib.Path(record_path)
    anchor_path = pathlib.Path(anchor_path)
    if record_path.name != "deployment.json":
        raise AdmissionError("deployment input must be named deployment.json")
    if anchor_path.name != "deployment.sha256":
        raise AdmissionError("deployment anchor must be named deployment.sha256")
    raw = read_regular_bytes(
        record_path, "deployment.json", MAX_DEPLOYMENT_BYTES
    )
    anchor_raw = read_regular_bytes(anchor_path, "deployment.sha256", 256)
    try:
        anchor_text = anchor_raw.decode("ascii")
    except UnicodeDecodeError as error:
        raise AdmissionError("deployment.sha256 is not ASCII") from error
    match = ANCHOR.fullmatch(anchor_text)
    if match is None:
        raise AdmissionError("deployment.sha256 has invalid syntax")
    record_sha256 = hashlib.sha256(raw).hexdigest()
    if match.group(1) != record_sha256:
        raise AdmissionError("deployment.json does not match deployment.sha256")
    try:
        document = json.loads(
            raw.decode("utf-8"), object_pairs_hook=_reject_duplicate_keys
        )
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise AdmissionError("deployment.json is not valid UTF-8 JSON") from error
    canonical = (json.dumps(document, indent=2, sort_keys=True) + "\n").encode()
    if canonical != raw:
        raise AdmissionError("deployment.json is not canonical JSON")

    root = _object(
        document,
        "deployment",
        {
            "schema",
            "status",
            "candidate_sha256",
            "channel",
            "row",
            "build",
            "community",
            "deployment",
        },
    )
    _fixed(root["schema"], "deployment.schema", DEPLOYMENT_SCHEMA)
    _fixed(root["status"], "deployment.status", "deployed_candidate")
    _fixed(root["channel"], "deployment.channel", "community")
    _fixed(
        root["candidate_sha256"],
        "deployment.candidate_sha256",
        candidate.sha256,
    )

    row = _object(root["row"], "deployment.row", {"duckdb", "platform"})
    duckdb = _object(
        row["duckdb"], "deployment.row.duckdb", {"version", "commit"}
    )
    identity = DuckDbIdentity(
        _fixed(
            duckdb["version"],
            "deployment.row.duckdb.version",
            candidate.duckdb.version,
        ),
        _fixed(
            duckdb["commit"],
            "deployment.row.duckdb.commit",
            candidate.duckdb.commit,
        ),
    )
    platform = _string(row["platform"], "deployment.row.platform")
    if not is_community_platform(platform):
        raise AdmissionError("deployment row has an invalid Community platform")
    if platform.startswith("wasm"):
        raise AdmissionError("deployment v1 admits native Community rows only")

    build = _object(
        root["build"],
        "deployment.build",
        {
            "archive_sha256",
            "unsigned_artifact_sha256",
            "shared_payload_sha256",
        },
    )
    archive_sha256 = _matching(
        build["archive_sha256"], "deployment.build.archive_sha256", SHA256
    )
    unsigned_sha256 = _matching(
        build["unsigned_artifact_sha256"],
        "deployment.build.unsigned_artifact_sha256",
        SHA256,
    )
    shared_payload_sha256 = _matching(
        build["shared_payload_sha256"],
        "deployment.build.shared_payload_sha256",
        SHA256,
    )

    community = _object(
        root["community"],
        "deployment.community",
        {
            "repository",
            "workflow",
            "run_id",
            "run_attempt",
            "head_commit",
            "endpoint",
        },
    )
    _fixed(
        community["repository"],
        "deployment.community.repository",
        candidate.community_repository,
    )
    _fixed(
        community["workflow"],
        "deployment.community.workflow",
        ".github/workflows/build.yml",
    )
    _positive_integer(community["run_id"], "deployment.community.run_id")
    _positive_integer(
        community["run_attempt"], "deployment.community.run_attempt"
    )
    _matching(
        community["head_commit"],
        "deployment.community.head_commit",
        GIT_ID,
    )
    _endpoint(community["endpoint"], candidate, platform)

    deployed = _object(
        root["deployment"],
        "deployment.deployment",
        {"served_gzip_sha256", "signed_artifact_sha256", "artifact_size"},
    )
    served_gzip_sha256 = _matching(
        deployed["served_gzip_sha256"],
        "deployment.deployment.served_gzip_sha256",
        SHA256,
    )
    signed_sha256 = _matching(
        deployed["signed_artifact_sha256"],
        "deployment.deployment.signed_artifact_sha256",
        SHA256,
    )
    if unsigned_sha256 == signed_sha256:
        raise AdmissionError("unsigned and signed deployment identities are equal")
    if _positive_integer(
        deployed["artifact_size"], "deployment.deployment.artifact_size"
    ) <= 256:
        raise AdmissionError("deployed artifact is smaller than its signature block")

    return DeploymentEvidence(
        candidate_sha256=candidate.sha256,
        row=RowIdentity(identity, platform),
        status="passed",
        channel="community",
        build_archive_sha256=archive_sha256,
        unsigned_artifact_sha256=unsigned_sha256,
        shared_payload_sha256=shared_payload_sha256,
        served_gzip_sha256=served_gzip_sha256,
        deployed_artifact_sha256=signed_sha256,
        deployment_record_sha256=record_sha256,
        deployment_anchor_sha256=hashlib.sha256(anchor_raw).hexdigest(),
    )

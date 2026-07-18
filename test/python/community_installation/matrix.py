"""Pure support-row eligibility law for Community installation evidence.

The dataclasses below are Query-internal normalized observations, not provider
JSON contracts. A later adapter may construct them only after Enablement binds
one Community build payload to the signed artifact served after deployment.
"""

from __future__ import annotations

from dataclasses import dataclass
import re
from typing import Iterable

try:
    from .input_admission import Candidate, DuckDbIdentity
except ImportError:
    from input_admission import Candidate, DuckDbIdentity


SHA256 = re.compile(r"[0-9a-f]{64}")
PLATFORM = re.compile(r"[a-z0-9]+(?:_[a-z0-9]+)+")


class MatrixError(ValueError):
    """Evidence cannot produce an exact, non-ambiguous support row set."""


@dataclass(frozen=True, order=True)
class RowIdentity:
    duckdb: DuckDbIdentity
    platform: str


@dataclass(frozen=True)
class DeploymentEvidence:
    """One Community-built, signed, deployed artifact candidate for Query."""

    candidate_sha256: str
    row: RowIdentity
    status: str
    channel: str
    build_archive_sha256: str | None
    unsigned_artifact_sha256: str | None
    shared_payload_sha256: str | None
    served_gzip_sha256: str | None
    deployed_artifact_sha256: str | None
    deployment_record_sha256: str | None
    deployment_anchor_sha256: str | None


@dataclass(frozen=True)
class QueryEvidence:
    candidate_sha256: str
    row: RowIdentity
    status: str
    channel: str
    artifact_sha256: str
    deployment_record_sha256: str
    deployment_anchor_sha256: str
    default_signature_enforced: bool
    extension_version: str
    public_contract_sha256: str


def _digest(value: str | None, label: str) -> str:
    if value is None or SHA256.fullmatch(value) is None:
        raise MatrixError(f"{label} is not a lowercase SHA-256 identity")
    return value


def is_community_platform(value: str) -> bool:
    """Return whether a value has the canonical underscore-separated row shape."""

    return PLATFORM.fullmatch(value) is not None


def _row(candidate: Candidate, row: RowIdentity, label: str) -> None:
    if row.duckdb != candidate.duckdb:
        raise MatrixError(f"{label} does not target the candidate DuckDB identity")
    if not is_community_platform(row.platform):
        raise MatrixError(f"{label} has an invalid Community platform identity")


def claimable_rows(
    candidate: Candidate,
    deployments: Iterable[DeploymentEvidence],
    query_results: Iterable[QueryEvidence],
    public_contract_sha256: str,
) -> tuple[RowIdentity, ...]:
    """Return exactly the complete deployed-artifact/Query intersection."""

    expected_contract = _digest(public_contract_sha256, "public contract")
    deployment_by_row: dict[RowIdentity, DeploymentEvidence] = {}
    for deployment in deployments:
        _row(candidate, deployment.row, "deployment row")
        if deployment.row in deployment_by_row:
            raise MatrixError("Community deployment evidence contains a duplicate row")
        if deployment.candidate_sha256 != candidate.sha256:
            raise MatrixError("deployment row names a different source candidate")
        if deployment.channel != "community":
            raise MatrixError("deployment row is not from the Community channel")
        if deployment.status not in {"passed", "failed", "excluded"}:
            raise MatrixError("deployment row has an unknown status")
        if deployment.status == "passed":
            if deployment.row.platform.startswith("wasm"):
                raise MatrixError("native support admission cannot claim a Wasm row")
            for value, label in (
                (deployment.build_archive_sha256, "Community build archive"),
                (deployment.unsigned_artifact_sha256, "unsigned Community artifact"),
                (deployment.shared_payload_sha256, "Community signing payload"),
                (deployment.served_gzip_sha256, "served Community gzip"),
                (deployment.deployed_artifact_sha256, "deployed Community artifact"),
                (deployment.deployment_record_sha256, "Community deployment record"),
                (deployment.deployment_anchor_sha256, "Community deployment anchor"),
            ):
                _digest(value, label)
            if (
                deployment.unsigned_artifact_sha256
                == deployment.deployed_artifact_sha256
            ):
                raise MatrixError(
                    "unsigned and deployed Community artifacts have the same identity"
                )
        elif any(
            value is not None
            for value in (
                deployment.build_archive_sha256,
                deployment.unsigned_artifact_sha256,
                deployment.shared_payload_sha256,
                deployment.served_gzip_sha256,
                deployment.deployed_artifact_sha256,
                deployment.deployment_record_sha256,
                deployment.deployment_anchor_sha256,
            )
        ):
            raise MatrixError(
                "a non-passing deployment row claims artifact custody"
            )
        deployment_by_row[deployment.row] = deployment

    query_by_row: dict[RowIdentity, QueryEvidence] = {}
    for result in query_results:
        _row(candidate, result.row, "Query row")
        if result.row in query_by_row:
            raise MatrixError("Query evidence contains a duplicate row")
        if result.candidate_sha256 != candidate.sha256:
            raise MatrixError("Query row names a different source candidate")
        if result.channel != "community":
            raise MatrixError("Query row did not use the Community channel")
        if result.status not in {"passed", "failed"}:
            raise MatrixError("Query row has an unknown status")
        if result.default_signature_enforced is not True:
            raise MatrixError("Query row weakened default signature enforcement")
        if result.extension_version != "0.2.0":
            raise MatrixError("Query row observed the wrong extension version")
        if result.public_contract_sha256 != expected_contract:
            raise MatrixError("Query row used a different public contract")
        _digest(result.artifact_sha256, "Query artifact")
        _digest(result.deployment_record_sha256, "Query deployment record")
        _digest(result.deployment_anchor_sha256, "Query deployment anchor")
        deployment = deployment_by_row.get(result.row)
        if deployment is None:
            raise MatrixError("Query row has no Community deployment evidence")
        if deployment.status != "passed":
            raise MatrixError(
                "Query row exists for a non-passing Community deployment"
            )
        if result.artifact_sha256 != deployment.deployed_artifact_sha256:
            raise MatrixError("Query row exercised different artifact bytes")
        if (
            result.deployment_record_sha256
            != deployment.deployment_record_sha256
            or result.deployment_anchor_sha256
            != deployment.deployment_anchor_sha256
        ):
            raise MatrixError("Query row used different deployment custody")
        query_by_row[result.row] = result

    claimed: list[RowIdentity] = []
    for row, deployment in deployment_by_row.items():
        if deployment.status != "passed":
            continue
        result = query_by_row.get(row)
        if result is None:
            raise MatrixError("passing Community deployment lacks Query evidence")
        if result.status == "passed":
            claimed.append(row)
    return tuple(sorted(claimed))

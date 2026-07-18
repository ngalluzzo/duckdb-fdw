"""Pure support-row eligibility law for Community installation evidence.

The dataclasses below are Query-internal normalized observations, not provider
JSON contracts. A later adapter may construct them only after Enablement
publishes and verifies the corresponding build/custody schema.
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
class BuildEvidence:
    candidate_sha256: str
    row: RowIdentity
    status: str
    channel: str
    artifact_sha256: str | None
    custody_sha256: str | None


@dataclass(frozen=True)
class QueryEvidence:
    candidate_sha256: str
    row: RowIdentity
    status: str
    channel: str
    artifact_sha256: str
    default_signature_enforced: bool
    extension_version: str
    public_contract_sha256: str


def _digest(value: str | None, label: str) -> str:
    if value is None or SHA256.fullmatch(value) is None:
        raise MatrixError(f"{label} is not a lowercase SHA-256 identity")
    return value


def _row(candidate: Candidate, row: RowIdentity, label: str) -> None:
    if row.duckdb != candidate.duckdb:
        raise MatrixError(f"{label} does not target the candidate DuckDB identity")
    if PLATFORM.fullmatch(row.platform) is None:
        raise MatrixError(f"{label} has an invalid Community platform identity")


def claimable_rows(
    candidate: Candidate,
    builds: Iterable[BuildEvidence],
    query_results: Iterable[QueryEvidence],
    public_contract_sha256: str,
) -> tuple[RowIdentity, ...]:
    """Return exactly the complete passing build/custody/query intersection."""

    expected_contract = _digest(public_contract_sha256, "public contract")
    build_by_row: dict[RowIdentity, BuildEvidence] = {}
    for build in builds:
        _row(candidate, build.row, "build row")
        if build.row in build_by_row:
            raise MatrixError("Community build evidence contains a duplicate row")
        if build.candidate_sha256 != candidate.sha256:
            raise MatrixError("build row names a different source candidate")
        if build.channel != "community":
            raise MatrixError("build row is not from the Community channel")
        if build.status not in {"passed", "failed", "excluded"}:
            raise MatrixError("build row has an unknown status")
        if build.status == "passed":
            _digest(build.artifact_sha256, "build artifact")
            _digest(build.custody_sha256, "build custody")
        elif build.artifact_sha256 is not None or build.custody_sha256 is not None:
            raise MatrixError("a non-passing build row claims artifact custody")
        build_by_row[build.row] = build

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
        build = build_by_row.get(result.row)
        if build is None:
            raise MatrixError("Query row has no Community build evidence")
        if build.status != "passed":
            raise MatrixError("Query row exists for a non-passing Community build")
        if result.artifact_sha256 != build.artifact_sha256:
            raise MatrixError("Query row exercised different artifact bytes")
        query_by_row[result.row] = result

    claimed: list[RowIdentity] = []
    for row, build in build_by_row.items():
        if build.status != "passed":
            continue
        result = query_by_row.get(row)
        if result is None:
            raise MatrixError("passing Community build lacks Query evidence")
        if result.status == "passed":
            claimed.append(row)
    return tuple(sorted(claimed))

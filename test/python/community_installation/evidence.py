"""Build canonical, bounded Query evidence after lifecycle assertions finish.

Evidence formatting has no process or support authority.  It receives admitted
identities and completed observations, normalizes only known machine-local
roots, and writes one caller-selected new file.  Path replacement occurs after
real-path containment and byte assertions, so redaction cannot make an unsafe
host result pass.
"""

from __future__ import annotations

import json
import os
import pathlib
import re
from collections.abc import Sequence
from typing import Any

try:
    from .input_admission import Candidate
    from .lifecycle import ExtensionObservation, HostObservation
    from .matrix import BuildEvidence, QueryEvidence, RowIdentity
    from .scenarios import ScenarioResult
except ImportError:
    from input_admission import Candidate
    from lifecycle import ExtensionObservation, HostObservation
    from matrix import BuildEvidence, QueryEvidence, RowIdentity
    from scenarios import ScenarioResult


QUERY_EVIDENCE_SCHEMA = "duckdb_api/community-query-evidence/v1"
MAX_EVIDENCE_BYTES = 256 * 1024
SHA256 = re.compile(r"[0-9a-f]{64}")


class EvidenceError(ValueError):
    """Completed observations cannot be represented by the Query v1 schema."""


def _row(row: RowIdentity) -> dict[str, object]:
    return {
        "duckdb": {
            "commit": row.duckdb.commit,
            "version": row.duckdb.version,
        },
        "platform": row.platform,
    }


def _extension(value: ExtensionObservation | None) -> dict[str, object] | None:
    if value is None:
        return None
    return {
        "artifact_sha256": value.artifact_sha256,
        "install_path": value.install_path,
        "install_source": value.install_source,
        "installed": value.installed,
        "loaded": value.loaded,
        "name": value.name,
        "version": value.version,
    }


def _observation(value: HostObservation) -> dict[str, object]:
    return {
        "action": value.action,
        "allow_unsigned_extensions": value.allow_unsigned_extensions,
        "behavior": value.behavior,
        "diagnostic": value.diagnostic,
        "diagnostic_category": value.diagnostic_category,
        "extension": _extension(value.extension),
        "function_registered": value.function_registered,
        "ok": value.ok,
        "process_token": value.process_token,
        "row": _row(value.row),
    }


def _path_forms(path: pathlib.Path) -> set[str]:
    forms = {str(path.expanduser().absolute())}
    try:
        forms.add(str(path.resolve(strict=True)))
    except OSError:
        pass
    return {value.rstrip(os.sep) for value in forms if value.rstrip(os.sep)}


def normalize_paths(
    value: object, replacements: Sequence[tuple[pathlib.Path, str]]
) -> object:
    """Recursively replace canonical and lexical roots, longest first."""

    normalized: list[tuple[str, str]] = []
    for root, placeholder in replacements:
        normalized.extend((form, placeholder) for form in _path_forms(root))
    ordered = sorted(set(normalized), key=lambda item: len(item[0]), reverse=True)
    if isinstance(value, str):
        for source, replacement in ordered:
            value = value.replace(source, replacement)
        return value
    if isinstance(value, list):
        return [normalize_paths(item, replacements) for item in value]
    if isinstance(value, tuple):
        return [normalize_paths(item, replacements) for item in value]
    if isinstance(value, dict):
        return {
            key: normalize_paths(item, replacements)
            for key, item in value.items()
        }
    return value


def build_passed_evidence(
    *,
    candidate: Candidate,
    build: BuildEvidence,
    scenarios: ScenarioResult,
    incompatible_artifact_size: int,
    incompatible_artifact_sha256: str,
    supported_launcher_sha256: str,
    incompatible_launcher_sha256: str,
    supported_host_inventory_sha256: str,
    incompatible_host_inventory_sha256: str,
    public_contract_sha256: str,
    replacements: Sequence[tuple[pathlib.Path, str]],
) -> dict[str, object]:
    """Bind one passing stock-host lifecycle to its candidate and build row."""

    if (
        build.artifact_sha256 is None
        or build.custody_sha256 is None
        or incompatible_artifact_size < 0
    ):
        raise EvidenceError("passing Query evidence requires artifact custody")
    if any(
        SHA256.fullmatch(value) is None
        for value in (
            supported_launcher_sha256,
            incompatible_launcher_sha256,
            supported_host_inventory_sha256,
            incompatible_host_inventory_sha256,
            scenarios.initialization_probe_sha256,
        )
    ):
        raise EvidenceError("passing Query host authority is malformed")
    result: dict[str, object] = {
        "artifact_sha256": build.artifact_sha256,
        "candidate": {
            "sha256": candidate.sha256,
            "source_commit": candidate.source_commit,
            "source_tag": candidate.source_tag,
            "source_tree": candidate.source_tree,
        },
        "channel": "community",
        "community": {
            "build_custody_sha256": build.custody_sha256,
            "repository": candidate.community_repository,
            "source_commit": candidate.community_commit,
        },
        "default_signature_enforced": True,
        "extension_version": "0.2.0",
        "incompatible": _observation(scenarios.incompatible),
        "incompatible_artifact_size": incompatible_artifact_size,
        "incompatible_artifact_sha256": incompatible_artifact_sha256,
        "initialization_probe_sha256": scenarios.initialization_probe_sha256,
        "launcher_sha256": {
            "incompatible": incompatible_launcher_sha256,
            "supported": supported_launcher_sha256,
        },
        "stock_host_inventory_sha256": {
            "incompatible": incompatible_host_inventory_sha256,
            "supported": supported_host_inventory_sha256,
        },
        "public_contract_sha256": public_contract_sha256,
        "row": _row(build.row),
        "schema": QUERY_EVIDENCE_SCHEMA,
        "status": "passed",
        "supported": [
            _observation(observation)
            for observation in scenarios.supported.observations
        ],
    }
    retained = normalize_paths(result, replacements)
    if not isinstance(retained, dict):
        raise EvidenceError("Query evidence is not an object")
    encoded = canonical_bytes(retained)
    if len(encoded) > MAX_EVIDENCE_BYTES:
        raise EvidenceError("Query evidence exceeds its byte bound")
    return retained


def build_failed_evidence(
    *,
    candidate: Candidate,
    build: BuildEvidence,
    public_contract_sha256: str,
    category: str,
    diagnostic: str,
    incompatible_artifact_size: int,
    incompatible_artifact_sha256: str,
    initialization_probe_sha256: str,
    supported_launcher_sha256: str,
    incompatible_launcher_sha256: str,
    supported_host_inventory_sha256: str,
    incompatible_host_inventory_sha256: str,
    replacements: Sequence[tuple[pathlib.Path, str]],
) -> dict[str, object]:
    """Record a non-claimable bounded failure without inventing observations."""

    if incompatible_artifact_size < 0 or any(
        SHA256.fullmatch(value) is None
        for value in (
            incompatible_artifact_sha256,
            initialization_probe_sha256,
            supported_launcher_sha256,
            incompatible_launcher_sha256,
            supported_host_inventory_sha256,
            incompatible_host_inventory_sha256,
        )
    ):
        raise EvidenceError("failed Query host authority is malformed")
    result = {
        "artifact_sha256": build.artifact_sha256,
        "candidate": {
            "sha256": candidate.sha256,
            "source_commit": candidate.source_commit,
            "source_tag": candidate.source_tag,
            "source_tree": candidate.source_tree,
        },
        "channel": "community",
        "failure": {"category": category, "diagnostic": diagnostic[:4096]},
        "incompatible_artifact_sha256": incompatible_artifact_sha256,
        "incompatible_artifact_size": incompatible_artifact_size,
        "initialization_probe_sha256": initialization_probe_sha256,
        "launcher_sha256": {
            "incompatible": incompatible_launcher_sha256,
            "supported": supported_launcher_sha256,
        },
        "stock_host_inventory_sha256": {
            "incompatible": incompatible_host_inventory_sha256,
            "supported": supported_host_inventory_sha256,
        },
        "public_contract_sha256": public_contract_sha256,
        "row": _row(build.row),
        "schema": QUERY_EVIDENCE_SCHEMA,
        "status": "failed",
    }
    retained = normalize_paths(result, replacements)
    if not isinstance(retained, dict):
        raise EvidenceError("failed Query evidence is not an object")
    encoded = canonical_bytes(retained)
    if len(encoded) > MAX_EVIDENCE_BYTES:
        raise EvidenceError("failed Query evidence exceeds its byte bound")
    return retained


def canonical_bytes(value: object) -> bytes:
    return (json.dumps(value, indent=2, sort_keys=True) + "\n").encode("utf-8")


def write_evidence(path: pathlib.Path, value: dict[str, object]) -> None:
    """Write one canonical result to a caller-owned, previously absent path."""

    encoded = canonical_bytes(value)
    if len(encoded) > MAX_EVIDENCE_BYTES:
        raise EvidenceError("Query evidence exceeds its byte bound")
    target = pathlib.Path(path).expanduser().absolute()
    parent = target.parent.resolve(strict=True)
    if not parent.is_dir() or target.exists() or target.is_symlink():
        raise EvidenceError("Query evidence output must be a new caller-owned file")
    descriptor: int | None = None
    created = False
    try:
        descriptor = os.open(parent / target.name, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        created = True
        with os.fdopen(descriptor, "wb", closefd=False) as output:
            output.write(encoded)
            output.flush()
            os.fsync(output.fileno())
        os.chmod(parent / target.name, 0o444)
    except FileExistsError as error:
        raise EvidenceError("Query evidence output must be a new caller-owned file") from error
    except OSError as error:
        if created:
            try:
                (parent / target.name).unlink(missing_ok=True)
            except OSError:
                pass
        raise EvidenceError("Query evidence output could not be written") from error
    finally:
        if descriptor is not None:
            os.close(descriptor)


def query_evidence(value: dict[str, object]) -> QueryEvidence:
    """Adapt only a passing Query-owned result to the existing matrix law."""

    if value.get("schema") != QUERY_EVIDENCE_SCHEMA or value.get("status") != "passed":
        raise EvidenceError("only passed Query v1 evidence can enter the matrix")
    candidate = value.get("candidate")
    row = value.get("row")
    if not isinstance(candidate, dict) or not isinstance(row, dict):
        raise EvidenceError("passed Query evidence identity is malformed")
    duckdb = row.get("duckdb")
    if not isinstance(duckdb, dict):
        raise EvidenceError("passed Query evidence DuckDB identity is malformed")
    try:
        from .input_admission import DuckDbIdentity
    except ImportError:
        from input_admission import DuckDbIdentity
    return QueryEvidence(
        candidate_sha256=str(candidate["sha256"]),
        row=RowIdentity(
            DuckDbIdentity(str(duckdb["version"]), str(duckdb["commit"])),
            str(row["platform"]),
        ),
        status="passed",
        channel=str(value["channel"]),
        artifact_sha256=str(value["artifact_sha256"]),
        default_signature_enforced=value.get("default_signature_enforced") is True,
        extension_version=str(value["extension_version"]),
        public_contract_sha256=str(value["public_contract_sha256"]),
    )

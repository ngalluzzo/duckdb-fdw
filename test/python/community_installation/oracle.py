"""Compose one admitted Community row into a stock-host Query result.

This module consumes existing Query-normalized ``Candidate`` and
``DeploymentEvidence`` values; it does not parse provider JSON or import
build, workflow, signing, or custody internals. A provider adapter can call
``run_stock_row`` admits the exact anchored provider record before it can start
stock DuckDB and otherwise consumes only new caller-owned roots.
"""

from __future__ import annotations

from dataclasses import dataclass
import hashlib
import json
import pathlib
import re
from collections.abc import Sequence
from typing import Any

try:
    from .deployment_admission import admit_deployment
    from .evidence import (
        build_failed_evidence,
        build_passed_evidence,
        write_evidence,
    )
    from .evidence_admission import query_evidence
    from .host_action import HostActionError, StockHostRunner
    from .file_admission import FileAdmissionError, regular_file
    from .launcher import Launcher
    from .lifecycle import HostRunner, LifecycleError
    from .matrix import DeploymentEvidence, MatrixError, RowIdentity, claimable_rows
    from .input_admission import Candidate
    from .scenarios import InitializationProbe, run_scenarios
except ImportError:
    from deployment_admission import admit_deployment
    from evidence import (
        build_failed_evidence,
        build_passed_evidence,
        write_evidence,
    )
    from evidence_admission import query_evidence
    from host_action import HostActionError, StockHostRunner
    from file_admission import FileAdmissionError, regular_file
    from launcher import Launcher
    from lifecycle import HostRunner, LifecycleError
    from matrix import DeploymentEvidence, MatrixError, RowIdentity, claimable_rows
    from input_admission import Candidate
    from scenarios import InitializationProbe, run_scenarios


PUBLIC_CONTRACT_SHA256 = "bbba900cb94f6289c9282750ed6d15a5356f6c0de9aa00fa5ae3a0ed8e452160"
MAX_PUBLIC_CONTRACT_BYTES = 64 * 1024
SHA256 = re.compile(r"[0-9a-f]{64}")


class OracleError(AssertionError):
    """Inputs or observations cannot produce authoritative Query evidence."""


@dataclass(frozen=True)
class StockOracleInputs:
    """Caller-owned authorities for one supported row and one refusal row."""

    candidate: Candidate
    deployment_record_path: pathlib.Path
    deployment_anchor_path: pathlib.Path
    supported_launcher: Launcher
    incompatible_launcher: Launcher
    supported_state_root: pathlib.Path
    supported_environment_root: pathlib.Path
    incompatible_state_root: pathlib.Path
    incompatible_environment_root: pathlib.Path
    incompatible_row: RowIdentity
    incompatible_artifact: pathlib.Path
    incompatible_artifact_size: int
    incompatible_artifact_sha256: str
    initialization_probe: InitializationProbe
    public_contract_path: pathlib.Path
    output_path: pathlib.Path
    required_incompatible_facts: tuple[str, ...]
    forbidden_diagnostic_values: tuple[str, ...] = ()
    diagnostic_roots: tuple[pathlib.Path, ...] = ()


def load_public_contract(
    path: pathlib.Path, candidate: Candidate
) -> tuple[dict[str, object], str]:
    """Admit the exact accepted 0.2.0 contract, not a caller-selected oracle."""

    try:
        source = regular_file(path, "accepted public contract")
    except FileAdmissionError as error:
        raise OracleError(str(error)) from error
    if source.stat().st_size > MAX_PUBLIC_CONTRACT_BYTES:
        raise OracleError("accepted public contract exceeds its byte bound")
    payload = source.read_bytes()
    try:
        value = json.loads(payload)
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise OracleError("accepted public contract is not UTF-8 JSON") from error
    canonical = (json.dumps(value, indent=2, sort_keys=True) + "\n").encode()
    if canonical != payload or not isinstance(value, dict):
        raise OracleError("accepted public contract is not canonical JSON")
    identity = json.dumps(value, sort_keys=True, separators=(",", ":")).encode()
    digest = hashlib.sha256(identity).hexdigest()
    if digest != PUBLIC_CONTRACT_SHA256:
        raise OracleError("accepted 0.2.0 public contract identity drifted")
    if value.get("extension") != ["duckdb_api", "0.2.0"]:
        raise OracleError("accepted public contract has the wrong extension identity")
    expected_duckdb = [
        f"v{candidate.duckdb.version}",
        candidate.duckdb.commit[:10],
    ]
    if value.get("duckdb") != expected_duckdb:
        raise OracleError("accepted public contract has the wrong DuckDB identity")
    return value, digest


def _validate_deployment(
    candidate: Candidate, deployment: DeploymentEvidence
) -> None:
    if (
        deployment.candidate_sha256 != candidate.sha256
        or deployment.row.duckdb != candidate.duckdb
        or deployment.channel != "community"
        or deployment.status != "passed"
    ):
        raise OracleError(
            "stock Query run requires one passing admitted Community deployment"
        )
    identities = (
        deployment.build_archive_sha256,
        deployment.unsigned_artifact_sha256,
        deployment.shared_payload_sha256,
        deployment.served_gzip_sha256,
        deployment.deployed_artifact_sha256,
        deployment.deployment_record_sha256,
        deployment.deployment_anchor_sha256,
    )
    if any(value is None or SHA256.fullmatch(value) is None for value in identities):
        raise OracleError(
            "stock Query run requires content-identified deployment custody"
        )
    if deployment.unsigned_artifact_sha256 == deployment.deployed_artifact_sha256:
        raise OracleError("stock Query run received unsigned artifact substitution")


def evaluate_row(
    *,
    candidate: Candidate,
    deployment: DeploymentEvidence,
    supported_runner: HostRunner,
    incompatible_runner: HostRunner,
    incompatible_row: RowIdentity,
    incompatible_artifact_size: int,
    incompatible_artifact_sha256: str,
    initialization_probe: InitializationProbe,
    supported_launcher_sha256: str,
    incompatible_launcher_sha256: str,
    supported_host_inventory_sha256: str,
    incompatible_host_inventory_sha256: str,
    public_contract: Any,
    public_contract_sha256: str,
    required_incompatible_facts: tuple[str, ...],
    forbidden_diagnostic_values: tuple[str, ...],
    replacements: Sequence[tuple[pathlib.Path, str]],
    output_path: pathlib.Path,
) -> dict[str, object]:
    """Evaluate deterministic runners and write a passing or failed row result."""

    _validate_deployment(candidate, deployment)
    if incompatible_artifact_size < 0 or SHA256.fullmatch(incompatible_artifact_sha256) is None:
        raise OracleError("representative incompatible artifact is not content-identified")
    if any(
        SHA256.fullmatch(value) is None
        for value in (
            initialization_probe.evidence_sha256,
            supported_launcher_sha256,
            incompatible_launcher_sha256,
            supported_host_inventory_sha256,
            incompatible_host_inventory_sha256,
        )
    ):
        raise OracleError("stock host authority is not content-identified")
    assert deployment.deployed_artifact_sha256 is not None
    try:
        scenarios = run_scenarios(
            supported_runner=supported_runner,
            incompatible_runner=incompatible_runner,
            supported_row=deployment.row,
            incompatible_row=incompatible_row,
            artifact_sha256=deployment.deployed_artifact_sha256,
            public_contract=public_contract,
            required_incompatible_facts=required_incompatible_facts,
            initialization_probe=initialization_probe,
            forbidden_diagnostic_values=forbidden_diagnostic_values,
        )
        result = build_passed_evidence(
            candidate=candidate,
            deployment=deployment,
            scenarios=scenarios,
            incompatible_artifact_size=incompatible_artifact_size,
            incompatible_artifact_sha256=incompatible_artifact_sha256,
            supported_launcher_sha256=supported_launcher_sha256,
            incompatible_launcher_sha256=incompatible_launcher_sha256,
            supported_host_inventory_sha256=supported_host_inventory_sha256,
            incompatible_host_inventory_sha256=incompatible_host_inventory_sha256,
            public_contract_sha256=public_contract_sha256,
            replacements=replacements,
        )
        normalized = query_evidence(result, candidate)
        claimed = claimable_rows(
            candidate, [deployment], [normalized], public_contract_sha256
        )
        if claimed != (deployment.row,):
            raise OracleError("passing Query result did not claim exactly its own row")
    except (HostActionError, LifecycleError, MatrixError) as error:
        result = build_failed_evidence(
            candidate=candidate,
            deployment=deployment,
            public_contract_sha256=public_contract_sha256,
            category="stock_host_lifecycle",
            diagnostic=str(error),
            incompatible_artifact_size=incompatible_artifact_size,
            incompatible_artifact_sha256=incompatible_artifact_sha256,
            initialization_probe_sha256=initialization_probe.evidence_sha256,
            supported_launcher_sha256=supported_launcher_sha256,
            incompatible_launcher_sha256=incompatible_launcher_sha256,
            supported_host_inventory_sha256=supported_host_inventory_sha256,
            incompatible_host_inventory_sha256=incompatible_host_inventory_sha256,
            replacements=replacements,
        )
    write_evidence(output_path, result)
    return result


def run_stock_row(inputs: StockOracleInputs) -> dict[str, object]:
    """Run the real stock-host seam after provider inputs have been admitted."""

    try:
        deployment = admit_deployment(
            inputs.deployment_record_path,
            inputs.deployment_anchor_path,
            inputs.candidate,
        )
    except ValueError as error:
        raise OracleError(str(error)) from error
    _validate_deployment(inputs.candidate, deployment)
    public_contract, contract_sha256 = load_public_contract(
        inputs.public_contract_path, inputs.candidate
    )
    supported = StockHostRunner(
        launcher=inputs.supported_launcher,
        row=deployment.row,
        state_root=inputs.supported_state_root,
        environment_root=inputs.supported_environment_root,
        diagnostic_roots=inputs.diagnostic_roots,
    )
    incompatible = StockHostRunner(
        launcher=inputs.incompatible_launcher,
        row=inputs.incompatible_row,
        state_root=inputs.incompatible_state_root,
        environment_root=inputs.incompatible_environment_root,
        incompatible_artifact=inputs.incompatible_artifact,
        incompatible_artifact_size=inputs.incompatible_artifact_size,
        incompatible_artifact_sha256=inputs.incompatible_artifact_sha256,
        diagnostic_roots=inputs.diagnostic_roots,
    )
    replacements = (
        (inputs.supported_state_root, "<supported-state-root>"),
        (inputs.supported_environment_root, "<supported-environment-root>"),
        (inputs.incompatible_state_root, "<incompatible-state-root>"),
        (inputs.incompatible_environment_root, "<incompatible-environment-root>"),
        (inputs.incompatible_artifact.parent, "<incompatible-root>"),
        (inputs.public_contract_path.parent, "<contract-root>"),
        *((root, "<diagnostic-root>") for root in inputs.diagnostic_roots),
    )
    try:
        return evaluate_row(
            candidate=inputs.candidate,
            deployment=deployment,
            supported_runner=supported,
            incompatible_runner=incompatible,
            incompatible_row=inputs.incompatible_row,
            incompatible_artifact_size=inputs.incompatible_artifact_size,
            incompatible_artifact_sha256=inputs.incompatible_artifact_sha256,
            initialization_probe=inputs.initialization_probe,
            supported_launcher_sha256=inputs.supported_launcher.executable_sha256,
            incompatible_launcher_sha256=inputs.incompatible_launcher.executable_sha256,
            supported_host_inventory_sha256=(
                inputs.supported_launcher.stock_host_inventory_sha256
            ),
            incompatible_host_inventory_sha256=(
                inputs.incompatible_launcher.stock_host_inventory_sha256
            ),
            public_contract=public_contract,
            public_contract_sha256=contract_sha256,
            required_incompatible_facts=inputs.required_incompatible_facts,
            forbidden_diagnostic_values=inputs.forbidden_diagnostic_values,
            replacements=replacements,
            output_path=inputs.output_path,
        )
    finally:
        supported.close()
        incompatible.close()

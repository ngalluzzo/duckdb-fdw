"""Admit exact status-aware Query v2 evidence for support-matrix use.

Evidence production and filesystem writing live in ``evidence.py``. This module
owns only the untrusted-document shape, lifecycle completeness, digest syntax,
and conversion into Query's normalized matrix observation.
"""

from __future__ import annotations

import hashlib
import json
import re

try:
    from .evidence import EvidenceError, QUERY_EVIDENCE_SCHEMA
    from .input_admission import Candidate, DuckDbIdentity
    from .matrix import QueryEvidence, RowIdentity
except ImportError:
    from evidence import EvidenceError, QUERY_EVIDENCE_SCHEMA
    from input_admission import Candidate, DuckDbIdentity
    from matrix import QueryEvidence, RowIdentity


SHA256 = re.compile(r"[0-9a-f]{64}")
GIT_ID = re.compile(r"[0-9a-f]{40}")
VERSION = re.compile(r"[0-9]+\.[0-9]+\.[0-9]+")


def _exact_object(
    value: object, label: str, keys: set[str]
) -> dict[str, object]:
    if not isinstance(value, dict) or set(value) != keys:
        raise EvidenceError(f"{label} fields differ from the Query v2 contract")
    return value


def _required_string(value: object, label: str) -> str:
    if not isinstance(value, str) or not value:
        raise EvidenceError(f"{label} is not a non-empty string")
    return value


def _required_digest(value: object, label: str) -> str:
    result = _required_string(value, label)
    if SHA256.fullmatch(result) is None:
        raise EvidenceError(f"{label} is not a SHA-256 identity")
    return result


def _required_size(value: object, label: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or value < 0:
        raise EvidenceError(f"{label} is not a non-negative size")
    return value


def _evidence_row(value: object, label: str) -> RowIdentity:
    row = _exact_object(value, label, {"duckdb", "platform"})
    duckdb = _exact_object(
        row["duckdb"], f"{label}.duckdb", {"version", "commit"}
    )
    version = _required_string(duckdb["version"], f"{label}.duckdb.version")
    commit = _required_string(duckdb["commit"], f"{label}.duckdb.commit")
    if VERSION.fullmatch(version) is None or GIT_ID.fullmatch(commit) is None:
        raise EvidenceError(f"{label} DuckDB identity is malformed")
    platform = _required_string(row["platform"], f"{label}.platform")
    return RowIdentity(DuckDbIdentity(version, commit), platform)


def _extension_evidence(
    value: object,
    *,
    artifact_sha256: str,
    loaded: bool,
    label: str,
) -> str:
    extension = _exact_object(
        value,
        label,
        {
            "artifact_sha256",
            "install_path",
            "install_source",
            "installed",
            "loaded",
            "name",
            "version",
        },
    )
    if (
        extension["artifact_sha256"] != artifact_sha256
        or extension["install_source"] != "community"
        or extension["installed"] is not True
        or extension["loaded"] is not loaded
        or extension["name"] != "duckdb_api"
        or extension["version"] != "0.2.0"
    ):
        raise EvidenceError(f"{label} identity or state is malformed")
    return _required_string(extension["install_path"], f"{label}.install_path")


def _validate_observation(
    value: object,
    *,
    action: str,
    artifact_sha256: str,
    expected_row: RowIdentity | None,
    extension_loaded: bool | None,
    function_registered: bool,
    label: str,
) -> tuple[str, object, str | None]:
    observation = _exact_object(
        value,
        label,
        {
            "action",
            "allow_unsigned_extensions",
            "behavior",
            "diagnostic",
            "diagnostic_category",
            "extension",
            "function_registered",
            "ok",
            "process_token",
            "row",
        },
    )
    row = _evidence_row(observation["row"], f"{label}.row")
    if expected_row is not None and row != expected_row:
        raise EvidenceError(f"{label} used a different Community row")
    if (
        observation["action"] != action
        or observation["allow_unsigned_extensions"] is not False
        or observation["function_registered"] is not function_registered
        or not isinstance(observation["ok"], bool)
    ):
        raise EvidenceError(f"{label} policy or state is malformed")
    diagnostic = observation["diagnostic"]
    category = observation["diagnostic_category"]
    if diagnostic is not None and not isinstance(diagnostic, str):
        raise EvidenceError(f"{label} diagnostic is malformed")
    if category is not None and not isinstance(category, str):
        raise EvidenceError(f"{label} diagnostic category is malformed")
    behavior = observation["behavior"]
    if behavior is not None and not isinstance(behavior, dict):
        raise EvidenceError(f"{label} behavior is malformed")
    token = _required_string(observation["process_token"], f"{label}.process_token")
    if extension_loaded is None:
        if observation["extension"] is not None:
            raise EvidenceError(f"{label} unexpectedly observed an extension")
        install_path = None
    else:
        install_path = _extension_evidence(
            observation["extension"],
            artifact_sha256=artifact_sha256,
            loaded=extension_loaded,
            label=f"{label}.extension",
        )
    return token, behavior, install_path


def _common_evidence(
    value: dict[str, object], status: str, admitted_candidate: Candidate
) -> tuple[
    dict[str, object],
    str,
    str,
    str,
    str,
    RowIdentity,
]:
    common = {
        "artifact_sha256",
        "candidate",
        "channel",
        "community",
        "default_signature_enforced",
        "extension_version",
        "incompatible_artifact_sha256",
        "incompatible_artifact_size",
        "initialization_probe_sha256",
        "launcher_sha256",
        "stock_host_inventory_sha256",
        "public_contract_sha256",
        "row",
        "schema",
        "status",
    }
    root = _exact_object(
        value,
        "Query evidence",
        common | ({"incompatible", "supported"} if status == "passed" else {"failure"}),
    )
    candidate = _exact_object(
        root["candidate"],
        "Query evidence candidate",
        {"sha256", "source_commit", "source_tag", "source_tree"},
    )
    candidate_sha256 = _required_digest(
        candidate["sha256"], "Query candidate digest"
    )
    if (
        candidate_sha256 != admitted_candidate.sha256
        or candidate["source_commit"] != admitted_candidate.source_commit
        or candidate["source_tree"] != admitted_candidate.source_tree
        or candidate["source_tag"] != admitted_candidate.source_tag
    ):
        raise EvidenceError("Query evidence names a different admitted candidate")
    if root["channel"] != "community":
        raise EvidenceError("Query evidence is not from the Community channel")
    if root["default_signature_enforced"] is not True:
        raise EvidenceError("Query evidence weakened default signature policy")
    if root["extension_version"] != "0.2.0":
        raise EvidenceError("Query evidence has the wrong extension version")

    community = _exact_object(
        root["community"],
        "Query evidence community",
        {
            "deployment_anchor_sha256",
            "deployment_record_sha256",
            "repository",
            "source_commit",
        },
    )
    deployment_record_sha256 = _required_digest(
        community["deployment_record_sha256"], "Query deployment record"
    )
    deployment_anchor_sha256 = _required_digest(
        community["deployment_anchor_sha256"], "Query deployment anchor"
    )
    if (
        community["repository"] != admitted_candidate.community_repository
        or community["source_commit"] != admitted_candidate.community_commit
    ):
        raise EvidenceError("Query evidence names different Community source authority")

    artifact_sha256 = _required_digest(
        root["artifact_sha256"], "Query deployed artifact"
    )
    public_contract_sha256 = _required_digest(
        root["public_contract_sha256"], "Query public contract"
    )
    _required_digest(
        root["incompatible_artifact_sha256"], "Query incompatible artifact"
    )
    _required_size(root["incompatible_artifact_size"], "Query incompatible artifact")
    _required_digest(
        root["initialization_probe_sha256"], "Query initialization probe"
    )
    for field in ("launcher_sha256", "stock_host_inventory_sha256"):
        identities = _exact_object(
            root[field], f"Query {field}", {"supported", "incompatible"}
        )
        _required_digest(identities["supported"], f"Query {field} supported")
        _required_digest(identities["incompatible"], f"Query {field} incompatible")
    row = _evidence_row(root["row"], "Query evidence row")
    if row.duckdb != admitted_candidate.duckdb:
        raise EvidenceError("Query evidence targets a different DuckDB identity")
    return (
        root,
        candidate_sha256,
        artifact_sha256,
        deployment_record_sha256,
        deployment_anchor_sha256,
        row,
    )


def _validate_passed(
    root: dict[str, object], artifact_sha256: str, row: RowIdentity
) -> None:
    supported = root["supported"]
    if not isinstance(supported, list) or len(supported) != 4:
        raise EvidenceError("passed Query evidence lacks the complete lifecycle")
    tokens: list[str] = []
    behaviors: list[object] = []
    install_paths: list[str] = []
    for index, (action, loaded, registered) in enumerate(
        (
            ("pre_install", None, False),
            ("install", False, False),
            ("repeat_install", False, False),
            ("load_query", True, True),
        )
    ):
        token, behavior, install_path = _validate_observation(
            supported[index],
            action=action,
            artifact_sha256=artifact_sha256,
            expected_row=row,
            extension_loaded=loaded,
            function_registered=registered,
            label=f"Query supported[{index}]",
        )
        observation = supported[index]
        assert isinstance(observation, dict)
        if observation["ok"] is not True:
            raise EvidenceError("passed Query lifecycle contains a failed action")
        if (
            observation["diagnostic"] is not None
            or observation["diagnostic_category"] is not None
        ):
            raise EvidenceError("passed Query lifecycle contains a diagnostic")
        tokens.append(token)
        behaviors.append(behavior)
        if install_path is not None:
            install_paths.append(install_path)
    if len(set(tokens)) != len(tokens) or behaviors[:3] != [None, None, None]:
        raise EvidenceError("passed Query lifecycle reused a process or behavior")
    if len(install_paths) != 3 or len(set(install_paths)) != 1:
        raise EvidenceError("passed Query lifecycle changed its installed artifact path")
    behavior = behaviors[3]
    if not isinstance(behavior, dict):
        raise EvidenceError("passed Query load lacks public behavior")
    observed_contract = hashlib.sha256(
        json.dumps(behavior, sort_keys=True, separators=(",", ":")).encode()
    ).hexdigest()
    if observed_contract != root["public_contract_sha256"]:
        raise EvidenceError("passed Query behavior differs from its public contract")
    incompatible = root["incompatible"]
    token, incompatible_behavior, install_path = _validate_observation(
        incompatible,
        action="incompatible",
        artifact_sha256=artifact_sha256,
        expected_row=None,
        extension_loaded=None,
        function_registered=False,
        label="Query incompatible",
    )
    assert isinstance(incompatible, dict)
    if (
        incompatible["ok"] is not False
        or incompatible_behavior is not None
        or install_path is not None
        or incompatible["diagnostic_category"] not in {"version", "platform"}
        or not isinstance(incompatible["diagnostic"], str)
        or not incompatible["diagnostic"]
        or token in tokens
    ):
        raise EvidenceError("passed Query refusal evidence is malformed")


def query_evidence(
    value: dict[str, object], admitted_candidate: Candidate
) -> QueryEvidence:
    """Validate one exact status-aware Query v2 document for matrix admission."""

    status = value.get("status")
    if value.get("schema") != QUERY_EVIDENCE_SCHEMA or status not in {
        "passed",
        "failed",
    }:
        raise EvidenceError("Query evidence has an unknown schema or status")
    (
        root,
        candidate_sha256,
        artifact_sha256,
        deployment_record_sha256,
        deployment_anchor_sha256,
        row,
    ) = _common_evidence(value, status, admitted_candidate)
    if status == "passed":
        _validate_passed(root, artifact_sha256, row)
    else:
        failure = _exact_object(
            root["failure"], "failed Query evidence", {"category", "diagnostic"}
        )
        _required_string(failure["category"], "failed Query category")
        _required_string(failure["diagnostic"], "failed Query diagnostic")

    return QueryEvidence(
        candidate_sha256=candidate_sha256,
        row=row,
        status=status,
        channel="community",
        artifact_sha256=artifact_sha256,
        deployment_record_sha256=deployment_record_sha256,
        deployment_anchor_sha256=deployment_anchor_sha256,
        default_signature_enforced=True,
        extension_version="0.2.0",
        public_contract_sha256=str(root["public_contract_sha256"]),
    )

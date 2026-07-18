"""Admit the exact anchored Community source-candidate handoff.

Engineering Enablement owns candidate construction and source/toolchain
verification. Query Experience checks the narrow, content-identified handoff
before it can influence lifecycle or compatibility evidence. Deployment
records use their own narrow admission module once provider bytes exist.
"""

from __future__ import annotations

from dataclasses import dataclass
import hashlib
import json
import os
import pathlib
import re
import stat
from typing import Any
from urllib.parse import urlsplit


CANDIDATE_SCHEMA = "duckdb_api/community-candidate/v1"
DEPENDENCY_AUDIT_SCHEMA = "duckdb_api/dependency-audit/v1"
MAX_CANDIDATE_BYTES = 64 * 1024
SHA256 = re.compile(r"[0-9a-f]{64}")
GIT_ID = re.compile(r"[0-9a-f]{40}")
DUCKDB_VERSION = re.compile(r"[0-9]+\.[0-9]+\.[0-9]+")
ANCHOR = re.compile(r"([0-9a-f]{64})  candidate\.json\n")


class AdmissionError(ValueError):
    """The provider handoff is malformed, ambiguous, or not content-bound."""


@dataclass(frozen=True, order=True)
class DuckDbIdentity:
    """One exact latest-stable DuckDB identity selected by the candidate."""

    version: str
    commit: str


@dataclass(frozen=True)
class Candidate:
    """Query's immutable view of an Enablement-admitted source candidate."""

    sha256: str
    project_repository: str
    source_tag: str
    source_commit: str
    source_tree: str
    duckdb: DuckDbIdentity
    community_repository: str
    community_commit: str
    extension_template_commit: str
    extension_ci_tools_ref: str
    extension_ci_tools_commit: str
    descriptor_expectation_sha256: str
    dependency_audit_sha256: str
    dependency_audit_anchor_sha256: str
    pins_sha256: str


def read_regular_bytes(path: pathlib.Path, label: str, limit: int) -> bytes:
    """Read one bounded, stable regular-file snapshot without following links."""

    try:
        before = path.lstat()
    except OSError as error:
        raise AdmissionError(f"{label} is unavailable") from error
    if stat.S_ISLNK(before.st_mode):
        raise AdmissionError(f"{label} must not be a symlink")
    if not stat.S_ISREG(before.st_mode):
        raise AdmissionError(f"{label} is not a regular file")
    if before.st_size > limit:
        raise AdmissionError(f"{label} exceeds its size bound")

    descriptor: int | None = None
    try:
        descriptor = os.open(path, os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0))
        opened = os.fstat(descriptor)
        if (
            (opened.st_dev, opened.st_ino) != (before.st_dev, before.st_ino)
            or stat.S_IFMT(opened.st_mode) != stat.S_IFMT(before.st_mode)
            or opened.st_size != before.st_size
            or opened.st_mtime_ns != before.st_mtime_ns
            or opened.st_ctime_ns != before.st_ctime_ns
        ):
            raise AdmissionError(f"{label} changed before it could be read")
        if not stat.S_ISREG(opened.st_mode):
            raise AdmissionError(f"{label} is not a regular file")
        if opened.st_size > limit:
            raise AdmissionError(f"{label} exceeds its size bound")

        chunks: list[bytes] = []
        remaining = opened.st_size
        while remaining:
            chunk = os.read(descriptor, min(remaining, 64 * 1024))
            if not chunk:
                raise AdmissionError(f"{label} changed while it was read")
            chunks.append(chunk)
            remaining -= len(chunk)
        if os.read(descriptor, 1):
            raise AdmissionError(f"{label} changed while it was read")

        after = os.fstat(descriptor)
        if (
            (after.st_dev, after.st_ino) != (opened.st_dev, opened.st_ino)
            or stat.S_IFMT(after.st_mode) != stat.S_IFMT(opened.st_mode)
            or after.st_size != opened.st_size
            or after.st_mtime_ns != opened.st_mtime_ns
            or after.st_ctime_ns != opened.st_ctime_ns
        ):
            raise AdmissionError(f"{label} changed while it was read")
        return b"".join(chunks)
    except AdmissionError:
        raise
    except OSError as error:
        raise AdmissionError(f"{label} could not be read") from error
    finally:
        if descriptor is not None:
            os.close(descriptor)


def _reject_duplicate_keys(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise AdmissionError(f"candidate contains duplicate key {key!r}")
        result[key] = value
    return result


def _object(value: Any, label: str, keys: set[str]) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise AdmissionError(f"{label} is not an object")
    if set(value) != keys:
        raise AdmissionError(f"{label} fields differ from the candidate v1 contract")
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


def _repository(value: Any, label: str) -> str:
    actual = _string(value, label)
    parsed = urlsplit(actual)
    if (
        parsed.scheme != "https"
        or not parsed.hostname
        or parsed.username is not None
        or parsed.password is not None
        or parsed.query
        or parsed.fragment
    ):
        raise AdmissionError(f"{label} is not an uncredentialed HTTPS repository")
    return actual


def admit_candidate(candidate_path: pathlib.Path, anchor_path: pathlib.Path) -> Candidate:
    """Verify byte custody and return the Query-relevant candidate identities."""

    candidate_path = pathlib.Path(candidate_path)
    anchor_path = pathlib.Path(anchor_path)
    if candidate_path.name != "candidate.json":
        raise AdmissionError("candidate input must be named candidate.json")
    if anchor_path.name != "candidate.sha256":
        raise AdmissionError("candidate anchor must be named candidate.sha256")
    candidate_bytes = read_regular_bytes(
        candidate_path, "candidate.json", MAX_CANDIDATE_BYTES
    )
    anchor_bytes = read_regular_bytes(anchor_path, "candidate.sha256", 256)
    try:
        anchor_text = anchor_bytes.decode("ascii")
    except UnicodeDecodeError as error:
        raise AdmissionError("candidate.sha256 is not ASCII") from error
    match = ANCHOR.fullmatch(anchor_text)
    if match is None:
        raise AdmissionError("candidate.sha256 has invalid syntax")
    digest = hashlib.sha256(candidate_bytes).hexdigest()
    if digest != match.group(1):
        raise AdmissionError("candidate.json does not match candidate.sha256")

    try:
        document = json.loads(
            candidate_bytes.decode("utf-8"), object_pairs_hook=_reject_duplicate_keys
        )
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise AdmissionError("candidate.json is not valid UTF-8 JSON") from error
    canonical = (json.dumps(document, sort_keys=True, indent=2) + "\n").encode()
    if canonical != candidate_bytes:
        raise AdmissionError("candidate.json is not canonical JSON")

    root = _object(
        document,
        "candidate",
        {
            "schema",
            "status",
            "project",
            "source",
            "duckdb",
            "community",
            "descriptor_expectation",
            "dependency_audit",
            "pins_sha256",
        },
    )
    _fixed(root["schema"], "candidate.schema", CANDIDATE_SCHEMA)
    _fixed(root["status"], "candidate.status", "admitted_candidate")

    project = _object(
        root["project"],
        "candidate.project",
        {"extension", "version", "license", "repository", "tag", "tag_state"},
    )
    _fixed(project["extension"], "candidate.project.extension", "duckdb_api")
    _fixed(project["version"], "candidate.project.version", "0.2.0")
    _fixed(project["license"], "candidate.project.license", "MIT")
    project_repository = _repository(
        project["repository"], "candidate.project.repository"
    )
    source_tag = _fixed(project["tag"], "candidate.project.tag", "v0.2.0")
    _fixed(project["tag_state"], "candidate.project.tag_state", "pending")

    source = _object(
        root["source"], "candidate.source", {"commit", "tree"}
    )
    source_commit = _matching(
        source["commit"], "candidate.source.commit", GIT_ID
    )
    source_tree = _matching(source["tree"], "candidate.source.tree", GIT_ID)

    duckdb = _object(root["duckdb"], "candidate.duckdb", {"version", "commit"})
    duckdb_identity = DuckDbIdentity(
        version=_matching(
            duckdb["version"], "candidate.duckdb.version", DUCKDB_VERSION
        ),
        commit=_matching(duckdb["commit"], "candidate.duckdb.commit", GIT_ID),
    )

    community = _object(
        root["community"],
        "candidate.community",
        {"repository", "commit", "extension_template", "extension_ci_tools"},
    )
    community_repository = _repository(
        community["repository"], "candidate.community.repository"
    )
    community_commit = _matching(
        community["commit"], "candidate.community.commit", GIT_ID
    )
    template = _object(
        community["extension_template"],
        "candidate.community.extension_template",
        {"repository", "commit"},
    )
    _repository(
        template["repository"], "candidate.community.extension_template.repository"
    )
    extension_template_commit = _matching(
        template["commit"], "candidate.community.extension_template.commit", GIT_ID
    )
    ci_tools = _object(
        community["extension_ci_tools"],
        "candidate.community.extension_ci_tools",
        {"repository", "ref", "commit"},
    )
    _repository(
        ci_tools["repository"], "candidate.community.extension_ci_tools.repository"
    )
    extension_ci_tools_ref = _string(
        ci_tools["ref"], "candidate.community.extension_ci_tools.ref"
    )
    extension_ci_tools_commit = _matching(
        ci_tools["commit"], "candidate.community.extension_ci_tools.commit", GIT_ID
    )

    descriptor = _object(
        root["descriptor_expectation"],
        "candidate.descriptor_expectation",
        {"status", "sha256"},
    )
    _fixed(
        descriptor["status"],
        "candidate.descriptor_expectation.status",
        "pending_non_authoritative",
    )
    descriptor_sha256 = _matching(
        descriptor["sha256"], "candidate.descriptor_expectation.sha256", SHA256
    )

    dependency = _object(
        root["dependency_audit"],
        "candidate.dependency_audit",
        {"schema", "sha256", "anchor_sha256"},
    )
    _fixed(
        dependency["schema"],
        "candidate.dependency_audit.schema",
        DEPENDENCY_AUDIT_SCHEMA,
    )
    dependency_sha256 = _matching(
        dependency["sha256"], "candidate.dependency_audit.sha256", SHA256
    )
    dependency_anchor_sha256 = _matching(
        dependency["anchor_sha256"],
        "candidate.dependency_audit.anchor_sha256",
        SHA256,
    )
    pins_sha256 = _matching(
        root["pins_sha256"], "candidate.pins_sha256", SHA256
    )

    return Candidate(
        sha256=digest,
        project_repository=project_repository,
        source_tag=source_tag,
        source_commit=source_commit,
        source_tree=source_tree,
        duckdb=duckdb_identity,
        community_repository=community_repository,
        community_commit=community_commit,
        extension_template_commit=extension_template_commit,
        extension_ci_tools_ref=extension_ci_tools_ref,
        extension_ci_tools_commit=extension_ci_tools_commit,
        descriptor_expectation_sha256=descriptor_sha256,
        dependency_audit_sha256=dependency_sha256,
        dependency_audit_anchor_sha256=dependency_anchor_sha256,
        pins_sha256=pins_sha256,
    )

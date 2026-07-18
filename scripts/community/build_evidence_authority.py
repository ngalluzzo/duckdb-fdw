"""Reviewed authority boundary for Community build evidence."""

from __future__ import annotations

import re
from typing import Any

from record_format import require


REGISTRY_SCHEMA = "duckdb_api/community-build-authorities/v1"
DESCRIPTOR_SCHEMA = "duckdb_api/community-descriptor-admission/v1"
COMMUNITY_REPOSITORY = "duckdb/community-extensions"
APPROVED_REGISTRY_SHA256 = (
    "7c40001748fdf980ab73ee67200f1f2ad89b1a76c43165c1d99a1892192ac8ed"
)
SHA256 = re.compile(r"[0-9a-f]{64}")
COMMIT = re.compile(r"[0-9a-f]{40}")
REF = re.compile(r"[A-Za-z0-9][A-Za-z0-9._/-]{0,254}")
WORKFLOW_PATH = re.compile(r"\.github/workflows/[A-Za-z0-9][A-Za-z0-9._-]*\.ya?ml")


def _mapping(value: object, label: str) -> dict[str, Any]:
    require(isinstance(value, dict), f"{label} must be an object")
    return value


def _fields(value: object, expected: set[str], label: str) -> dict[str, Any]:
    result = _mapping(value, label)
    require(set(result) == expected, f"{label} fields drifted")
    return result


def _positive_integer(value: object, label: str) -> int:
    require(isinstance(value, int) and not isinstance(value, bool) and value > 0,
            f"{label} must be a positive integer")
    return value


def _digest(value: object, label: str) -> str:
    require(isinstance(value, str) and SHA256.fullmatch(value) is not None,
            f"{label} must be lowercase SHA-256")
    return value


def _commit(value: object, label: str) -> str:
    require(isinstance(value, str) and COMMIT.fullmatch(value) is not None,
            f"{label} must be an exact commit")
    return value


def _ref(value: object, label: str) -> str:
    require(isinstance(value, str) and REF.fullmatch(value) is not None,
            f"{label} is invalid")
    require(value not in {"HEAD", "head"}, f"{label} must not be mutable")
    return value


def validate_descriptor_admission(
    admission: dict[str, Any], admission_digest: str
) -> dict[str, str]:
    """Return the immutable source named by a locally admitted proposal."""

    _digest(admission_digest, "descriptor admission digest")
    root = _fields(
        admission,
        {
            "authority",
            "candidate",
            "dependency_audit",
            "descriptor_cycle_sha256",
            "descriptor_expectation_sha256",
            "pins_sha256",
            "proposal",
            "publication_status",
            "schema",
            "status",
            "support_claims",
        },
        "descriptor admission",
    )
    require(root["schema"] == DESCRIPTOR_SCHEMA,
            "descriptor admission schema is unsupported")
    require(
        root["authority"] == "local_provider_admission_only"
        and root["status"] == "proposal_admitted"
        and root["publication_status"] == "not_submitted"
        and root["support_claims"] == [],
        "descriptor admission overstates its authority",
    )
    candidate = _fields(
        root["candidate"], {"anchor_sha256", "sha256", "source"},
        "descriptor candidate",
    )
    source = _fields(candidate["source"], {"commit", "tree"},
                     "descriptor source")
    _commit(source["commit"], "descriptor source commit")
    _commit(source["tree"], "descriptor source tree")
    proposal = _fields(
        root["proposal"], {"extension", "filename", "repo", "sha256"},
        "descriptor proposal",
    )
    require(proposal["filename"] == "description.yml",
            "descriptor proposal filename drifted")
    repo = _fields(proposal["repo"], {"github", "ref"},
                   "descriptor proposal repository")
    require(repo["ref"] == source["commit"],
            "descriptor proposal names a different source")
    require(isinstance(repo["github"], str) and repo["github"],
            "descriptor proposal repository is invalid")
    _digest(root["pins_sha256"], "descriptor admission pins digest")
    return {
        "digest": admission_digest,
        "pins_sha256": root["pins_sha256"],
        "repository": repo["github"],
        "source_commit": source["commit"],
        "source_tree": source["tree"],
    }


def _validate_authority(value: object, index: int) -> dict[str, Any]:
    label = f"build authority {index}"
    authority = _fields(
        value,
        {
            "artifacts_export_sha256",
            "base",
            "descriptor_admission_sha256",
            "extension_source",
            "head",
            "jobs_export_sha256",
            "matrix_export_sha256",
            "pins_sha256",
            "pull_request_export_sha256",
            "pull_request_number",
            "repository",
            "run",
            "run_export_sha256",
            "status",
            "workflow",
        },
        label,
    )
    require(authority["status"] == "maintainer_approved",
            f"{label} is not maintainer approved")
    require(authority["repository"] == COMMUNITY_REPOSITORY,
            f"{label} repository drifted")
    _positive_integer(authority["pull_request_number"],
                      f"{label} pull request")
    for name in (
        "artifacts_export_sha256",
        "descriptor_admission_sha256",
        "jobs_export_sha256",
        "matrix_export_sha256",
        "pins_sha256",
        "pull_request_export_sha256",
        "run_export_sha256",
    ):
        _digest(authority[name], f"{label} {name}")
    head = _fields(authority["head"], {"ref", "repository", "sha"},
                   f"{label} head")
    require(isinstance(head["repository"], str) and head["repository"],
            f"{label} head repository is invalid")
    _ref(head["ref"], f"{label} head ref")
    _commit(head["sha"], f"{label} head commit")
    extension_source = _fields(
        authority["extension_source"], {"commit", "repository", "tree"},
        f"{label} extension source",
    )
    require(
        isinstance(extension_source["repository"], str)
        and extension_source["repository"],
        f"{label} extension source repository is invalid",
    )
    _commit(extension_source["commit"], f"{label} extension source commit")
    _commit(extension_source["tree"], f"{label} extension source tree")
    base = _fields(authority["base"], {"ref", "sha"}, f"{label} base")
    _ref(base["ref"], f"{label} base ref")
    _commit(base["sha"], f"{label} base commit")
    run = _fields(authority["run"], {"attempt", "head_sha", "id"}, f"{label} run")
    _positive_integer(run["id"], f"{label} run id")
    _positive_integer(run["attempt"], f"{label} run attempt")
    _commit(run["head_sha"], f"{label} run head commit")
    workflow = _fields(authority["workflow"], {"id", "path"},
                       f"{label} workflow")
    _positive_integer(workflow["id"], f"{label} workflow id")
    require(
        isinstance(workflow["path"], str)
        and WORKFLOW_PATH.fullmatch(workflow["path"]) is not None,
        f"{label} workflow path is invalid",
    )
    return authority


def validate_registry(
    registry: dict[str, Any], registry_digest: str
) -> list[dict[str, Any]]:
    """Admit only the code-reviewed registry bytes, never caller policy."""

    require(
        registry_digest == APPROVED_REGISTRY_SHA256,
        "build authority registry is not the exact reviewed policy",
    )
    root = _fields(registry, {"approved", "schema"}, "build authority registry")
    require(root["schema"] == REGISTRY_SCHEMA,
            "build authority registry schema is unsupported")
    approved = root["approved"]
    require(isinstance(approved, list), "build authority registry must contain a list")
    result = [_validate_authority(value, index) for index, value in enumerate(approved)]
    identities = {
        (value["repository"], value["pull_request_number"], value["run"]["id"],
         value["run"]["attempt"])
        for value in result
    }
    require(len(identities) == len(result), "build authority registry is ambiguous")
    return result


def select_authority(
    approved: list[dict[str, Any]],
    descriptor: dict[str, str],
    pins_digest: str,
    export_digests: dict[str, str],
) -> dict[str, Any]:
    """Select the sole reviewed authority for these exact input bytes."""

    require(descriptor["pins_sha256"] == pins_digest,
            "descriptor admission names different Community pins")
    matches = [
        value
        for value in approved
        if value["descriptor_admission_sha256"] == descriptor["digest"]
        and value["pins_sha256"] == pins_digest
        and value["pull_request_export_sha256"] == export_digests["pull_request"]
        and value["run_export_sha256"] == export_digests["run"]
        and value["jobs_export_sha256"] == export_digests["jobs"]
        and value["matrix_export_sha256"] == export_digests["matrix"]
        and value["artifacts_export_sha256"] == export_digests["artifacts"]
    ]
    require(len(matches) == 1,
            "build inputs do not match one maintainer-approved authority")
    authority = matches[0]
    require(
        authority["extension_source"]
        == {
            "commit": descriptor["source_commit"],
            "repository": descriptor["repository"],
            "tree": descriptor["source_tree"],
        },
        "build authority names a different admitted descriptor source",
    )
    return authority

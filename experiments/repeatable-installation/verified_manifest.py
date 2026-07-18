"""Validate the release-manifest contract consumed by Query Experience."""

from __future__ import annotations

import hashlib
import pathlib
import re
from dataclasses import dataclass


EXPECTED_EXTENSION = "duckdb_api"
EXPECTED_EXTENSION_VERSION = "0.1.0"
SHA256_PATTERN = re.compile(r"[0-9a-f]{64}")
COMMIT_PATTERN = re.compile(r"[0-9a-f]{40}")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def object_value(value: object, label: str) -> dict[str, object]:
    require(isinstance(value, dict), f"{label} is not an object")
    return value


def exact_keys(value: dict[str, object], expected: set[str], label: str) -> None:
    require(set(value) == expected, f"{label} fields drifted: {sorted(value)!r}")


def sha256_value(value: object, label: str) -> str:
    require(
        isinstance(value, str) and SHA256_PATTERN.fullmatch(value) is not None,
        f"{label} is not a lowercase SHA-256 digest",
    )
    return value


def integer_value(value: object, label: str) -> int:
    require(type(value) is int, f"{label} is not an integer")
    return value


def file_sha256(path: pathlib.Path) -> str:
    """Return the content identity used by manifests and installed bytes."""

    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


@dataclass(frozen=True)
class VerifiedManifest:
    """Manifest-owned facts needed by installation scenarios."""

    value: dict[str, object]
    artifact_sha256: str
    artifact_size: int
    public_contract: dict[str, object]
    supported_duckdb_identity: tuple[str, str]
    mismatched_duckdb_identity: tuple[str, str]
    source_platform: str


def duckdb_identity(
    manifest: dict[str, object], dependency: str
) -> tuple[str, str]:
    dependencies = object_value(manifest.get("dependencies"), "release dependencies")
    record = object_value(
        dependencies.get(dependency),
        f"release dependency {dependency}",
    )
    version = record.get("version")
    commit = record.get("commit")
    require(
        isinstance(version, str) and version,
        f"release dependency {dependency} has no version",
    )
    require(
        isinstance(commit, str) and COMMIT_PATTERN.fullmatch(commit) is not None,
        f"release dependency {dependency} has no full source commit",
    )
    return (f"v{version}", commit[:10])


def public_contract(
    manifest: dict[str, object], supported_identity: tuple[str, str]
) -> dict[str, object]:
    contract = object_value(manifest.get("public_contract"), "embedded public contract")
    exact_keys(
        contract,
        {
            "added_settings",
            "added_types",
            "duckdb",
            "extension",
            "function",
            "rows",
            "schema",
        },
        "embedded public contract",
    )
    require(
        contract.get("duckdb") == list(supported_identity),
        "embedded public contract DuckDB identity differs from dependencies",
    )
    require(
        contract.get("extension")
        == [EXPECTED_EXTENSION, EXPECTED_EXTENSION_VERSION],
        "embedded public contract extension identity drifted",
    )
    added_settings = contract.get("added_settings")
    require(
        isinstance(added_settings, list)
        and all(isinstance(value, str) for value in added_settings),
        "embedded public contract settings are malformed",
    )
    require(
        isinstance(contract.get("added_types"), list),
        "embedded public contract types are malformed",
    )
    function = object_value(contract.get("function"), "embedded function contract")
    exact_keys(function, {"name", "named_parameters"}, "embedded function contract")
    parameters = object_value(
        function.get("named_parameters"),
        "embedded function parameters",
    )
    require(
        isinstance(function.get("name"), str)
        and all(
            isinstance(name, str) and isinstance(data_type, str)
            for name, data_type in parameters.items()
        ),
        "embedded function contract is malformed",
    )
    rows = contract.get("rows")
    schema = contract.get("schema")
    require(
        isinstance(rows, list) and all(isinstance(row, list) for row in rows),
        "embedded public contract rows are malformed",
    )
    require(
        isinstance(schema, list)
        and all(
            isinstance(column, list)
            and len(column) == 2
            and all(isinstance(value, str) for value in column)
            for column in schema
        ),
        "embedded public contract schema is malformed",
    )
    return contract


def verify_manifest(
    manifest: dict[str, object], artifact_path: pathlib.Path
) -> VerifiedManifest:
    """Validate manifest shape and derive every Query behavior identity."""

    project = manifest.get("project")
    require(
        project
        == {
            "extension": EXPECTED_EXTENSION,
            "load_mode": "unsigned_direct_local",
            "version": EXPECTED_EXTENSION_VERSION,
        },
        f"release project identity drifted: {project!r}",
    )
    artifact = object_value(manifest.get("artifact"), "release artifact")
    expected_digest = sha256_value(
        artifact.get("sha256"),
        "release artifact digest",
    )
    expected_size = integer_value(
        artifact.get("size"),
        "release artifact size",
    )
    require(
        artifact.get("filename") == artifact_path.name,
        "release artifact filename does not match the supplied artifact",
    )
    require(
        file_sha256(artifact_path) == expected_digest,
        "supplied artifact does not match the manifest digest",
    )
    require(
        artifact_path.stat().st_size == expected_size,
        "supplied artifact does not match the manifest size",
    )

    supported_identity = duckdb_identity(manifest, "duckdb")
    mismatched_identity = duckdb_identity(manifest, "duckdb_mismatch")
    compatibility = object_value(manifest.get("compatibility"), "release compatibility")
    mismatched_host = object_value(
        compatibility.get("mismatched_host"),
        "mismatched-host compatibility",
    )
    require(
        mismatched_host.get("duckdb") == list(mismatched_identity),
        "mismatched-host compatibility differs from dependency identities",
    )
    contract = public_contract(manifest, supported_identity)
    toolchain = object_value(manifest.get("toolchain"), "release toolchain")
    source_platform = toolchain.get("duckdb_platform")
    require(
        isinstance(source_platform, str) and source_platform,
        "release toolchain has no DuckDB platform",
    )
    return VerifiedManifest(
        value=manifest,
        artifact_sha256=expected_digest,
        artifact_size=expected_size,
        public_contract=contract,
        supported_duckdb_identity=supported_identity,
        mismatched_duckdb_identity=mismatched_identity,
        source_platform=source_platform,
    )

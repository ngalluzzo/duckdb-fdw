"""Compose path admission, verification, and immutable trial inputs.

This compatibility-facing module preserves the Query package's public imports
while delegating provider execution, manifest semantics, and negative-fixture
admission to modules with independent reasons to change.
"""

from __future__ import annotations

import json
import os
import pathlib
import stat
import subprocess
from dataclasses import dataclass

from negative_fixture_admission import (
    EXTENSION_FOOTER_SIZE,
    FOOTER_FIELD_SIZE,
    NEGATIVE_FIXTURE_SCHEMA,
    PLATFORM_OFFSET_IN_FOOTER,
    canonical_json,
    verify_negative_fixtures,
)
from verified_manifest import (
    EXPECTED_EXTENSION,
    EXPECTED_EXTENSION_VERSION,
    file_sha256,
    require,
    verify_manifest,
)
from host_process import PROCESS_OUTPUT_LIMIT_BYTES
from verifier_process import VERIFIER_TIMEOUT_SECONDS, run_provider_verifier


MAX_INPUT_FILE_BYTES = 64 * 1024 * 1024


def existing_file(value: str) -> pathlib.Path:
    """Admit one bounded regular file without following a symlink leaf."""

    lexical = pathlib.Path(value).expanduser().absolute()
    require(not lexical.is_symlink(), f"input must not be a symlink leaf: {lexical}")
    metadata = lexical.stat()
    require(stat.S_ISREG(metadata.st_mode), f"input is not a regular file: {lexical}")
    require(
        0 < metadata.st_size <= MAX_INPUT_FILE_BYTES,
        f"input size is outside the 1..{MAX_INPUT_FILE_BYTES} byte bound: {lexical}",
    )
    return lexical.resolve(strict=True)


def executable_file(value: str) -> pathlib.Path:
    """Admit a pinned executable while preserving virtual-environment symlinks."""

    # Resolving a virtual-environment symlink can bypass that environment and
    # lose the pinned DuckDB package.
    path = pathlib.Path(value).expanduser().absolute()
    require(path.is_file() and os.access(path, os.X_OK), f"not executable: {path}")
    return path


@dataclass(frozen=True)
class TrialInputs:
    """The complete path-only interface supplied to Query Experience."""

    supported_python: pathlib.Path
    mismatch_python: pathlib.Path
    artifact: pathlib.Path
    manifest: pathlib.Path
    manifest_anchor: pathlib.Path
    verifier: pathlib.Path
    negative_fixture_inventory: pathlib.Path
    wrong_platform_artifact: pathlib.Path
    corrupted_artifact: pathlib.Path

    @classmethod
    def admit(
        cls,
        *,
        supported_python: str,
        mismatch_python: str,
        artifact: str,
        manifest: str,
        manifest_anchor: str,
        verifier: str,
        negative_fixture_inventory: str,
        wrong_platform_artifact: str,
        corrupted_artifact: str,
    ) -> TrialInputs:
        """Validate path shape without learning provider directory layout."""

        return cls(
            supported_python=executable_file(supported_python),
            mismatch_python=executable_file(mismatch_python),
            artifact=existing_file(artifact),
            manifest=existing_file(manifest),
            manifest_anchor=existing_file(manifest_anchor),
            verifier=existing_file(verifier),
            negative_fixture_inventory=existing_file(negative_fixture_inventory),
            wrong_platform_artifact=existing_file(wrong_platform_artifact),
            corrupted_artifact=existing_file(corrupted_artifact),
        )


@dataclass(frozen=True)
class VerifiedBundle:
    """Immutable identities established before DuckDB receives any artifact."""

    manifest: dict[str, object]
    artifact_sha256: str
    manifest_sha256: str
    verifier_sha256: str
    negative_fixture_inventory_sha256: str
    wrong_platform_artifact_sha256: str
    corrupted_artifact_sha256: str
    public_contract: dict[str, object]
    supported_duckdb_identity: tuple[str, str]
    mismatched_duckdb_identity: tuple[str, str]
    source_platform: str
    wrong_platform: str


def run_verifier(
    inputs: TrialInputs,
    artifact: pathlib.Path,
    *,
    timeout_seconds: float = VERIFIER_TIMEOUT_SECONDS,
    output_limit_bytes: int = PROCESS_OUTPUT_LIMIT_BYTES,
) -> subprocess.CompletedProcess[str]:
    """Preserve the Query verifier interface over its isolated process module."""

    return run_provider_verifier(
        verifier=inputs.verifier,
        manifest=inputs.manifest,
        artifact=artifact,
        manifest_anchor=inputs.manifest_anchor,
        release_artifact=inputs.artifact,
        wrong_platform_artifact=inputs.wrong_platform_artifact,
        corrupted_artifact=inputs.corrupted_artifact,
        timeout_seconds=timeout_seconds,
        output_limit_bytes=output_limit_bytes,
    )


def run_identity_bound_verifier(
    inputs: TrialInputs,
    artifact: pathlib.Path,
    *,
    expected_sha256: str | None = None,
) -> tuple[subprocess.CompletedProcess[str], str]:
    """Run one exchange and require one verifier identity throughout it."""

    try:
        before = file_sha256(inputs.verifier)
    except OSError as error:
        raise AssertionError("provider verifier identity is unreadable") from error
    if expected_sha256 is not None:
        require(
            before == expected_sha256,
            "provider verifier identity changed before verification",
        )
    completed = run_verifier(inputs, artifact)
    try:
        after = file_sha256(inputs.verifier)
    except OSError as error:
        raise AssertionError("provider verifier identity changed during verification") from error
    require(
        after == before,
        "provider verifier identity changed during verification",
    )
    return completed, before


def verify_trial_inputs(inputs: TrialInputs) -> VerifiedBundle:
    """Compose provider, manifest, and negative-fixture verification."""

    verified, verifier_sha256 = run_identity_bound_verifier(
        inputs,
        inputs.artifact,
    )
    require(
        verified.returncode == 0,
        "provider verifier rejected the original bundle:\n"
        f"stdout:\n{verified.stdout}\nstderr:\n{verified.stderr}",
    )
    manifest_value = json.loads(inputs.manifest.read_text(encoding="utf-8"))
    require(isinstance(manifest_value, dict), "release manifest is not an object")
    manifest = verify_manifest(manifest_value, inputs.artifact)
    fixtures = verify_negative_fixtures(
        artifact=inputs.artifact,
        inventory_path=inputs.negative_fixture_inventory,
        wrong_platform_artifact=inputs.wrong_platform_artifact,
        corrupted_artifact=inputs.corrupted_artifact,
        expected_digest=manifest.artifact_sha256,
        expected_size=manifest.artifact_size,
        source_platform=manifest.source_platform,
    )
    return VerifiedBundle(
        manifest=manifest.value,
        artifact_sha256=manifest.artifact_sha256,
        manifest_sha256=file_sha256(inputs.manifest),
        verifier_sha256=verifier_sha256,
        negative_fixture_inventory_sha256=fixtures.inventory_sha256,
        wrong_platform_artifact_sha256=(
            fixtures.wrong_platform_artifact_sha256
        ),
        corrupted_artifact_sha256=fixtures.corrupted_artifact_sha256,
        public_contract=manifest.public_contract,
        supported_duckdb_identity=manifest.supported_duckdb_identity,
        mismatched_duckdb_identity=manifest.mismatched_duckdb_identity,
        source_platform=manifest.source_platform,
        wrong_platform=fixtures.wrong_platform,
    )

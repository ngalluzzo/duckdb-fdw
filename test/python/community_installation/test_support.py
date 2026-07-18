"""Shared deterministic fixtures for Query's focused Community tests."""

from __future__ import annotations

import hashlib
import json
import pathlib
from typing import Iterable

try:
    from .input_admission import Candidate, DuckDbIdentity, admit_candidate
    from .lifecycle import ExtensionObservation, HostObservation, LifecycleError
    from .matrix import DeploymentEvidence, RowIdentity
except ImportError:
    from input_admission import Candidate, DuckDbIdentity, admit_candidate
    from lifecycle import ExtensionObservation, HostObservation, LifecycleError
    from matrix import DeploymentEvidence, RowIdentity


GIT_A = "1" * 40
GIT_B = "2" * 40
GIT_C = "08e34c447bae34eaee3723cac61f2878b6bdf787"
GIT_D = "4" * 40
GIT_E = "5" * 40
SHA_A = "a" * 64
SHA_B = "b" * 64
SHA_C = "c" * 64
SHA_D = "d" * 64


def candidate_document() -> dict[str, object]:
    return {
        "schema": "duckdb_api/community-candidate/v1",
        "status": "admitted_candidate",
        "project": {
            "extension": "duckdb_api",
            "version": "0.2.0",
            "license": "MIT",
            "repository": "https://github.com/example/duckdb-fdw",
            "tag": "v0.2.0",
            "tag_state": "pending",
        },
        "source": {"commit": GIT_A, "tree": GIT_B},
        "duckdb": {"version": "1.5.4", "commit": GIT_C},
        "community": {
            "repository": "https://github.com/duckdb/community-extensions",
            "commit": GIT_D,
            "extension_template": {
                "repository": "https://github.com/duckdb/extension-template",
                "commit": GIT_E,
            },
            "extension_ci_tools": {
                "repository": "https://github.com/duckdb/extension-ci-tools",
                "ref": "v1.5-variegata",
                "commit": GIT_A,
            },
        },
        "descriptor_expectation": {
            "status": "pending_non_authoritative",
            "sha256": SHA_A,
        },
        "dependency_audit": {
            "schema": "duckdb_api/dependency-audit/v1",
            "sha256": SHA_B,
            "anchor_sha256": SHA_C,
        },
        "pins_sha256": SHA_D,
    }


def canonical_bytes(document: object) -> bytes:
    return (json.dumps(document, sort_keys=True, indent=2) + "\n").encode()


def write_candidate(
    root: pathlib.Path, document: dict[str, object] | None = None
) -> tuple[pathlib.Path, pathlib.Path]:
    candidate = root / "candidate.json"
    anchor = root / "candidate.sha256"
    payload = canonical_bytes(document if document is not None else candidate_document())
    candidate.write_bytes(payload)
    digest = hashlib.sha256(payload).hexdigest()
    anchor.write_text(f"{digest}  candidate.json\n", encoding="ascii")
    return candidate, anchor


def admitted_candidate(root: pathlib.Path) -> Candidate:
    candidate, anchor = write_candidate(root)
    return admit_candidate(candidate, anchor)


def row(platform: str = "osx_arm64") -> RowIdentity:
    return RowIdentity(DuckDbIdentity("1.5.4", GIT_C), platform)


def public_contract() -> object:
    repository = pathlib.Path(__file__).resolve().parents[3]
    return json.loads(
        (repository / "release/0.2.0/public_contract.json").read_text(
            encoding="utf-8"
        )
    )


def extension(
    *,
    loaded: bool,
    artifact_sha256: str = SHA_A,
    install_path: str = "<extension-dir>/duckdb_api.duckdb_extension",
) -> ExtensionObservation:
    return ExtensionObservation(
        name="duckdb_api",
        version="0.2.0",
        installed=True,
        loaded=loaded,
        install_source="community",
        install_path=install_path,
        artifact_sha256=artifact_sha256,
    )


def deployment_evidence(
    candidate: Candidate, target: RowIdentity | None = None
) -> DeploymentEvidence:
    return DeploymentEvidence(
        candidate_sha256=candidate.sha256,
        row=target or row(),
        status="passed",
        channel="community",
        build_archive_sha256=SHA_C,
        unsigned_artifact_sha256=SHA_B,
        shared_payload_sha256=SHA_D,
        served_gzip_sha256=SHA_C,
        deployed_artifact_sha256=SHA_A,
        deployment_record_sha256=SHA_C,
        deployment_anchor_sha256=SHA_D,
    )


def deployment_document(candidate: Candidate) -> dict[str, object]:
    """Return one exact synthetic native deployment-provider handoff."""

    return {
        "schema": "duckdb_api/community-deployment/v1",
        "status": "deployed_candidate",
        "candidate_sha256": candidate.sha256,
        "channel": "community",
        "row": {
            "duckdb": {
                "version": candidate.duckdb.version,
                "commit": candidate.duckdb.commit,
            },
            "platform": "osx_arm64",
        },
        "build": {
            "archive_sha256": SHA_C,
            "unsigned_artifact_sha256": SHA_B,
            "shared_payload_sha256": SHA_D,
        },
        "community": {
            "repository": candidate.community_repository,
            "workflow": ".github/workflows/build.yml",
            "run_id": 12345,
            "run_attempt": 1,
            "head_commit": GIT_D,
            "endpoint": (
                "http://community-extensions.duckdb.org/v1.5.4/osx_arm64/"
                "duckdb_api.duckdb_extension.gz"
            ),
        },
        "deployment": {
            "served_gzip_sha256": SHA_C,
            "signed_artifact_sha256": SHA_A,
            "artifact_size": 4096,
        },
    }


def write_deployment(
    root: pathlib.Path,
    candidate: Candidate,
    document: dict[str, object] | None = None,
) -> tuple[pathlib.Path, pathlib.Path]:
    record = root / "deployment.json"
    anchor = root / "deployment.sha256"
    payload = canonical_bytes(document or deployment_document(candidate))
    record.write_bytes(payload)
    digest = hashlib.sha256(payload).hexdigest()
    anchor.write_text(f"{digest}  deployment.json\n", encoding="ascii")
    return record, anchor


def supported_observations(target: RowIdentity | None = None) -> list[HostObservation]:
    selected = target or row()
    return [
        HostObservation(
            action="pre_install",
            process_token="process-1",
            ok=True,
            row=selected,
            allow_unsigned_extensions=False,
            extension=None,
            function_registered=False,
        ),
        HostObservation(
            action="install",
            process_token="process-2",
            ok=True,
            row=selected,
            allow_unsigned_extensions=False,
            extension=extension(loaded=False),
            function_registered=False,
        ),
        HostObservation(
            action="repeat_install",
            process_token="process-3",
            ok=True,
            row=selected,
            allow_unsigned_extensions=False,
            extension=extension(loaded=False),
            function_registered=False,
        ),
        HostObservation(
            action="load_query",
            process_token="process-4",
            ok=True,
            row=selected,
            allow_unsigned_extensions=False,
            extension=extension(loaded=True),
            function_registered=True,
            behavior=public_contract(),
        ),
    ]


def incompatible_observation(
    target: RowIdentity | None = None,
    *,
    process_token: str = "process-negative",
) -> HostObservation:
    selected = target or row()
    return HostObservation(
        action="incompatible",
        process_token=process_token,
        ok=False,
        row=selected,
        allow_unsigned_extensions=False,
        extension=None,
        function_registered=False,
        diagnostic_category="version",
        diagnostic="artifact targets v1.5.4; current host is v1.5.3",
    )


class FakeRunner:
    """Return scripted observations while recording one call per process action."""

    def __init__(self, observations: Iterable[HostObservation]):
        self._observations = iter(observations)
        self.calls: list[tuple[str, str]] = []

    def run(self, action: str, state_id: str) -> HostObservation:
        self.calls.append((action, state_id))
        try:
            return next(self._observations)
        except StopIteration as error:
            raise AssertionError("fake host received an unexpected action") from error


class FakeInitializationProbe:
    """Deterministic independent refusal observable for scenario tests."""

    evidence_sha256 = SHA_D

    def __init__(self, *, initialized: bool = False):
        self.initialized = initialized
        self.armed = False
        self.checked = False

    def arm(self) -> None:
        self.armed = True

    def assert_not_initialized(self) -> None:
        if not self.armed:
            raise LifecycleError("initialization probe was not armed")
        self.checked = True
        if self.initialized:
            raise LifecycleError("incompatible native initializer was observed")

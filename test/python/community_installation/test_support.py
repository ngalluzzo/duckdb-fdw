"""Shared deterministic fixtures for Query's focused Community tests."""

from __future__ import annotations

import hashlib
import json
import pathlib
from typing import Iterable

try:
    from .input_admission import Candidate, DuckDbIdentity, admit_candidate
    from .lifecycle import ExtensionObservation, HostObservation
    from .matrix import RowIdentity
except ImportError:
    from input_admission import Candidate, DuckDbIdentity, admit_candidate
    from lifecycle import ExtensionObservation, HostObservation
    from matrix import RowIdentity


GIT_A = "1" * 40
GIT_B = "2" * 40
GIT_C = "3" * 40
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

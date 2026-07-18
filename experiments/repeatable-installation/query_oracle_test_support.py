"""Deterministic in-memory-sized inputs shared by focused Query tests."""

from __future__ import annotations

import hashlib
import json
import pathlib
import sys

from negative_fixture_admission import (
    EXTENSION_FOOTER_SIZE,
    FOOTER_FIELD_SIZE,
    PLATFORM_OFFSET_IN_FOOTER,
    canonical_json,
)
from trial_inputs import TrialInputs


def digest(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def footer_field(value: str) -> bytes:
    encoded = value.encode("ascii")
    return encoded + bytes(FOOTER_FIELD_SIZE - len(encoded))


def write_inventory(path: pathlib.Path, value: dict[str, object]) -> None:
    path.write_bytes(canonical_json(value))


def trial_package(
    root: pathlib.Path,
) -> tuple[TrialInputs, dict[str, object], dict[str, object]]:
    artifact_bytes = bytearray(b"a" * 1024)
    platform_offset = (
        len(artifact_bytes) - EXTENSION_FOOTER_SIZE + PLATFORM_OFFSET_IN_FOOTER
    )
    artifact_bytes[platform_offset : platform_offset + FOOTER_FIELD_SIZE] = (
        footer_field("osx_arm64")
    )
    wrong_bytes = bytearray(artifact_bytes)
    wrong_bytes[platform_offset : platform_offset + FOOTER_FIELD_SIZE] = footer_field(
        "linux_amd64"
    )
    corruption_offset = 127
    corrupted_bytes = bytearray(artifact_bytes)
    corruption_before = corrupted_bytes[corruption_offset]
    corrupted_bytes[corruption_offset] ^= 0x01

    artifact = root / "duckdb_api.duckdb_extension"
    wrong_platform = root / "wrong-platform.duckdb_extension"
    corrupted_root = root / "corrupted"
    corrupted_root.mkdir()
    corrupted = corrupted_root / "duckdb_api.duckdb_extension"
    artifact.write_bytes(artifact_bytes)
    wrong_platform.write_bytes(wrong_bytes)
    corrupted.write_bytes(corrupted_bytes)

    supported_identity = ["v1.5.4", "08e34c447b"]
    mismatched_identity = ["v1.5.3", "14eca11bd9"]
    public_contract = {
        "added_settings": [],
        "added_types": [],
        "duckdb": supported_identity,
        "extension": ["duckdb_api", "0.1.0"],
        "function": {
            "name": "duckdb_api_scan",
            "named_parameters": {
                "connector": "VARCHAR",
                "relation": "VARCHAR",
            },
        },
        "rows": [[1, "alpha", True]],
        "schema": [["id", "BIGINT"], ["name", "VARCHAR"]],
    }
    manifest_value = {
        "artifact": {
            "filename": artifact.name,
            "sha256": digest(artifact_bytes),
            "size": len(artifact_bytes),
        },
        "compatibility": {
            "mismatched_host": {
                "duckdb": mismatched_identity,
                "outcome": "rejected_before_registration",
                "registered_functions": [],
            }
        },
        "dependencies": {
            "duckdb": {
                "commit": "08e34c447bae34eaee3723cac61f2878b6bdf787",
                "version": "1.5.4",
            },
            "duckdb_mismatch": {
                "commit": "14eca11bd9d4a0de2ea0f078be588a9c1c5b279c",
                "version": "1.5.3",
            },
        },
        "project": {
            "extension": "duckdb_api",
            "load_mode": "unsigned_direct_local",
            "version": "0.1.0",
        },
        "public_contract": public_contract,
        "source": {"commit": "trial"},
        "toolchain": {"duckdb_platform": "osx_arm64"},
    }
    manifest = root / "manifest.json"
    manifest.write_text(json.dumps(manifest_value), encoding="utf-8")
    anchor = root / "manifest.sha256"
    anchor.write_text("test anchor\n", encoding="utf-8")
    verifier = root / "verifier.py"
    verifier.write_text(
        "import sys\nraise SystemExit(0 if len(sys.argv) == 4 else 2)\n",
        encoding="utf-8",
    )

    inventory_value = {
        "fixtures": {
            corrupted.name: {
                "mutation": {
                    "after": corrupted_bytes[corruption_offset],
                    "before": corruption_before,
                    "offset": corruption_offset,
                    "operation": "xor-0x01",
                    "region": "body",
                },
                "sha256": digest(corrupted_bytes),
                "size": len(corrupted_bytes),
            },
            wrong_platform.name: {
                "mutation": {
                    "after": "linux_amd64",
                    "before": "osx_arm64",
                    "offset": platform_offset,
                    "operation": "replace-zero-padded-footer-field",
                    "region": "metadata-platform",
                },
                "sha256": digest(wrong_bytes),
                "size": len(wrong_bytes),
            },
        },
        "schema": "duckdb_api/installability-negative-fixtures/v1",
        "source": {
            "filename": artifact.name,
            "sha256": digest(artifact_bytes),
            "size": len(artifact_bytes),
        },
    }
    inventory = root / "negative-fixtures.json"
    write_inventory(inventory, inventory_value)
    inputs = TrialInputs.admit(
        supported_python=sys.executable,
        mismatch_python=sys.executable,
        artifact=str(artifact),
        manifest=str(manifest),
        manifest_anchor=str(anchor),
        verifier=str(verifier),
        negative_fixture_inventory=str(inventory),
        wrong_platform_artifact=str(wrong_platform),
        corrupted_artifact=str(corrupted),
    )
    return inputs, inventory_value, public_contract

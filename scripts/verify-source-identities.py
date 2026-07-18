#!/usr/bin/env python3

from __future__ import annotations

import hashlib
import json
import pathlib
import re


ROOT = pathlib.Path(__file__).resolve().parents[1]
HISTORICAL_RELEASE_IDENTITIES = {
    "0.1.0": {
        "compiled_connector_sha256": (
            "a3f59ec862769f2e32b234500e9d95e002fb82d64fb0e4881560fb0980ce6072"
        ),
        "fixture_sha256": (
            "1b789738607ae9a74d24b07cacc09e98531c0de5ae9a822e78cabe9c8fc0eb39"
        ),
        "public_contract_sha256": (
            "a18df636619ffd09eae963cc5c6e7d3aa0670e69644380ab6a7c0fb340de2fb2"
        ),
    },
    "0.2.0": {
        "compiled_connector_sha256": (
            "a3f59ec862769f2e32b234500e9d95e002fb82d64fb0e4881560fb0980ce6072"
        ),
        "fixture_sha256": (
            "1b789738607ae9a74d24b07cacc09e98531c0de5ae9a822e78cabe9c8fc0eb39"
        ),
        "public_contract_sha256": (
            "bbba900cb94f6289c9282750ed6d15a5356f6c0de9aa00fa5ae3a0ed8e452160"
        ),
    },
}
GIT_ID = re.compile(r"[0-9a-f]{40}")
SHA256 = re.compile(r"[0-9a-f]{64}")
VERSION_PATTERN = r"(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)"
EXTENSION_CONFIG = re.compile(
    r"[ \t\r\n]*duckdb_extension_load\([ \t\r\n]*"
    r"duckdb_api[ \t\r\n]+"
    r"SOURCE_DIR[ \t\r\n]+\$\{CMAKE_CURRENT_LIST_DIR\}[ \t\r\n]+"
    rf'EXTENSION_VERSION[ \t\r\n]+"(?P<version>{VERSION_PATTERN})"[ \t\r\n]*'
    r"\)[ \t\r\n]*"
)


def digest(path: pathlib.Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def canonical_digest(value: object) -> str:
    encoded = json.dumps(value, sort_keys=True, separators=(",", ":")).encode()
    return hashlib.sha256(encoded).hexdigest()


def release_record(root: pathlib.Path, version: str) -> tuple[dict, dict]:
    release_root = root / "release" / version
    pins = json.loads((release_root / "pins.json").read_text())
    public_contract = json.loads((release_root / "public_contract.json").read_text())
    return pins, public_contract


def current_extension_version(root: pathlib.Path) -> str:
    extension_config = (root / "extension_config.cmake").read_text()
    match = EXTENSION_CONFIG.fullmatch(extension_config)
    if match is None:
        raise AssertionError(
            "extension_config.cmake does not match the sole accepted extension declaration"
        )
    return match.group("version")


def validate_project(pins: dict, version: str) -> None:
    expected = {
        "extension": "duckdb_api",
        "tag": f"v{version}",
        "version": version,
    }
    if pins.get("project") != expected:
        raise AssertionError("current project identity does not match its release pins")


def validate_duckdb(pins: dict, public_contract: dict) -> dict:
    try:
        duckdb = pins["dependencies"]["duckdb"]
        commit = duckdb["commit"]
        tree = duckdb["tree"]
        version = duckdb["version"]
        git_describe = duckdb["git_describe"]
    except (KeyError, TypeError) as error:
        raise AssertionError("current DuckDB identity is incomplete") from error
    if (
        set(duckdb) != {"commit", "git_describe", "tree", "version"}
        or not isinstance(commit, str)
        or GIT_ID.fullmatch(commit) is None
        or not isinstance(tree, str)
        or GIT_ID.fullmatch(tree) is None
        or not isinstance(version, str)
        or git_describe != f"v{version}-0-g{commit[:10]}"
    ):
        raise AssertionError("current DuckDB identity is malformed")
    if public_contract.get("duckdb") != [f"v{version}", commit[:10]]:
        raise AssertionError("current public contract names a different DuckDB identity")
    return duckdb


def validate_legacy_identities(pins: dict, version: str) -> dict:
    identities = pins.get("identities")
    expected_keys = {
        "compiled_connector_sha256",
        "fixture_sha256",
        "public_contract_sha256",
    }
    if not isinstance(identities, dict) or set(identities) != expected_keys:
        raise AssertionError(f"historical {version} source identities are incomplete")
    if any(
        not isinstance(value, str) or SHA256.fullmatch(value) is None
        for value in identities.values()
    ):
        raise AssertionError(
            f"historical {version} source identity is not a lowercase SHA-256"
        )
    return identities


def validate_current_identities(pins: dict) -> dict:
    identities = pins.get("identities")
    if not isinstance(identities, dict) or set(identities) != {
        "native_connector_source",
        "public_contract",
    }:
        raise AssertionError("current 0.3 source identity pins are incomplete")
    expected = {
        "native_connector_source": ("path", "sha256"),
        "public_contract": ("canonical_json_sha256", "path"),
    }
    for name, keys in expected.items():
        record = identities.get(name)
        if not isinstance(record, dict) or set(record) != set(keys):
            raise AssertionError(f"current 0.3 {name} identity is malformed")
        path = record.get("path")
        if (
            not isinstance(path, str)
            or pathlib.PurePosixPath(path).is_absolute()
            or ".." in pathlib.PurePosixPath(path).parts
        ):
            raise AssertionError(f"current 0.3 {name} path is not repository-relative")
        digest_key = next(key for key in keys if key != "path")
        value = record.get(digest_key)
        if not isinstance(value, str) or SHA256.fullmatch(value) is None:
            raise AssertionError(f"current 0.3 {name} digest is not a lowercase SHA-256")
    if identities["native_connector_source"]["path"] != "src/connector.cpp":
        raise AssertionError("current 0.3 connector identity names the wrong source")
    if identities["public_contract"]["path"] != "release/0.3.0/public_contract.json":
        raise AssertionError("current 0.3 public contract identity names the wrong source")
    return identities


def validate_historical_releases(root: pathlib.Path) -> dict[str, dict]:
    contracts = {}
    for version, expected_identities in HISTORICAL_RELEASE_IDENTITIES.items():
        pins, contract = release_record(root, version)
        validate_project(pins, version)
        validate_duckdb(pins, contract)
        identities = validate_legacy_identities(pins, version)
        if identities != expected_identities:
            raise AssertionError(f"historical {version} source identities drifted")
        if canonical_digest(contract) != expected_identities["public_contract_sha256"]:
            raise AssertionError(f"historical {version} public contract drifted")
        if contract.get("extension") != ["duckdb_api", version]:
            raise AssertionError(f"historical {version} public contract identity drifted")
        contracts[version] = contract

    expected_0_2 = json.loads(json.dumps(contracts["0.1.0"]))
    expected_0_2["extension"] = ["duckdb_api", "0.2.0"]
    if contracts["0.2.0"] != expected_0_2:
        raise AssertionError(
            "historical 0.2.0 public behavior differs from 0.1.0 beyond extension version"
        )
    return contracts


def verify(root: pathlib.Path = ROOT) -> dict[str, str]:
    version = current_extension_version(root)
    pins, public_contract = release_record(root, version)
    validate_project(pins, version)
    duckdb = validate_duckdb(pins, public_contract)
    if public_contract.get("extension") != ["duckdb_api", version]:
        raise AssertionError("current public contract names a different extension identity")

    validate_historical_releases(root)

    if version != "0.3.0":
        raise AssertionError("current source identity verifier supports only 0.3.0")
    identities = validate_current_identities(pins)
    connector = identities["native_connector_source"]
    connector_path = root / connector["path"]
    actual_connector = digest(connector_path)
    if actual_connector != connector["sha256"]:
        raise AssertionError(
            "native connector source digest does not match the current release pin"
        )
    public_identity = identities["public_contract"]
    actual_public_contract = canonical_digest(public_contract)
    if actual_public_contract != public_identity["canonical_json_sha256"]:
        raise AssertionError("current public contract digest does not match the release pin")
    return {
        "duckdb_commit": duckdb["commit"],
        "duckdb_version": duckdb["version"],
        "native_connector_source_sha256": actual_connector,
        "public_contract_sha256": actual_public_contract,
        "version": version,
    }


def main() -> int:
    print(json.dumps(verify(), sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

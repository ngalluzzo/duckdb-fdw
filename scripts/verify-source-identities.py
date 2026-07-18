#!/usr/bin/env python3

from __future__ import annotations

import ast
import hashlib
import json
import pathlib
import re
import sys


ROOT = pathlib.Path(__file__).resolve().parents[1]
HISTORICAL_VERSION = "0.1.0"
HISTORICAL_PUBLIC_CONTRACT_SHA256 = (
    "a18df636619ffd09eae963cc5c6e7d3aa0670e69644380ab6a7c0fb340de2fb2"
)
PRESERVED_BEHAVIOR_BASES = {"0.2.0": HISTORICAL_VERSION}
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


def embedded_fixture(header: str) -> bytes:
    match = re.search(
        r"static const char EXAMPLE_FIXTURE\[\]\s*=\s*(.*?);",
        header,
        flags=re.DOTALL,
    )
    if match is None:
        raise AssertionError("embedded fixture declaration is missing")
    literals = re.findall(r'"(?:\\.|[^"\\])*"', match.group(1))
    if not literals:
        raise AssertionError("embedded fixture contains no string literals")
    return "".join(ast.literal_eval(literal) for literal in literals).encode()


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


def validate_identities(pins: dict) -> dict:
    identities = pins.get("identities")
    expected_keys = {
        "compiled_connector_sha256",
        "fixture_sha256",
        "public_contract_sha256",
    }
    if not isinstance(identities, dict) or set(identities) != expected_keys:
        raise AssertionError("current source identity pins are incomplete")
    if any(
        not isinstance(value, str) or SHA256.fullmatch(value) is None
        for value in identities.values()
    ):
        raise AssertionError("current source identity pin is not a lowercase SHA-256")
    return identities


def validate_historical_contract(root: pathlib.Path) -> dict:
    historical_pins, historical_contract = release_record(root, HISTORICAL_VERSION)
    try:
        expected_digest = historical_pins["identities"]["public_contract_sha256"]
    except (KeyError, TypeError) as error:
        raise AssertionError("historical 0.1 public contract pin is missing") from error
    if expected_digest != HISTORICAL_PUBLIC_CONTRACT_SHA256:
        raise AssertionError(
            "historical 0.1 adjacent pin does not match the accepted canonical digest"
        )
    if canonical_digest(historical_contract) != HISTORICAL_PUBLIC_CONTRACT_SHA256:
        raise AssertionError(
            "historical 0.1 public contract does not match the accepted canonical digest"
        )
    if historical_contract.get("extension") != ["duckdb_api", HISTORICAL_VERSION]:
        raise AssertionError("historical 0.1 public contract identity drifted")
    return historical_contract


def validate_preserved_behavior(
    version: str, current_contract: dict, historical_contract: dict
) -> None:
    base_version = PRESERVED_BEHAVIOR_BASES.get(version)
    if base_version is None:
        return
    expected = json.loads(json.dumps(historical_contract))
    expected["extension"] = ["duckdb_api", version]
    if current_contract != expected:
        raise AssertionError(
            f"{version} public behavior differs from {base_version} beyond extension version"
        )


def verify(root: pathlib.Path = ROOT) -> dict[str, str]:
    version = current_extension_version(root)
    pins, public_contract = release_record(root, version)
    validate_project(pins, version)
    identities = validate_identities(pins)
    duckdb = validate_duckdb(pins, public_contract)
    if public_contract.get("extension") != ["duckdb_api", version]:
        raise AssertionError("current public contract names a different extension identity")

    historical_contract = validate_historical_contract(root)
    validate_preserved_behavior(version, public_contract, historical_contract)

    expected = identities
    actual_fixture = digest(root / "fixtures/example/items.json")
    actual_connector = digest(root / "fixtures/example/compiled_connector.snapshot")
    if actual_fixture != expected["fixture_sha256"]:
        raise AssertionError("example fixture digest does not match the current release pin")
    if actual_connector != expected["compiled_connector_sha256"]:
        raise AssertionError(
            "compiled connector snapshot digest does not match the current release pin"
        )
    if canonical_digest(public_contract) != expected["public_contract_sha256"]:
        raise AssertionError("current public contract digest does not match the release pin")

    embedded = (root / "src/include/duckdb_api/embedded_example.hpp").read_text()
    if embedded_fixture(embedded) != (root / "fixtures/example/items.json").read_bytes():
        raise AssertionError("embedded fixture bytes drifted from the tracked fixture")
    for value in (actual_fixture, actual_connector):
        if value not in embedded:
            raise AssertionError(f"embedded identity is missing {value}")

    snapshot = (root / "fixtures/example/compiled_connector.snapshot").read_text()
    if f"fixture={actual_fixture}\n" not in snapshot:
        raise AssertionError("compiled connector snapshot does not reference the fixture digest")
    return {
        "compiled_connector_sha256": actual_connector,
        "duckdb_commit": duckdb["commit"],
        "duckdb_version": duckdb["version"],
        "fixture_sha256": actual_fixture,
        "public_contract_sha256": expected["public_contract_sha256"],
        "version": version,
    }


def main() -> int:
    print(json.dumps(verify(), sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

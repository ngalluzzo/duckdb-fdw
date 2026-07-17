#!/usr/bin/env python3

from __future__ import annotations

import ast
import hashlib
import json
import pathlib
import re
import sys


ROOT = pathlib.Path(__file__).resolve().parents[1]


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


def main() -> int:
    pins = json.loads((ROOT / "release/0.1.0/pins.json").read_text())
    expected = pins["identities"]
    actual_fixture = digest(ROOT / "fixtures/example/items.json")
    actual_connector = digest(ROOT / "fixtures/example/compiled_connector.snapshot")
    public_contract = json.loads(
        (ROOT / "release/0.1.0/public_contract.json").read_text()
    )
    if actual_fixture != expected["fixture_sha256"]:
        raise AssertionError("example fixture digest does not match the release pin")
    if actual_connector != expected["compiled_connector_sha256"]:
        raise AssertionError("compiled connector snapshot digest does not match the release pin")
    if canonical_digest(public_contract) != expected["public_contract_sha256"]:
        raise AssertionError("public contract digest does not match the release pin")

    embedded = (ROOT / "src/include/duckdb_api/embedded_example.hpp").read_text()
    if embedded_fixture(embedded) != (ROOT / "fixtures/example/items.json").read_bytes():
        raise AssertionError("embedded fixture bytes drifted from the tracked fixture")
    for value in (actual_fixture, actual_connector):
        if value not in embedded:
            raise AssertionError(f"embedded identity is missing {value}")

    extension_config = (ROOT / "extension_config.cmake").read_text()
    versions = re.findall(r'EXTENSION_VERSION\s+"([^"]+)"', extension_config)
    if versions != [pins["project"]["version"]]:
        raise AssertionError(f"extension version sources drifted: {versions!r}")

    snapshot = (ROOT / "fixtures/example/compiled_connector.snapshot").read_text()
    if f"fixture={actual_fixture}\n" not in snapshot:
        raise AssertionError("compiled connector snapshot does not reference the fixture digest")
    print(
        json.dumps(
            {
                "compiled_connector_sha256": actual_connector,
                "fixture_sha256": actual_fixture,
                "version": pins["project"]["version"],
            },
            sort_keys=True,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

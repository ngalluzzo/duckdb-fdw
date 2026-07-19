#!/usr/bin/env python3

"""Verify that CMake's configured product graph matches the 0.5 release identity."""

from __future__ import annotations

import json
import pathlib
import sys


def load_object(path: pathlib.Path, label: str) -> dict:
    try:
        value = json.loads(path.read_text())
    except (OSError, json.JSONDecodeError) as error:
        raise AssertionError(f"{label} is not readable JSON: {path}") from error
    if not isinstance(value, dict):
        raise AssertionError(f"{label} root must be an object")
    return value


def translation_units(value: object, label: str) -> list[str]:
    if (
        not isinstance(value, list)
        or not value
        or any(not isinstance(path, str) or not path.endswith(".cpp") for path in value)
        or len(value) != len(set(value))
    ):
        raise AssertionError(f"{label} must be a nonempty unique translation-unit list")
    return value


def verify(pins_path: pathlib.Path, observed_path: pathlib.Path) -> dict[str, int]:
    pins = load_object(pins_path, "release pins")
    observed = load_object(observed_path, "configured product source record")
    if pins.get("project") != {
        "extension": "duckdb_api",
        "tag": "v0.5.0",
        "version": "0.5.0",
    }:
        raise AssertionError("release pins do not name the 0.5.0 product")
    try:
        expected = pins["identities"]["build_graph"]
    except (KeyError, TypeError) as error:
        raise AssertionError("release pins omit the build graph identity") from error
    if not isinstance(expected, dict) or set(expected) != {
        "controlled_translation_units",
        "public_translation_units",
    }:
        raise AssertionError("release build graph identity is malformed")
    if set(observed) != set(expected):
        raise AssertionError("configured product source record has the wrong surfaces")

    result = {}
    for key in ("public_translation_units", "controlled_translation_units"):
        expected_units = translation_units(expected[key], f"release {key}")
        observed_units = translation_units(observed[key], f"configured {key}")
        if observed_units != expected_units:
            raise AssertionError(f"configured {key} differs from the release identity")
        result[key] = len(observed_units)
    return result


def main() -> int:
    if len(sys.argv) != 3:
        raise SystemExit(
            "usage: verify-native-product-sources.py PINS CONFIGURED_PRODUCT_SOURCES"
        )
    print(
        json.dumps(
            verify(pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])),
            sort_keys=True,
        )
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as error:
        print(f"native product source verification failed: {error}", file=sys.stderr)
        raise SystemExit(1)

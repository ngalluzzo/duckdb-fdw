#!/usr/bin/env python3

from __future__ import annotations

import json
import pathlib
import sys


def main() -> int:
    if len(sys.argv) != 2:
        raise SystemExit("usage: verify-sanitizer-flags.py COMPILE_COMMANDS_JSON")
    commands = json.loads(pathlib.Path(sys.argv[1]).resolve(strict=True).read_text())
    target_sources = {
        "duckdb_api_loadable_extension": {
            "connector.cpp",
            "scan_request.cpp",
            "scan_planner.cpp",
            "execution_error.cpp",
            "fixture_decoder.cpp",
            "fixture_runtime.cpp",
            "example_composition.cpp",
            "duckdb_api_extension.cpp",
        },
        "duckdb_api_connector_tests": {"connector.cpp"},
        "duckdb_api_scan_planner_tests": {
            "connector.cpp",
            "scan_request.cpp",
            "scan_planner.cpp",
        },
        "duckdb_api_fixture_decoder_tests": {
            "connector.cpp",
            "scan_request.cpp",
            "scan_planner.cpp",
            "execution_error.cpp",
            "fixture_decoder.cpp",
            "fixture_runtime.cpp",
            "fixture_scenarios.cpp",
        },
        "duckdb_api_fixture_stream_tests": {
            "connector.cpp",
            "scan_request.cpp",
            "scan_planner.cpp",
            "execution_error.cpp",
            "fixture_decoder.cpp",
            "fixture_runtime.cpp",
            "fixture_scenarios.cpp",
        },
        "duckdb_api_adapter_tests": {
            "connector.cpp",
            "scan_request.cpp",
            "scan_planner.cpp",
            "execution_error.cpp",
            "fixture_decoder.cpp",
            "fixture_runtime.cpp",
            "example_composition.cpp",
            "duckdb_api_extension.cpp",
            "fixture_scenarios.cpp",
        },
    }
    production_sources = set().union(*target_sources.values())
    production = [
        entry
        for entry in commands
        if pathlib.Path(entry["file"]).name in production_sources
    ]
    if not production:
        raise AssertionError(
            "sanitizer compile database contains no production extension objects"
        )
    expected = {
        (target, source)
        for target, sources in target_sources.items()
        for source in sources
    }
    observed: set[tuple[str, str]] = set()
    for entry in production:
        command = entry["command"]
        output = entry.get("output", "")
        source = pathlib.Path(entry["file"]).name
        for target in target_sources:
            if f"/{target}.dir/" in output:
                observed.add((target, source))
        if "-fsanitize=address" not in command:
            raise AssertionError("production extension object is missing ASan")
        if "-fsanitize=undefined" not in command:
            raise AssertionError("production extension object is missing UBSan")
        if "-std=c++11" not in command:
            raise AssertionError("production extension object is not C++11")
    if observed != expected:
        raise AssertionError(
            f"sanitizer compile database production target/source set drifted: {observed!r}"
        )
    print("production extension objects contain ASan, UBSan, and C++11 flags")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

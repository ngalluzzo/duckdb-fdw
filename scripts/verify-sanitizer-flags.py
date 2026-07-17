#!/usr/bin/env python3

from __future__ import annotations

import json
import pathlib
import sys


def main() -> int:
    if len(sys.argv) != 2:
        raise SystemExit("usage: verify-sanitizer-flags.py COMPILE_COMMANDS_JSON")
    commands = json.loads(pathlib.Path(sys.argv[1]).resolve(strict=True).read_text())
    production = [
        entry
        for entry in commands
        if "/src/duckdb_api_" in entry["file"]
    ]
    if not production:
        raise AssertionError(
            "sanitizer compile database contains no production extension objects"
        )
    expected = {
        (target, source)
        for target in ("duckdb_api_loadable_extension", "duckdb_api_contract_tests")
        for source in ("duckdb_api_core.cpp", "duckdb_api_extension.cpp")
    }
    observed: set[tuple[str, str]] = set()
    for entry in production:
        command = entry["command"]
        output = entry.get("output", "")
        source = pathlib.Path(entry["file"]).name
        for target in ("duckdb_api_loadable_extension", "duckdb_api_contract_tests"):
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

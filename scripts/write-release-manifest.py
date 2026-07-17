#!/usr/bin/env python3

from __future__ import annotations

import hashlib
import json
import os
import pathlib
import platform
import re
import subprocess
import sys


def sha256(path: pathlib.Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def command(*args: str) -> str:
    return subprocess.check_output(args, text=True).strip()


def cache_value(cache: pathlib.Path, name: str) -> str:
    pattern = re.compile(rf"^{re.escape(name)}(?::[^=]+)?=(.*)$")
    for line in cache.read_text().splitlines():
        match = pattern.match(line)
        if match:
            return match.group(1)
    raise AssertionError(f"CMake cache does not contain {name}")


def main() -> int:
    if len(sys.argv) != 6:
        raise SystemExit(
            "usage: write-release-manifest.py REPOSITORY BUILD_ROOT ARTIFACT BEHAVIOR_JSON OUTPUT"
        )
    repository = pathlib.Path(sys.argv[1]).resolve(strict=True)
    build_root = pathlib.Path(sys.argv[2]).resolve(strict=True)
    artifact = pathlib.Path(sys.argv[3]).resolve(strict=True)
    behavior_path = pathlib.Path(sys.argv[4]).resolve(strict=True)
    output = pathlib.Path(sys.argv[5]).resolve()
    pins = json.loads((repository / "release/0.1.0/pins.json").read_text())
    behavior_record = json.loads(behavior_path.read_text())
    cache = build_root / "extension-template/build/release/CMakeCache.txt"
    compiler = pathlib.Path(cache_value(cache, "CMAKE_CXX_COMPILER"))
    compiler_version = command(str(compiler), "--version").splitlines()[0]
    cmake = build_root / "tools/cmake/CMake.app/Contents/bin/cmake"
    ninja = build_root / "tools/ninja/ninja"

    manifest = {
        "schema": "duckdb_api/release-evidence/v1",
        "cell": "osx_arm64",
        "source": {
            "commit": command("git", "-C", str(repository), "rev-parse", "HEAD"),
            "tag": pins["project"]["tag"],
        },
        "project": {
            "extension": pins["project"]["extension"],
            "version": pins["project"]["version"],
            "load_mode": "unsigned_direct_local",
        },
        "dependencies": pins["dependencies"],
        "toolchain": {
            "architecture": platform.machine(),
            "build_profile": "release",
            "cmake": command(str(cmake), "--version").splitlines()[0],
            "compiler_path": str(compiler),
            "compiler_version": compiler_version,
            "cxx_standard": cache_value(cache, "CMAKE_CXX_STANDARD"),
            "duckdb_platform": "osx_arm64",
            "host": f"macOS {platform.mac_ver()[0]}",
            "ninja": command(str(ninja), "--version"),
            "sanitizers": {"address": False, "undefined": False},
        },
        "content": {
            "compiled_connector_sha256": sha256(
                repository / "fixtures/example/compiled_connector.snapshot"
            ),
            "fixture_sha256": sha256(repository / "fixtures/example/items.json"),
        },
        "public_contract": behavior_record["behavior"],
        "public_contract_sha256": behavior_record["behavior_sha256"],
        "artifact": {
            "filename": artifact.name,
            "sha256": sha256(artifact),
            "size": artifact.stat().st_size,
        },
        "environment": {
            "locale": os.environ.get("LC_ALL", os.environ.get("LANG", "")),
        },
    }
    output.parent.mkdir(parents=True, exist_ok=False)
    output.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n")
    print(output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env python3

from __future__ import annotations

import hashlib
import json
import pathlib
import platform
import subprocess
import sys


def sha256(path: pathlib.Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def git(repository: pathlib.Path, revision: str) -> str:
    return subprocess.check_output(
        ["git", "-C", str(repository), "rev-parse", revision], text=True
    ).strip()


def main() -> int:
    if len(sys.argv) != 8:
        raise SystemExit(
            "usage: write-sanitizer-envelope.py REPOSITORY EVIDENCE_ROOT "
            "DOCKER_INSPECT_JSON DOCKER_INFO_JSON DOCKER_CONTEXT "
            "DOCKER_ENDPOINT OUTPUT"
        )
    repository = pathlib.Path(sys.argv[1]).resolve(strict=True)
    evidence = pathlib.Path(sys.argv[2]).resolve(strict=True)
    inspect_path = pathlib.Path(sys.argv[3]).resolve(strict=True)
    daemon_path = pathlib.Path(sys.argv[4]).resolve(strict=True)
    context = sys.argv[5]
    endpoint = sys.argv[6]
    output = pathlib.Path(sys.argv[7]).resolve()
    pins = json.loads((repository / "release/0.1.0/pins.json").read_text())
    image = json.loads(inspect_path.read_text())
    if not isinstance(image, list) or len(image) != 1:
        raise AssertionError("Docker inspect did not return exactly one image")
    record = image[0]
    daemon = json.loads(daemon_path.read_text())
    required_image = pins["sanitizer_cell"]["base_image"]
    required_digest = required_image.split("@", 1)[1]
    repo_digests = record.get("RepoDigests", [])
    if not any(value.endswith(f"@{required_digest}") for value in repo_digests):
        raise AssertionError("inspected Docker image is not the release pin")
    if record.get("Os") != "linux" or record.get("Architecture") != "amd64":
        raise AssertionError("inspected Docker image platform drifted")
    if platform.system() != "Linux" or platform.machine() != "x86_64":
        raise AssertionError("sanitizer envelope requires a native Linux x86_64 host")
    if context != "default" or not endpoint.startswith("unix://"):
        raise AssertionError("sanitizer envelope requires the local default Docker context")
    if daemon.get("OSType") != "linux" or daemon.get("Architecture") != "x86_64":
        raise AssertionError("sanitizer envelope requires a Linux x86_64 Docker daemon")

    manifest = evidence / "manifest.json"
    artifact = evidence / "duckdb_api.duckdb_extension"
    compile_commands = evidence / "compile_commands.json"
    flags_report = evidence / "sanitizer-flags.txt"
    tag = pins["project"]["tag"]
    envelope = {
        "schema": "duckdb_api/sanitizer-launcher-envelope/v1",
        "cell": pins["sanitizer_cell"]["name"],
        "source": {
            "commit": git(repository, f"{tag}^{{commit}}"),
            "tree": git(repository, f"{tag}^{{tree}}"),
            "tag": tag,
        },
        "executor": {
            "client_host_os": platform.system(),
            "client_host_architecture": platform.machine(),
            "docker_context": context,
            "docker_endpoint": endpoint,
            "daemon_os": daemon["OSType"],
            "daemon_architecture": daemon["Architecture"],
            "container_platform": pins["sanitizer_cell"]["platform"],
        },
        "image": {
            "requested": required_image,
            "id": record["Id"],
            "repo_digests": sorted(repo_digests),
            "os": record["Os"],
            "architecture": record["Architecture"],
        },
        "inner_evidence": {
            "manifest_sha256": sha256(manifest),
            "artifact_sha256": sha256(artifact),
            "compile_commands_sha256": sha256(compile_commands),
            "sanitizer_flags_report_sha256": sha256(flags_report),
        },
    }
    output.write_text(json.dumps(envelope, indent=2, sort_keys=True) + "\n")
    print(output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

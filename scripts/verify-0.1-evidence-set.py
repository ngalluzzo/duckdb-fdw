#!/usr/bin/env python3

from __future__ import annotations

import hashlib
import json
import pathlib
import subprocess
import sys


def sha256(path: pathlib.Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def anchored(path: pathlib.Path, anchor: pathlib.Path, label: str) -> None:
    if sha256(path) != anchor.read_text().split()[0]:
        raise AssertionError(f"{label} anchor mismatch")


def git(repository: pathlib.Path, revision: str) -> str:
    return subprocess.check_output(
        ["git", "-C", str(repository), "rev-parse", revision], text=True
    ).strip()


def main() -> int:
    if len(sys.argv) != 4:
        raise SystemExit(
            "usage: verify-0.1-evidence-set.py PRODUCT_EVIDENCE_ROOT "
            "SANITIZER_EVIDENCE_ROOT REPOSITORY"
        )
    product_root = pathlib.Path(sys.argv[1]).resolve(strict=True)
    sanitizer_root = pathlib.Path(sys.argv[2]).resolve(strict=True)
    repository = pathlib.Path(sys.argv[3]).resolve(strict=True)
    product_manifest = product_root / "manifest/manifest.json"
    product_anchor = product_root / "manifest/manifest.sha256"
    product_artifact = product_root / "duckdb_api.duckdb_extension"
    sanitizer_manifest = sanitizer_root / "manifest.json"
    sanitizer_anchor = sanitizer_root / "manifest.sha256"
    sanitizer_artifact = sanitizer_root / "duckdb_api.duckdb_extension"
    compile_commands = sanitizer_root / "compile_commands.json"
    flags_report = sanitizer_root / "sanitizer-flags.txt"
    envelope_path = sanitizer_root / "envelope.json"
    envelope_anchor = sanitizer_root / "envelope.sha256"
    for path in (
        product_manifest,
        product_anchor,
        product_artifact,
        sanitizer_manifest,
        sanitizer_anchor,
        sanitizer_artifact,
        compile_commands,
        flags_report,
        envelope_path,
        envelope_anchor,
    ):
        path.resolve(strict=True)

    subprocess.run(
        [
            sys.executable,
            "-I",
            str(repository / "scripts/verify-release-manifest.py"),
            str(repository),
            str(product_manifest),
            str(product_artifact),
            str(product_anchor),
        ],
        check=True,
        stdout=subprocess.DEVNULL,
    )
    subprocess.run(
        [
            sys.executable,
            "-I",
            str(repository / "scripts/verify-sanitizer-manifest.py"),
            str(repository),
            str(sanitizer_manifest),
            str(sanitizer_anchor),
            str(sanitizer_artifact),
            str(compile_commands),
            str(flags_report),
        ],
        check=True,
        stdout=subprocess.DEVNULL,
    )
    anchored(envelope_path, envelope_anchor, "sanitizer launcher envelope")

    product = json.loads(product_manifest.read_text())
    sanitizer = json.loads(sanitizer_manifest.read_text())
    envelope = json.loads(envelope_path.read_text())
    pins = json.loads((repository / "release/0.1.0/pins.json").read_text())
    tag = pins["project"]["tag"]
    canonical_source = {
        "commit": git(repository, f"{tag}^{{commit}}"),
        "tree": git(repository, f"{tag}^{{tree}}"),
        "tag": tag,
    }
    if git(repository, "HEAD") != canonical_source["commit"] or git(
        repository, "HEAD^{tree}"
    ) != canonical_source["tree"]:
        raise AssertionError("evidence verification repository is not at the canonical tag")
    if product["source"] != canonical_source:
        raise AssertionError("product evidence is not bound to the canonical release tag")
    if {key: sanitizer["source"][key] for key in canonical_source} != canonical_source:
        raise AssertionError("sanitizer evidence is not bound to the canonical release tag")
    if envelope.get("source") != canonical_source:
        raise AssertionError("sanitizer launcher is not bound to the canonical release tag")
    if product["dependencies"] != pins["dependencies"] or sanitizer[
        "dependencies"
    ] != pins["dependencies"]:
        raise AssertionError("evidence-set dependency identities drifted")
    expected_content = {
        "compiled_connector_sha256": pins["identities"]["compiled_connector_sha256"],
        "fixture_sha256": pins["identities"]["fixture_sha256"],
    }
    if product["content"] != expected_content or sanitizer["content"] != expected_content:
        raise AssertionError("evidence-set content identities drifted")

    if envelope.get("schema") != "duckdb_api/sanitizer-launcher-envelope/v1":
        raise AssertionError("sanitizer launcher envelope schema drifted")
    if envelope.get("cell") != pins["sanitizer_cell"]["name"]:
        raise AssertionError("sanitizer launcher cell drifted")
    executor = envelope["executor"]
    if (
        executor.get("client_host_os") != "Linux"
        or executor.get("client_host_architecture") != "x86_64"
        or executor.get("docker_context") != "default"
        or not executor.get("docker_endpoint", "").startswith("unix://")
        or executor.get("daemon_os") != "linux"
        or executor.get("daemon_architecture") != "x86_64"
        or executor.get("container_platform") != pins["sanitizer_cell"]["platform"]
    ):
        raise AssertionError(
            "sanitizer launcher did not record a local native Linux x86_64 daemon"
        )
    image = envelope["image"]
    required_image = pins["sanitizer_cell"]["base_image"]
    required_digest = required_image.split("@", 1)[1]
    if (
        image.get("requested") != required_image
        or image.get("os") != "linux"
        or image.get("architecture") != "amd64"
        or not any(
            value.endswith(f"@{required_digest}")
            for value in image.get("repo_digests", [])
        )
    ):
        raise AssertionError("sanitizer launcher image identity drifted")
    if envelope["inner_evidence"] != {
        "manifest_sha256": sha256(sanitizer_manifest),
        "artifact_sha256": sha256(sanitizer_artifact),
        "compile_commands_sha256": sha256(compile_commands),
        "sanitizer_flags_report_sha256": sha256(flags_report),
    }:
        raise AssertionError("sanitizer launcher envelope does not bind the retained evidence")
    print("0.1.0 product and Docker sanitizer evidence are bound to the canonical tag")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

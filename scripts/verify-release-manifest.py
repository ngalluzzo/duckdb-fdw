#!/usr/bin/env python3

from __future__ import annotations

import hashlib
import json
import pathlib
import subprocess
import sys


def sha256(path: pathlib.Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def canonical_digest(value: object) -> str:
    encoded = json.dumps(value, sort_keys=True, separators=(",", ":")).encode()
    return hashlib.sha256(encoded).hexdigest()


def main() -> int:
    if len(sys.argv) not in (4, 5):
        raise SystemExit(
            "usage: verify-release-manifest.py REPOSITORY MANIFEST ARTIFACT [MANIFEST_SHA256_FILE]"
        )
    repository = pathlib.Path(sys.argv[1]).resolve(strict=True)
    manifest_path = pathlib.Path(sys.argv[2]).resolve(strict=True)
    artifact = pathlib.Path(sys.argv[3]).resolve(strict=True)
    if len(sys.argv) == 5:
        anchor = pathlib.Path(sys.argv[4]).resolve(strict=True).read_text().split()[0]
        if sha256(manifest_path) != anchor:
            raise AssertionError("release manifest does not match its anchored digest")

    manifest = json.loads(manifest_path.read_text())
    pins = json.loads((repository / "release/0.1.0/pins.json").read_text())
    expected_contract = json.loads(
        (repository / "release/0.1.0/public_contract.json").read_text()
    )
    if manifest["schema"] != "duckdb_api/release-evidence/v1":
        raise AssertionError("release manifest schema drifted")
    if manifest["cell"] != "osx_arm64":
        raise AssertionError("release product cell drifted")
    tag = pins["project"]["tag"]
    tag_commit = subprocess.check_output(
        ["git", "-C", str(repository), "rev-parse", f"{tag}^{{commit}}"], text=True
    ).strip()
    tag_tree = subprocess.check_output(
        ["git", "-C", str(repository), "rev-parse", f"{tag}^{{tree}}"], text=True
    ).strip()
    head_commit = subprocess.check_output(
        ["git", "-C", str(repository), "rev-parse", "HEAD"], text=True
    ).strip()
    head_tree = subprocess.check_output(
        ["git", "-C", str(repository), "rev-parse", "HEAD^{tree}"], text=True
    ).strip()
    if head_commit != tag_commit or head_tree != tag_tree:
        raise AssertionError("release verification repository is not at the canonical tag")
    if subprocess.check_output(
        [
            "git",
            "-C",
            str(repository),
            "status",
            "--porcelain",
            "--untracked-files=all",
        ],
        text=True,
    ):
        raise AssertionError("release verification repository is not clean")
    if manifest["source"] != {"commit": tag_commit, "tree": tag_tree, "tag": tag}:
        raise AssertionError("release source identity drifted")
    if manifest["project"] != {
        "extension": pins["project"]["extension"],
        "load_mode": "unsigned_direct_local",
        "version": pins["project"]["version"],
    }:
        raise AssertionError("release project identity drifted")
    if manifest["dependencies"] != pins["dependencies"]:
        raise AssertionError("release dependency identities drifted")
    mismatch = pins["dependencies"]["duckdb_mismatch"]
    if manifest.get("compatibility", {}).get("mismatched_host") != {
        "duckdb": [f"v{mismatch['version']}", mismatch["commit"][:10]],
        "outcome": "rejected_before_registration",
        "registered_functions": [],
    }:
        raise AssertionError("release mismatch-host evidence drifted")
    if manifest["artifact"]["filename"] != artifact.name:
        raise AssertionError("release artifact filename drifted")
    if manifest["artifact"]["sha256"] != sha256(artifact):
        raise AssertionError("release artifact checksum mismatch")
    if manifest["artifact"]["size"] != artifact.stat().st_size:
        raise AssertionError("release artifact size mismatch")
    expected_content = {
        "compiled_connector_sha256": sha256(
            repository / "fixtures/example/compiled_connector.snapshot"
        ),
        "fixture_sha256": sha256(repository / "fixtures/example/items.json"),
    }
    pinned_content = {
        "compiled_connector_sha256": pins["identities"]["compiled_connector_sha256"],
        "fixture_sha256": pins["identities"]["fixture_sha256"],
    }
    if expected_content != pinned_content or manifest["content"] != expected_content:
        raise AssertionError("release content identities drifted")
    if manifest["public_contract"] != expected_contract:
        raise AssertionError("release public behavior contract drifted")
    if manifest["public_contract_sha256"] != pins["identities"][
        "public_contract_sha256"
    ] or manifest["public_contract_sha256"] != canonical_digest(expected_contract):
        raise AssertionError("release public behavior identity mismatch")
    product_cell = pins["product_cell"]
    toolchain = manifest["toolchain"]
    if toolchain["architecture"] != product_cell["architecture"]:
        raise AssertionError("release architecture drifted")
    if toolchain["build_profile"] != "release":
        raise AssertionError("release build profile drifted")
    if toolchain["cmake"] != f"cmake version {product_cell['cmake']}":
        raise AssertionError("release CMake identity drifted")
    if not toolchain["compiler_version"].startswith(product_cell["compiler"]):
        raise AssertionError("release compiler identity drifted")
    if toolchain["cxx_standard"] != "11":
        raise AssertionError("release C++ mode drifted")
    if toolchain["duckdb_platform"] != product_cell["duckdb_platform"]:
        raise AssertionError("release DuckDB platform drifted")
    if toolchain["host"] != product_cell["host"]:
        raise AssertionError("release host identity drifted")
    if toolchain["ninja"] != product_cell["ninja"]:
        raise AssertionError("release Ninja identity drifted")
    if toolchain["sanitizers"] != {
        "address": False,
        "undefined": False,
    }:
        raise AssertionError("product cell sanitizer declaration drifted")
    print("release manifest and artifact verification passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

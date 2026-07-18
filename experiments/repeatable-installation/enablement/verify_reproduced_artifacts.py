#!/usr/bin/env python3
"""Verify and compare two independent v0.1.0 reproduction evidence roots."""

from __future__ import annotations

import hashlib
import importlib.util
import json
import pathlib
import sys


HERE = pathlib.Path(__file__).resolve().parent


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def load_bundle_verifier():
    path = HERE / "verify_bundle.py"
    spec = importlib.util.spec_from_file_location("installability_bundle_verifier", path)
    require(spec is not None and spec.loader is not None, "cannot load bundle verifier")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def sha256(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def canonical_root(value: str, label: str) -> pathlib.Path:
    lexical = pathlib.Path(value).expanduser()
    if not lexical.is_absolute():
        lexical = pathlib.Path.cwd() / lexical
    require(not lexical.is_symlink(), f"{label} must not be a symlink leaf: {lexical}")
    root = lexical.resolve(strict=True)
    require(root.is_dir(), f"{label} is not a directory: {root}")
    require(
        {path.name for path in root.iterdir()} == {"duckdb_api.duckdb_extension", "manifest"},
        f"{label} top-level inventory drifted",
    )
    manifest_root = root / "manifest"
    require(
        manifest_root.is_dir()
        and {path.name for path in manifest_root.iterdir()} == {"manifest.json", "manifest.sha256"},
        f"{label} manifest inventory drifted",
    )
    for path in root.rglob("*"):
        require(not path.is_symlink(), f"{label} contains a symlink: {path.relative_to(root)}")
    return root


def equal_bytes(left: pathlib.Path, right: pathlib.Path) -> bool:
    if left.stat().st_size != right.stat().st_size:
        return False
    with left.open("rb") as left_file, right.open("rb") as right_file:
        while True:
            left_block = left_file.read(1024 * 1024)
            right_block = right_file.read(1024 * 1024)
            if left_block != right_block:
                return False
            if not left_block:
                return True


def verify_evidence(
    verifier, root: pathlib.Path, label: str, trusted_artifact_sha256: str
) -> dict[str, object]:
    artifact = root / "duckdb_api.duckdb_extension"
    manifest = root / "manifest/manifest.json"
    anchor = root / "manifest/manifest.sha256"
    verifier.verify(
        manifest,
        artifact,
        anchor,
        require_trusted_manifest=False,
        require_trusted_artifact=False,
    )
    artifact_digest = sha256(artifact)
    return {
        "artifact_sha256": artifact_digest,
        "artifact_size": artifact.stat().st_size,
        "label": label,
        "manifest_sha256": sha256(manifest),
        "matches_trusted_artifact": artifact_digest == trusted_artifact_sha256,
    }


def main() -> int:
    if len(sys.argv) != 3:
        raise SystemExit(
            "usage: verify_reproduced_artifacts.py REPRODUCTION_EVIDENCE_ONE "
            "REPRODUCTION_EVIDENCE_TWO"
        )
    try:
        verifier = load_bundle_verifier()
        one = canonical_root(sys.argv[1], "first reproduction evidence")
        two = canonical_root(sys.argv[2], "second reproduction evidence")
        require(one != two, "reproduction evidence roots must be distinct")
        trust = verifier.load_trust()
        trusted_artifact_sha256 = trust["artifact"]["sha256"]
        first = verify_evidence(verifier, one, "one", trusted_artifact_sha256)
        second = verify_evidence(verifier, two, "two", trusted_artifact_sha256)
        first_artifact = one / "duckdb_api.duckdb_extension"
        second_artifact = two / "duckdb_api.duckdb_extension"
        artifacts_byte_identical = equal_bytes(first_artifact, second_artifact)
        result = {
            "all_match_trusted_artifact": (
                first["matches_trusted_artifact"] and second["matches_trusted_artifact"]
            ),
            "artifacts_byte_identical": artifacts_byte_identical,
            "evidence": [first, second],
            "schema": "duckdb_api/reproduced-artifacts/v1",
            "source": trust["source"],
            "trusted_artifact_sha256": trusted_artifact_sha256,
        }
    except AssertionError as error:
        print(f"reproduced artifact verification failed: {error}", file=sys.stderr)
        return 1
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

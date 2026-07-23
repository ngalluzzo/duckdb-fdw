#!/usr/bin/env python3
"""Verify immutable RFC evidence against anchors outside mutable manifests."""

from __future__ import annotations

import hashlib
import importlib.util
import json
import os
import pathlib
import re
import stat
import sys


AUTHORITY_PATH = pathlib.Path(__file__).with_name("rfc_evidence_authorities.py")
AUTHORITY_SPEC = importlib.util.spec_from_file_location("rfc_evidence_authorities", AUTHORITY_PATH)
if AUTHORITY_SPEC is None or AUTHORITY_SPEC.loader is None:
    raise RuntimeError(f"cannot load RFC evidence authority: {AUTHORITY_PATH}")
AUTHORITY_MODULE = importlib.util.module_from_spec(AUTHORITY_SPEC)
AUTHORITY_SPEC.loader.exec_module(AUTHORITY_MODULE)
AUTHORITIES = AUTHORITY_MODULE.AUTHORITIES
CURRENT_CONNECTOR_LANGUAGE_AUTHORITY = AUTHORITY_MODULE.CURRENT_CONNECTOR_LANGUAGE_AUTHORITY


SAFE_RELATIVE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9_./-]*$")


def sha256(path: pathlib.Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def reject_duplicate_keys(pairs: list[tuple[str, object]]) -> dict[str, object]:
    result: dict[str, object] = {}
    for key, value in pairs:
        if key in result:
            raise AssertionError(f"duplicate JSON key: {key}")
        result[key] = value
    return result


def load_json(path: pathlib.Path) -> dict[str, object]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"), object_pairs_hook=reject_duplicate_keys)
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise AssertionError(f"RFC evidence JSON cannot be read: {path}") from error
    if not isinstance(value, dict):
        raise AssertionError(f"RFC evidence JSON root is not an object: {path}")
    return value


def checked_relative(relative: str) -> pathlib.PurePosixPath:
    if not SAFE_RELATIVE.fullmatch(relative):
        raise AssertionError(f"invalid RFC evidence path: {relative!r}")
    path = pathlib.PurePosixPath(relative)
    if path.is_absolute() or any(part in ("", ".", "..") for part in path.parts):
        raise AssertionError(f"invalid RFC evidence path: {relative!r}")
    return path


def require_regular(path: pathlib.Path, label: str) -> None:
    try:
        mode = path.lstat().st_mode
    except OSError as error:
        raise AssertionError(f"{label} cannot be read: {path}") from error
    if stat.S_ISLNK(mode):
        raise AssertionError(f"{label} is a symlink: {path}")
    if not stat.S_ISREG(mode):
        raise AssertionError(f"{label} is not a regular file: {path}")


def inventory(directory: pathlib.Path) -> list[str]:
    result: list[str] = []
    for root, directories, files in os.walk(directory, followlinks=False):
        root_path = pathlib.Path(root)
        for name in directories:
            child = root_path / name
            if child.is_symlink():
                raise AssertionError(f"RFC evidence inventory contains a symlink: {child}")
        for name in files:
            child = root_path / name
            require_regular(child, "RFC evidence artifact")
            result.append(child.relative_to(directory).as_posix())
    return sorted(result)


def verify_authority(repository: pathlib.Path, rfc: str, authority: dict[str, object]) -> dict[str, object]:
    directory = repository / str(authority["directory"])
    manifest_path = directory / "evidence-manifest.json"
    require_regular(manifest_path, "RFC evidence manifest")
    observed_manifest_digest = sha256(manifest_path)
    expected_manifest_digest = str(authority["manifest_sha256"])
    if observed_manifest_digest != expected_manifest_digest:
        raise AssertionError(
            f"RFC {rfc} evidence manifest digest differs: expected {expected_manifest_digest}, "
            f"got {observed_manifest_digest}"
        )

    manifest = load_json(manifest_path)
    if set(manifest) != {"manifest", "artifacts", "verifier"}:
        raise AssertionError(f"RFC {rfc} evidence manifest fields differ")
    if manifest["manifest"] != authority["manifest_contract"]:
        raise AssertionError(f"RFC {rfc} evidence manifest contract differs")
    if manifest["verifier"] != authority["verifier_record"]:
        raise AssertionError(f"RFC {rfc} evidence verifier record differs")
    artifacts = manifest["artifacts"]
    if not isinstance(artifacts, dict) or not artifacts:
        raise AssertionError(f"RFC {rfc} evidence artifact map is empty or malformed")

    expected_inventory = ["evidence-manifest.json"]
    for relative, expected_digest in artifacts.items():
        if not isinstance(relative, str) or not isinstance(expected_digest, str):
            raise AssertionError(f"RFC {rfc} evidence artifact record is malformed")
        checked_relative(relative)
        if not re.fullmatch(r"sha256\.[0-9a-f]{64}", expected_digest):
            raise AssertionError(f"RFC {rfc} evidence artifact digest is malformed: {relative}")
        artifact = directory / relative
        require_regular(artifact, "RFC evidence artifact")
        observed = f"sha256.{sha256(artifact)}"
        if observed != expected_digest:
            raise AssertionError(
                f"RFC {rfc} evidence artifact digest differs for {relative}: "
                f"expected {expected_digest}, got {observed}"
            )
        expected_inventory.append(relative)

    if inventory(directory) != sorted(expected_inventory):
        raise AssertionError(f"RFC {rfc} evidence inventory differs")

    verifier_record = manifest["verifier"]
    if isinstance(verifier_record, dict) and "digest" in verifier_record:
        verifier_path = repository / str(verifier_record["path"])
        require_regular(verifier_path, "RFC evidence verifier")
        observed = f"sha256.{sha256(verifier_path)}"
        if observed != verifier_record["digest"]:
            raise AssertionError(f"RFC {rfc} evidence verifier digest differs")

    mirrors = authority["production_mirrors"]
    if not isinstance(mirrors, dict):
        raise AssertionError(f"RFC {rfc} production mirror authority is malformed")
    for relative, production_relative in mirrors.items():
        checked_relative(str(relative))
        checked_relative(str(production_relative))
        evidence_path = directory / str(relative)
        production_path = repository / str(production_relative)
        require_regular(production_path, "RFC evidence production mirror")
        if evidence_path.read_bytes() != production_path.read_bytes():
            raise AssertionError(f"RFC {rfc} production/current-authority mirror differs: {relative}")

    return {"rfc": rfc, "manifest_sha256": observed_manifest_digest, "artifacts": len(artifacts)}


def verify(repository: pathlib.Path) -> dict[str, object]:
    results = {
        rfc: verify_authority(repository, rfc, authority)
        for rfc, authority in AUTHORITIES.items()
    }
    if CURRENT_CONNECTOR_LANGUAGE_AUTHORITY is not None:
        if CURRENT_CONNECTOR_LANGUAGE_AUTHORITY not in AUTHORITIES:
            raise AssertionError("current connector-language evidence authority is unknown")
        if not AUTHORITIES[CURRENT_CONNECTOR_LANGUAGE_AUTHORITY]["production_mirrors"]:
            raise AssertionError("current connector-language evidence authority has no production mirrors")
    return {"authorities": results, "current": CURRENT_CONNECTOR_LANGUAGE_AUTHORITY}


def main() -> None:
    repository = pathlib.Path(__file__).resolve().parents[1]
    print(json.dumps(verify(repository), sort_keys=True, separators=(",", ":")))


if __name__ == "__main__":
    try:
        main()
    except AssertionError as error:
        print(f"RFC evidence verification failed: {error}", file=sys.stderr)
        raise SystemExit(1)

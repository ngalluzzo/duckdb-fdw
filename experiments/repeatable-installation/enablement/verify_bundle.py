#!/usr/bin/env python3
"""Verify one immutable v0.1.0 artifact triple for the installation trial.

This verifier intentionally validates release source files through ``git show``
at the immutable tag.  Unlike the release gate, it does not require the active
worktree to be checked out at that tag or to be clean.
"""

from __future__ import annotations

import hashlib
import json
import pathlib
import re
import subprocess
import sys
from collections.abc import Mapping


REPOSITORY = pathlib.Path(__file__).resolve().parents[3]
TRUST_PATH = pathlib.Path(__file__).resolve().with_name("trusted-release.json")
EXPECTED_TOP_LEVEL = {
    "artifact",
    "cell",
    "compatibility",
    "content",
    "dependencies",
    "environment",
    "project",
    "public_contract",
    "public_contract_sha256",
    "schema",
    "source",
    "toolchain",
}


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def sha256_bytes(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def sha256(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def canonical_digest(value: object) -> str:
    encoded = json.dumps(value, sort_keys=True, separators=(",", ":")).encode()
    return sha256_bytes(encoded)


def git(*arguments: str, binary: bool = False) -> str | bytes:
    completed = subprocess.run(
        ["git", "-C", str(REPOSITORY), *arguments],
        check=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=not binary,
    )
    if binary:
        return completed.stdout
    return completed.stdout.strip()


def tagged_bytes(commit: str, relative_path: str) -> bytes:
    value = git("show", f"{commit}:{relative_path}", binary=True)
    require(isinstance(value, bytes), "git returned text for a tagged blob")
    return value


def tagged_json(commit: str, relative_path: str) -> dict[str, object]:
    value = json.loads(tagged_bytes(commit, relative_path))
    require(isinstance(value, dict), f"tagged {relative_path} is not an object")
    return value


def regular_input(value: str, label: str) -> pathlib.Path:
    lexical = pathlib.Path(value).expanduser()
    if not lexical.is_absolute():
        lexical = pathlib.Path.cwd() / lexical
    require(not lexical.is_symlink(), f"{label} must not be a symlink leaf: {lexical}")
    parent = lexical.parent.resolve(strict=True)
    path = parent / lexical.name
    require(path.is_file(), f"{label} is not a regular file: {path}")
    return path


def load_trust() -> dict[str, object]:
    trust = json.loads(TRUST_PATH.read_text(encoding="utf-8"))
    require(
        isinstance(trust, dict)
        and set(trust) == {"artifact", "manifest", "schema", "source"},
        "tracked release trust record drifted",
    )
    require(
        trust["schema"] == "duckdb_api/installability-trusted-release/v1",
        "tracked release trust schema drifted",
    )
    return trust


def verify_trusted_source(trust: dict[str, object]) -> dict[str, str]:
    source = trust["source"]
    require(isinstance(source, dict), "tracked source identity is not an object")
    required = {"commit", "tag", "tag_object", "tree"}
    require(set(source) == required and all(isinstance(source[key], str) for key in required),
            "tracked source identity drifted")
    tag = source["tag"]
    require(
        git("rev-parse", f"refs/tags/{tag}^{{tag}}") == source["tag_object"],
        "local release tag object does not match the tracked trust record",
    )
    require(
        git("rev-parse", f"refs/tags/{tag}^{{commit}}") == source["commit"],
        "local release tag commit does not match the tracked trust record",
    )
    require(
        git("rev-parse", f"{source['commit']}^{{tree}}") == source["tree"],
        "trusted release commit tree does not match the tracked trust record",
    )
    return source  # type: ignore[return-value]


def verify_anchor(
    anchor: pathlib.Path,
    manifest_path: pathlib.Path,
    expected_digest: str,
    expected_filename: str,
    expected_anchor_filename: str,
) -> None:
    require(anchor.name == expected_anchor_filename, "manifest anchor filename drifted")
    match = re.fullmatch(
        r"([0-9a-f]{64})  ([^\x00\r\n]+)\n",
        anchor.read_text(encoding="utf-8"),
    )
    require(match is not None, "manifest anchor syntax drifted")
    assert match is not None
    require(match.group(1) == expected_digest, "manifest anchor digest drifted")
    require(
        pathlib.PurePath(match.group(2)).name == expected_filename,
        "manifest anchor target filename drifted",
    )
    require(sha256(manifest_path) == match.group(1), "release manifest anchor mismatch")


def require_exact_mapping(
    actual: object, expected: Mapping[str, object], label: str
) -> None:
    require(actual == expected, f"{label} drifted: {actual!r}")


def verify(
    manifest_path: pathlib.Path,
    artifact: pathlib.Path,
    anchor: pathlib.Path,
    *,
    require_trusted_manifest: bool = True,
    require_trusted_artifact: bool = True,
) -> None:
    trust = load_trust()
    source = verify_trusted_source(trust)
    trusted_manifest = trust["manifest"]
    trusted_artifact = trust["artifact"]
    require(isinstance(trusted_manifest, dict), "tracked manifest identity is not an object")
    require(isinstance(trusted_artifact, dict), "tracked artifact identity is not an object")
    manifest_digest = sha256(manifest_path)
    require(manifest_path.name == trusted_manifest["filename"], "release manifest filename drifted")
    if require_trusted_manifest:
        require(
            manifest_path.stat().st_size == trusted_manifest["size"]
            and manifest_digest == trusted_manifest["sha256"],
            "release manifest does not match the tracked trust record",
        )
    require(artifact.name == trusted_artifact["filename"], "release artifact filename drifted")
    if require_trusted_artifact:
        require(
            artifact.stat().st_size == trusted_artifact["size"]
            and sha256(artifact) == trusted_artifact["sha256"],
            "release artifact does not match the tracked trust record",
        )
    verify_anchor(
        anchor,
        manifest_path,
        str(trusted_manifest["sha256"] if require_trusted_manifest else manifest_digest),
        str(trusted_manifest["filename"]),
        str(trusted_manifest["anchor_filename"]),
    )

    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    require(isinstance(manifest, dict), "release manifest is not an object")
    require(set(manifest) == EXPECTED_TOP_LEVEL, "release manifest inventory drifted")

    commit = source["commit"]
    tag = source["tag"]
    pins = tagged_json(commit, "release/0.1.0/pins.json")
    project = pins["project"]
    require(isinstance(project, dict), "tagged project pin is not an object")
    require(project.get("tag") == tag, "tagged release does not identify v0.1.0")
    require(manifest["schema"] == "duckdb_api/release-evidence/v1", "schema drifted")
    require(manifest["cell"] == "osx_arm64", "product cell drifted")
    require_exact_mapping(
        manifest["source"],
        {"commit": source["commit"], "tag": tag, "tree": source["tree"]},
        "release source identity",
    )
    require_exact_mapping(
        manifest["project"],
        {
            "extension": project["extension"],
            "load_mode": "unsigned_direct_local",
            "version": project["version"],
        },
        "release project identity",
    )
    require(manifest["dependencies"] == pins["dependencies"], "dependency identities drifted")

    dependencies = pins["dependencies"]
    require(isinstance(dependencies, dict), "tagged dependency pins are not an object")
    mismatch = dependencies["duckdb_mismatch"]
    require(isinstance(mismatch, dict), "mismatch dependency pin is not an object")
    require_exact_mapping(
        manifest["compatibility"],
        {
            "mismatched_host": {
                "duckdb": [f"v{mismatch['version']}", str(mismatch["commit"])[:10]],
                "outcome": "rejected_before_registration",
                "registered_functions": [],
            }
        },
        "mismatch-host evidence",
    )

    require_exact_mapping(
        manifest["artifact"],
        {
            "filename": artifact.name,
            "sha256": sha256(artifact),
            "size": artifact.stat().st_size,
        },
        "release artifact identity",
    )

    expected_content = {
        "compiled_connector_sha256": sha256_bytes(
            tagged_bytes(commit, "fixtures/example/compiled_connector.snapshot")
        ),
        "fixture_sha256": sha256_bytes(tagged_bytes(commit, "fixtures/example/items.json")),
    }
    identities = pins["identities"]
    require(isinstance(identities, dict), "tagged identities are not an object")
    require(
        expected_content
        == {name: identities[name] for name in expected_content},
        "tagged content pins drifted",
    )
    require(manifest["content"] == expected_content, "release content identities drifted")

    expected_contract = tagged_json(commit, "release/0.1.0/public_contract.json")
    require(manifest["public_contract"] == expected_contract, "public contract drifted")
    expected_contract_digest = canonical_digest(expected_contract)
    require(
        manifest["public_contract_sha256"]
        == identities["public_contract_sha256"]
        == expected_contract_digest,
        "public contract identity drifted",
    )

    product_cell = pins["product_cell"]
    require(isinstance(product_cell, dict), "tagged product cell is not an object")
    toolchain = manifest["toolchain"]
    require(isinstance(toolchain, dict), "release toolchain is not an object")
    require(
        set(toolchain)
        == {
            "architecture",
            "build_profile",
            "cmake",
            "compiler_path",
            "compiler_version",
            "cxx_standard",
            "duckdb_platform",
            "host",
            "ninja",
            "sanitizers",
        },
        "release toolchain inventory drifted",
    )
    require(toolchain["architecture"] == product_cell["architecture"], "architecture drifted")
    require(toolchain["build_profile"] == "release", "build profile drifted")
    require(toolchain["cmake"] == f"cmake version {product_cell['cmake']}", "CMake drifted")
    require(
        isinstance(toolchain["compiler_path"], str)
        and pathlib.PurePosixPath(toolchain["compiler_path"]).is_absolute(),
        "compiler path is not absolute",
    )
    require(
        isinstance(toolchain["compiler_version"], str)
        and toolchain["compiler_version"].startswith(str(product_cell["compiler"])),
        "compiler identity drifted",
    )
    require(toolchain["cxx_standard"] == "11", "C++ mode drifted")
    require(toolchain["duckdb_platform"] == product_cell["duckdb_platform"], "platform drifted")
    require(toolchain["host"] == product_cell["host"], "host identity drifted")
    require(toolchain["ninja"] == product_cell["ninja"], "Ninja identity drifted")
    require_exact_mapping(
        toolchain["sanitizers"],
        {"address": False, "undefined": False},
        "product sanitizer declaration",
    )


def main() -> int:
    if len(sys.argv) != 4:
        raise SystemExit("usage: verify_bundle.py MANIFEST ARTIFACT MANIFEST_ANCHOR")
    try:
        manifest = regular_input(sys.argv[1], "manifest")
        artifact = regular_input(sys.argv[2], "artifact")
        anchor = regular_input(sys.argv[3], "manifest anchor")
        verify(manifest, artifact, anchor)
    except AssertionError as error:
        print(f"bundle verification failed: {error}", file=sys.stderr)
        return 1
    print("immutable v0.1.0 bundle verification passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

"""Shared deterministic inputs and process helpers for Enablement tests."""

from __future__ import annotations

import hashlib
import json
import pathlib
import subprocess
import sys
import unittest


HERE = pathlib.Path(__file__).resolve().parent
REPOSITORY = HERE.parents[2]
TAG = "v0.1.0"
REAL_EVIDENCE = REPOSITORY / ".build/release-v0.1.0-evidence-f855dfb"
REPRODUCTION_ROOT = REPOSITORY / ".build/reproduction-v0.1.0-f855dfb"


def sha256_bytes(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def canonical_digest(value: object) -> str:
    encoded = json.dumps(value, sort_keys=True, separators=(",", ":")).encode()
    return sha256_bytes(encoded)


def git(*arguments: str, binary: bool = False) -> str | bytes:
    completed = subprocess.run(
        ["git", "-C", str(REPOSITORY), *arguments],
        check=True,
        stdout=subprocess.PIPE,
        text=not binary,
    )
    return completed.stdout if binary else completed.stdout.strip()


def tagged_bytes(relative_path: str) -> bytes:
    value = git("show", f"{TAG}:{relative_path}", binary=True)
    assert isinstance(value, bytes)
    return value


def tagged_json(relative_path: str) -> dict[str, object]:
    value = json.loads(tagged_bytes(relative_path))
    assert isinstance(value, dict)
    return value


def synthetic_extension() -> bytes:
    footer = bytearray(512)

    def set_field(offset: int, value: bytes) -> None:
        footer[offset : offset + 32] = value + bytes(32 - len(value))

    set_field(96, b"CPP")
    set_field(128, b"0.1.0")
    set_field(160, b"v1.5.4")
    set_field(192, b"osx_arm64")
    set_field(224, b"4")
    return bytes(range(256)) * 8 + footer


def release_manifest(artifact: pathlib.Path) -> dict[str, object]:
    pins = tagged_json("release/0.1.0/pins.json")
    contract = tagged_json("release/0.1.0/public_contract.json")
    dependencies = pins["dependencies"]
    assert isinstance(dependencies, dict)
    mismatch = dependencies["duckdb_mismatch"]
    assert isinstance(mismatch, dict)
    product_cell = pins["product_cell"]
    assert isinstance(product_cell, dict)
    return {
        "artifact": {
            "filename": artifact.name,
            "sha256": sha256_bytes(artifact.read_bytes()),
            "size": artifact.stat().st_size,
        },
        "cell": "osx_arm64",
        "compatibility": {
            "mismatched_host": {
                "duckdb": [f"v{mismatch['version']}", str(mismatch["commit"])[:10]],
                "outcome": "rejected_before_registration",
                "registered_functions": [],
            }
        },
        "content": {
            "compiled_connector_sha256": sha256_bytes(
                tagged_bytes("fixtures/example/compiled_connector.snapshot")
            ),
            "fixture_sha256": sha256_bytes(tagged_bytes("fixtures/example/items.json")),
        },
        "dependencies": dependencies,
        "environment": {"locale": "C.UTF-8"},
        "project": {
            "extension": "duckdb_api",
            "load_mode": "unsigned_direct_local",
            "version": "0.1.0",
        },
        "public_contract": contract,
        "public_contract_sha256": canonical_digest(contract),
        "schema": "duckdb_api/release-evidence/v1",
        "source": {
            "commit": git("rev-parse", f"{TAG}^{{commit}}"),
            "tag": TAG,
            "tree": git("rev-parse", f"{TAG}^{{tree}}"),
        },
        "toolchain": {
            "architecture": product_cell["architecture"],
            "build_profile": "release",
            "cmake": f"cmake version {product_cell['cmake']}",
            "compiler_path": "/usr/bin/c++",
            "compiler_version": product_cell["compiler"],
            "cxx_standard": "11",
            "duckdb_platform": product_cell["duckdb_platform"],
            "host": product_cell["host"],
            "ninja": product_cell["ninja"],
            "sanitizers": {"address": False, "undefined": False},
        },
    }


def write_release_triple(
    root: pathlib.Path,
) -> tuple[pathlib.Path, pathlib.Path, pathlib.Path]:
    artifact = root / "duckdb_api.duckdb_extension"
    artifact.write_bytes(synthetic_extension())
    manifest = root / "manifest.json"
    manifest.write_text(
        json.dumps(release_manifest(artifact), indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    anchor = root / "manifest.sha256"
    anchor.write_text(
        f"{sha256_bytes(manifest.read_bytes())}  manifest.json\n", encoding="utf-8"
    )
    return manifest, artifact, anchor


class EnablementTestCase(unittest.TestCase):
    """Stable subprocess and retained-evidence helpers for provider oracles."""

    maxDiff = None

    def run_script(
        self, script: str, *arguments: pathlib.Path | str
    ) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [sys.executable, "-I", str(HERE / script), *(str(value) for value in arguments)],
            cwd=REPOSITORY,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            check=False,
        )

    def real_triple(self) -> tuple[pathlib.Path, pathlib.Path, pathlib.Path]:
        manifest = REAL_EVIDENCE / "manifest/manifest.json"
        artifact = REAL_EVIDENCE / "duckdb_api.duckdb_extension"
        anchor = REAL_EVIDENCE / "manifest/manifest.sha256"
        if not all(path.is_file() for path in (manifest, artifact, anchor)):
            self.skipTest("retained local v0.1.0 evidence is unavailable")
        return manifest, artifact, anchor

    def assemble_real_bundle(self, output: pathlib.Path) -> pathlib.Path:
        manifest, artifact, anchor = self.real_triple()
        completed = self.run_script(
            "assemble_bundle.py",
            "--artifact",
            artifact,
            "--manifest",
            manifest,
            "--manifest-anchor",
            anchor,
            "--output",
            output,
        )
        self.assertEqual(completed.returncode, 0, completed.stderr)
        return output

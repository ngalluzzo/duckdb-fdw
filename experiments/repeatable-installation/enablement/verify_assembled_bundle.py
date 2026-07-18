#!/usr/bin/env python3
"""Verify the exact five-file installation bundle and its trusted payload."""

from __future__ import annotations

import hashlib
import json
import pathlib
import re
import subprocess
import sys


HERE = pathlib.Path(__file__).resolve().parent
PAYLOAD_FILES = {
    "duckdb_api.duckdb_extension",
    "manifest.json",
    "manifest.sha256",
}
ALL_FILES = PAYLOAD_FILES | {"bundle.json", "bundle.sha256"}


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def sha256(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def canonical_json(value: object) -> bytes:
    return (json.dumps(value, indent=2, sort_keys=True) + "\n").encode()


def canonical_root(value: str) -> pathlib.Path:
    lexical = pathlib.Path(value).expanduser()
    if not lexical.is_absolute():
        lexical = pathlib.Path.cwd() / lexical
    require(not lexical.is_symlink(), f"bundle root must not be a symlink leaf: {lexical}")
    root = lexical.resolve(strict=True)
    require(root.is_dir(), f"bundle root is not a directory: {root}")
    return root


def verify(root: pathlib.Path) -> None:
    entries = set()
    for path in root.iterdir():
        require(not path.is_symlink(), f"bundle contains a symlink: {path.name}")
        require(path.is_file(), f"bundle contains a non-file: {path.name}")
        entries.add(path.name)
    require(entries == ALL_FILES, "bundle directory inventory drifted")

    trust = json.loads((HERE / "trusted-release.json").read_text(encoding="utf-8"))
    expected_manifest_anchor = f"{trust['manifest']['sha256']}  manifest.json\n"
    require(
        (root / "manifest.sha256").read_text(encoding="utf-8")
        == expected_manifest_anchor,
        "bundle manifest anchor is not the normalized relative record",
    )

    completed = subprocess.run(
        [
            sys.executable,
            "-I",
            str(HERE / "verify_bundle.py"),
            str(root / "manifest.json"),
            str(root / "duckdb_api.duckdb_extension"),
            str(root / "manifest.sha256"),
        ],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        check=False,
    )
    require(
        completed.returncode == 0,
        "trusted release triple failed: " + (completed.stderr.strip() or completed.stdout.strip()),
    )

    inventory_path = root / "bundle.json"
    anchor_text = (root / "bundle.sha256").read_text(encoding="utf-8")
    anchor = re.fullmatch(r"([0-9a-f]{64})  bundle\.json\n", anchor_text)
    require(anchor is not None, "bundle anchor syntax or target drifted")
    assert anchor is not None
    require(anchor.group(1) == sha256(inventory_path), "bundle inventory anchor mismatch")

    inventory = json.loads(inventory_path.read_text(encoding="utf-8"))
    require(canonical_json(inventory) == inventory_path.read_bytes(), "bundle inventory is not canonical")
    require(
        isinstance(inventory, dict) and set(inventory) == {"files", "release", "schema"},
        "bundle inventory shape drifted",
    )
    require(inventory["schema"] == "duckdb_api/installability-bundle/v1", "schema drifted")
    files = inventory["files"]
    require(isinstance(files, dict) and set(files) == PAYLOAD_FILES, "payload map drifted")
    for name in sorted(PAYLOAD_FILES):
        path = root / name
        require(
            files[name] == {"sha256": sha256(path), "size": path.stat().st_size},
            f"bundle payload identity drifted for {name}",
        )

    manifest = json.loads((root / "manifest.json").read_text(encoding="utf-8"))
    require(
        inventory["release"]
        == {
            "cell": manifest["cell"],
            "dependencies": {"duckdb": manifest["dependencies"]["duckdb"]},
            "input_manifest_sha256": sha256(root / "manifest.json"),
            "project": manifest["project"],
            "source": manifest["source"],
        },
        "bundle release summary drifted",
    )


def main() -> int:
    if len(sys.argv) != 2:
        raise SystemExit("usage: verify_assembled_bundle.py BUNDLE_ROOT")
    try:
        verify(canonical_root(sys.argv[1]))
    except AssertionError as error:
        print(f"assembled bundle verification failed: {error}", file=sys.stderr)
        return 1
    print("exact installation bundle verification passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

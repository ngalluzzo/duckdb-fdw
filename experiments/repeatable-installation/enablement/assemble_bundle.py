#!/usr/bin/env python3
"""Assemble a deterministic installation-trial bundle from verified inputs."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import pathlib
import shutil
import subprocess
import sys
from collections.abc import Sequence


HERE = pathlib.Path(__file__).resolve().parent
OUTPUT_NAMES = {
    "artifact": "duckdb_api.duckdb_extension",
    "manifest": "manifest.json",
    "anchor": "manifest.sha256",
}


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


def regular_input(value: str, label: str) -> pathlib.Path:
    lexical = pathlib.Path(value).expanduser()
    if not lexical.is_absolute():
        lexical = pathlib.Path.cwd() / lexical
    require(not lexical.is_symlink(), f"{label} must not be a symlink leaf: {lexical}")
    path = lexical.parent.resolve(strict=True) / lexical.name
    require(path.is_file(), f"{label} is not a regular file: {path}")
    return path


def new_output(value: str) -> pathlib.Path:
    lexical = pathlib.Path(value).expanduser()
    if not lexical.is_absolute():
        lexical = pathlib.Path.cwd() / lexical
    require(not lexical.exists() and not lexical.is_symlink(), f"output already exists: {lexical}")
    nearest = lexical.parent
    missing: list[str] = []
    while not nearest.exists() and not nearest.is_symlink():
        missing.append(nearest.name)
        nearest = nearest.parent
    require(not nearest.is_symlink(), f"output parent must not be a symlink leaf: {nearest}")
    require(nearest.is_dir(), f"output parent is not a directory: {nearest}")
    canonical_parent = nearest.resolve(strict=True)
    for name in reversed(missing):
        canonical_parent /= name
    canonical_parent.mkdir(parents=True, exist_ok=True)
    canonical_parent = canonical_parent.resolve(strict=True)
    output = canonical_parent / lexical.name
    require(not output.exists() and not output.is_symlink(), f"output already exists: {output}")
    return output


def parse_args(argv: Sequence[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Assemble one deterministic trial bundle.")
    parser.add_argument("--artifact", required=True)
    parser.add_argument("--manifest", required=True)
    parser.add_argument("--manifest-anchor", required=True)
    parser.add_argument("--output", required=True)
    return parser.parse_args(argv)


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(argv)
    inputs = {
        "artifact": regular_input(args.artifact, "artifact"),
        "manifest": regular_input(args.manifest, "manifest"),
        "anchor": regular_input(args.manifest_anchor, "manifest anchor"),
    }
    require(
        inputs["artifact"].name == OUTPUT_NAMES["artifact"],
        "artifact must retain the canonical release filename",
    )
    subprocess.run(
        [
            sys.executable,
            "-I",
            str(HERE / "verify_bundle.py"),
            str(inputs["manifest"]),
            str(inputs["artifact"]),
            str(inputs["anchor"]),
        ],
        check=True,
    )

    output = new_output(args.output)
    output.mkdir(mode=0o755)

    try:
        copied: dict[str, pathlib.Path] = {}
        for role in ("artifact", "manifest", "anchor"):
            destination = output / OUTPUT_NAMES[role]
            if role == "anchor":
                trust = json.loads((HERE / "trusted-release.json").read_text(encoding="utf-8"))
                destination.write_text(
                    f"{trust['manifest']['sha256']}  manifest.json\n", encoding="utf-8"
                )
            else:
                shutil.copyfile(inputs[role], destination)
            os.chmod(destination, 0o444)
            copied[role] = destination

        release_manifest = json.loads(copied["manifest"].read_text(encoding="utf-8"))
        inventory = {
            "files": {
                path.name: {"sha256": sha256(path), "size": path.stat().st_size}
                for path in sorted(copied.values(), key=lambda item: item.name)
            },
            "release": {
                "cell": release_manifest["cell"],
                "dependencies": {
                    "duckdb": release_manifest["dependencies"]["duckdb"],
                },
                "input_manifest_sha256": sha256(copied["manifest"]),
                "project": release_manifest["project"],
                "source": release_manifest["source"],
            },
            "schema": "duckdb_api/installability-bundle/v1",
        }
        inventory_path = output / "bundle.json"
        inventory_path.write_bytes(canonical_json(inventory))
        anchor_path = output / "bundle.sha256"
        anchor_path.write_text(
            f"{sha256(inventory_path)}  {inventory_path.name}\n", encoding="utf-8"
        )
        os.chmod(inventory_path, 0o444)
        os.chmod(anchor_path, 0o444)

        completed = subprocess.run(
            [
                sys.executable,
                "-I",
                str(HERE / "verify_assembled_bundle.py"),
                str(output),
            ],
            check=False,
        )
        require(completed.returncode == 0, "assembled bundle verification failed")
    except BaseException:
        # A partial output is never a valid bundle. Leave it visibly incomplete
        # for diagnosis rather than making later callers mistake it for success.
        (output / ".incomplete").touch(exist_ok=True)
        raise

    print(output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

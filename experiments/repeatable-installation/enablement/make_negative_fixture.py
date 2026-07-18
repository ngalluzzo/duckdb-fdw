#!/usr/bin/env python3
"""Create deterministic wrong-platform and body-corrupted trial artifacts."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import pathlib
from collections.abc import Sequence


FOOTER_SIZE = 512
FIELD_SIZE = 32
PLATFORM_OFFSET_IN_FOOTER = 192
MAGIC_OFFSET_IN_FOOTER = 224
SIGNATURE_OFFSET_IN_FOOTER = 256
EXPECTED_PLATFORM = b"osx_arm64"
WRONG_PLATFORM = b"linux_amd64"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def sha256_bytes(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def canonical_json(value: object) -> bytes:
    return (json.dumps(value, indent=2, sort_keys=True) + "\n").encode()


def field(value: bytes) -> bytes:
    require(len(value) <= FIELD_SIZE, "extension footer field is too long")
    return value + bytes(FIELD_SIZE - len(value))


def parse_args(argv: Sequence[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Create deterministic rejection fixtures.")
    parser.add_argument("--artifact", required=True)
    parser.add_argument("--output", required=True)
    return parser.parse_args(argv)


def regular_input(value: str) -> pathlib.Path:
    lexical = pathlib.Path(value).expanduser()
    if not lexical.is_absolute():
        lexical = pathlib.Path.cwd() / lexical
    require(not lexical.is_symlink(), f"artifact must not be a symlink leaf: {lexical}")
    artifact = lexical.parent.resolve(strict=True) / lexical.name
    require(artifact.is_file(), f"artifact is not a regular file: {artifact}")
    return artifact


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
    return canonical_parent.resolve(strict=True) / lexical.name


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(argv)
    artifact = regular_input(args.artifact)
    original = artifact.read_bytes()
    require(len(original) > FOOTER_SIZE + 1, "artifact is too small to contain a body")

    footer_start = len(original) - FOOTER_SIZE
    platform_offset = footer_start + PLATFORM_OFFSET_IN_FOOTER
    magic_offset = footer_start + MAGIC_OFFSET_IN_FOOTER
    signature_offset = footer_start + SIGNATURE_OFFSET_IN_FOOTER
    require(
        original[platform_offset : platform_offset + FIELD_SIZE]
        == field(EXPECTED_PLATFORM),
        "source artifact is not the pinned osx_arm64 extension",
    )
    require(
        original[magic_offset : magic_offset + FIELD_SIZE] == field(b"4"),
        "source artifact does not carry the expected DuckDB footer magic",
    )
    require(signature_offset == len(original) - 256, "footer layout calculation drifted")

    output = new_output(args.output)
    output.mkdir(mode=0o755)

    wrong_platform = bytearray(original)
    wrong_platform[platform_offset : platform_offset + FIELD_SIZE] = field(WRONG_PLATFORM)
    corrupted = bytearray(original)
    corruption_offset = footer_start // 2
    require(corruption_offset < footer_start, "corruption offset entered the footer")
    corruption_before = corrupted[corruption_offset]
    corrupted[corruption_offset] ^= 0x01

    wrong_path = output / "wrong-platform.duckdb_extension"
    corrupted_root = output / "corrupted"
    corrupted_root.mkdir(mode=0o755)
    # Preserve the release filename so the bundle verifier reaches the byte
    # identity check instead of rejecting this fixture on its path name.
    corrupted_path = corrupted_root / artifact.name
    wrong_path.write_bytes(wrong_platform)
    corrupted_path.write_bytes(corrupted)
    inventory = {
        "fixtures": {
            corrupted_path.name: {
                "mutation": {
                    "after": corrupted[corruption_offset],
                    "before": corruption_before,
                    "offset": corruption_offset,
                    "operation": "xor-0x01",
                    "region": "body",
                },
                "sha256": sha256_bytes(corrupted),
                "size": len(corrupted),
            },
            wrong_path.name: {
                "mutation": {
                    "after": WRONG_PLATFORM.decode(),
                    "before": EXPECTED_PLATFORM.decode(),
                    "offset": platform_offset,
                    "operation": "replace-zero-padded-footer-field",
                    "region": "metadata-platform",
                },
                "sha256": sha256_bytes(wrong_platform),
                "size": len(wrong_platform),
            },
        },
        "schema": "duckdb_api/installability-negative-fixtures/v1",
        "source": {
            "filename": artifact.name,
            "sha256": sha256_bytes(original),
            "size": len(original),
        },
    }
    inventory_path = output / "negative-fixtures.json"
    inventory_path.write_bytes(canonical_json(inventory))
    for path in (wrong_path, corrupted_path, inventory_path):
        os.chmod(path, 0o444)

    print(output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

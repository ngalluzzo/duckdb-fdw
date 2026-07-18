#!/usr/bin/env python3
"""Self-contained verifier for the one immutable v0.1.0 trial bundle.

Query snapshots this file and runs the exact same verifier bytes for original
and corrupted inputs. The constants are intentionally duplicated from the
tracked trust record and checked against it by provider tests.
"""

from __future__ import annotations

import hashlib
import pathlib
import re
import sys


MANIFEST_NAME = "manifest.json"
MANIFEST_ANCHOR_NAME = "manifest.sha256"
MANIFEST_SHA256 = "764be0f79b373c53f61926e96dd5b56ca51d1c775cbdd949d85a30ac58b8a4f9"
MANIFEST_SIZE = 2863
ARTIFACT_NAME = "duckdb_api.duckdb_extension"
ARTIFACT_SHA256 = "4f1a0678fd2a673b433af6248a34966cb8fd41d107d4c0b3b97ca71eb35179ea"
ARTIFACT_SIZE = 4859678


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def regular_file(value: str, expected_name: str, label: str) -> pathlib.Path:
    path = pathlib.Path(value).expanduser().resolve(strict=True)
    require(path.is_file(), f"{label} is not a regular file")
    require(path.name == expected_name, f"{label} filename drifted")
    return path


def sha256(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def verify(manifest: pathlib.Path, artifact: pathlib.Path, anchor: pathlib.Path) -> None:
    require(
        manifest.stat().st_size == MANIFEST_SIZE
        and sha256(manifest) == MANIFEST_SHA256,
        "release manifest does not match the tracked trial authority",
    )
    require(
        artifact.stat().st_size == ARTIFACT_SIZE
        and sha256(artifact) == ARTIFACT_SHA256,
        "release artifact does not match the tracked trust record",
    )
    match = re.fullmatch(
        r"([0-9a-f]{64})  ([^\x00\r\n]+)\n",
        anchor.read_text(encoding="utf-8"),
    )
    require(match is not None, "manifest anchor syntax drifted")
    assert match is not None
    require(match.group(1) == MANIFEST_SHA256, "manifest anchor digest drifted")
    require(
        pathlib.PurePath(match.group(2)).name == MANIFEST_NAME,
        "manifest anchor target filename drifted",
    )


def main() -> int:
    if len(sys.argv) != 4:
        raise SystemExit(
            "usage: verify_trial_bundle.py MANIFEST ARTIFACT MANIFEST_ANCHOR"
        )
    try:
        manifest = regular_file(sys.argv[1], MANIFEST_NAME, "release manifest")
        artifact = regular_file(sys.argv[2], ARTIFACT_NAME, "release artifact")
        anchor = regular_file(
            sys.argv[3],
            MANIFEST_ANCHOR_NAME,
            "manifest anchor",
        )
        verify(manifest, artifact, anchor)
    except AssertionError as error:
        print(f"bundle verification failed: {error}", file=sys.stderr)
        return 1
    print("immutable v0.1.0 trial bundle verification passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env python3
"""Verify that the local release ref still names the tracked trial source."""

from __future__ import annotations

import json
import pathlib
import subprocess
import sys


HERE = pathlib.Path(__file__).resolve().parent
TRUST_PATH = HERE / "trusted-release.json"
EXPECTED_KEYS = {"artifact", "manifest", "schema", "source"}


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def load_trust() -> dict[str, object]:
    trust = json.loads(TRUST_PATH.read_text(encoding="utf-8"))
    require(isinstance(trust, dict) and set(trust) == EXPECTED_KEYS, "trust record drifted")
    require(
        trust["schema"] == "duckdb_api/installability-trusted-release/v1",
        "trust record schema drifted",
    )
    return trust


def git(repository: pathlib.Path, revision: str) -> str:
    return subprocess.check_output(
        ["git", "-C", str(repository), "rev-parse", revision],
        text=True,
        stderr=subprocess.DEVNULL,
    ).strip()


def verify_repository(repository: pathlib.Path, trust: dict[str, object]) -> None:
    source = trust["source"]
    require(isinstance(source, dict), "trusted source identity is not an object")
    tag = source["tag"]
    require(isinstance(tag, str), "trusted tag name is not text")
    require(
        git(repository, f"refs/tags/{tag}^{{tag}}") == source["tag_object"],
        "local release tag object does not match the tracked trust record",
    )
    require(
        git(repository, f"refs/tags/{tag}^{{commit}}") == source["commit"],
        "local release tag commit does not match the tracked trust record",
    )
    require(
        git(repository, f"{source['commit']}^{{tree}}") == source["tree"],
        "trusted release commit tree does not match the tracked trust record",
    )


def canonical_repository(value: str) -> pathlib.Path:
    lexical = pathlib.Path(value).expanduser()
    if not lexical.is_absolute():
        lexical = pathlib.Path.cwd() / lexical
    require(not lexical.is_symlink(), f"repository must not be a symlink leaf: {lexical}")
    repository = lexical.resolve(strict=True)
    require(repository.is_dir(), f"repository is not a directory: {repository}")
    return repository


def main() -> int:
    if len(sys.argv) != 2:
        raise SystemExit("usage: verify_trial_trust.py REPOSITORY")
    try:
        trust = load_trust()
        verify_repository(canonical_repository(sys.argv[1]), trust)
    except AssertionError as error:
        print(f"trial trust verification failed: {error}", file=sys.stderr)
        return 1
    print("tracked v0.1.0 tag object, commit, and tree verification passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

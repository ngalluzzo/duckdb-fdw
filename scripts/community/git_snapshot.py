#!/usr/bin/env python3
"""Read exact commits and blobs without trusting a checkout or branch head."""

from __future__ import annotations

import os
import pathlib
import re
import subprocess
from dataclasses import dataclass

from record_format import AdmissionError, regular_directory, require


HEX40 = re.compile(r"[0-9a-f]{40}")


@dataclass(frozen=True)
class TreeEntry:
    """One path identity read from an exact immutable Git tree."""

    mode: str
    object_type: str
    object_id: str
    path: str


def require_commit(value: object, label: str) -> str:
    require(isinstance(value, str) and HEX40.fullmatch(value) is not None,
            f"{label} must be one lowercase 40-hex commit")
    return value


def _git(repository: pathlib.Path, *arguments: str, binary: bool = False) -> bytes | str:
    regular_directory(repository, "Git repository")
    environment = {
        "GIT_CONFIG_GLOBAL": os.devnull,
        "GIT_CONFIG_NOSYSTEM": "1",
        "GIT_NO_LAZY_FETCH": "1",
        "GIT_NO_REPLACE_OBJECTS": "1",
        "LC_ALL": "C",
        "PATH": os.environ.get("PATH", ""),
    }
    try:
        completed = subprocess.run(
            ["git", "--no-replace-objects", "-C", str(repository), *arguments],
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=not binary,
            env=environment,
        )
    except (OSError, subprocess.CalledProcessError) as error:
        operation = arguments[0] if arguments else "plumbing"
        raise AdmissionError(f"Git {operation} failed for an admitted repository") from error
    if binary:
        assert isinstance(completed.stdout, bytes)
        return completed.stdout
    assert isinstance(completed.stdout, str)
    return completed.stdout.strip()


def identity(repository: pathlib.Path, commit: str) -> tuple[str, str]:
    exact = require_commit(commit, "source commit")
    resolved = _git(repository, "rev-parse", "--verify", f"{exact}^{{commit}}")
    assert isinstance(resolved, str)
    require(resolved == exact, "source commit did not resolve to its exact identity")
    tree = _git(repository, "rev-parse", f"{exact}^{{tree}}")
    assert isinstance(tree, str)
    require_commit(tree, "source tree")
    return exact, tree


def tree_entries(repository: pathlib.Path, commit: str) -> list[TreeEntry]:
    """Return every non-tree entry without consulting the index or worktree."""

    exact = require_commit(commit, "source commit")
    payload = _git(
        repository,
        "ls-tree",
        "-r",
        "-z",
        "--full-tree",
        exact,
        binary=True,
    )
    assert isinstance(payload, bytes)
    result: list[TreeEntry] = []
    observed_paths: set[str] = set()
    for record in payload.split(b"\0"):
        if record == b"":
            continue
        metadata, separator, encoded_path = record.partition(b"\t")
        require(separator == b"\t", "Git tree entry metadata is malformed")
        try:
            mode, object_type, encoded_id = metadata.decode("ascii").split()
            path = encoded_path.decode("utf-8")
        except (UnicodeDecodeError, ValueError) as error:
            raise AdmissionError("Git tree entry metadata is malformed") from error
        object_id = require_commit(encoded_id, "Git tree object identity")
        require(path != "" and path not in observed_paths,
                "Git tree path identity is ambiguous")
        observed_paths.add(path)
        result.append(TreeEntry(mode, object_type, object_id, path))
    return result


def blob(repository: pathlib.Path, commit: str, relative_path: str) -> bytes:
    require(
        relative_path != ""
        and not pathlib.PurePosixPath(relative_path).is_absolute()
        and ".." not in pathlib.PurePosixPath(relative_path).parts,
        "Git blob path must be a contained relative path",
    )
    value = _git(repository, "cat-file", "blob", f"{commit}:{relative_path}", binary=True)
    assert isinstance(value, bytes)
    return value


def has_blob(repository: pathlib.Path, commit: str, relative_path: str) -> bool:
    require(
        relative_path != ""
        and not pathlib.PurePosixPath(relative_path).is_absolute()
        and ".." not in pathlib.PurePosixPath(relative_path).parts,
        "Git blob path must be a contained relative path",
    )
    value = _git(
        repository,
        "ls-tree",
        "--full-name",
        commit,
        "--",
        relative_path,
    )
    assert isinstance(value, str)
    if value == "":
        return False
    lines = value.splitlines()
    require(len(lines) == 1, "Git blob path is ambiguous")
    metadata, separator, observed_path = lines[0].partition("\t")
    require(separator == "\t" and observed_path == relative_path,
            "Git blob path identity drifted")
    fields = metadata.split()
    require(len(fields) == 3 and fields[1] == "blob",
            "Git blob path is not a regular blob")
    return True

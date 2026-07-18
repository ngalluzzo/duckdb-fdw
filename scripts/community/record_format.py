#!/usr/bin/env python3
"""Canonical, anchored record boundary for Community evidence providers."""

from __future__ import annotations

import hashlib
import json
import os
import pathlib
import re
import stat
from typing import Any


MAX_JSON_BYTES = 1024 * 1024
MAX_ANCHOR_BYTES = 256


class AdmissionError(RuntimeError):
    """An input failed a deterministic provider admission rule."""


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AdmissionError(message)


def sha256_bytes(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def _stat_identity(value: os.stat_result) -> tuple[int, int, int, int, int, int]:
    return (
        value.st_dev,
        value.st_ino,
        value.st_mode,
        value.st_size,
        value.st_mtime_ns,
        value.st_ctime_ns,
    )


def read_regular_bytes(
    path: pathlib.Path, label: str, maximum_bytes: int = MAX_JSON_BYTES
) -> bytes:
    """Read one stable regular-file snapshot from one bounded descriptor."""
    require(path.is_absolute(), f"{label} path must be absolute")
    try:
        before = os.lstat(path)
    except OSError as error:
        raise AdmissionError(f"{label} is unavailable") from error
    require(not stat.S_ISLNK(before.st_mode), f"{label} must not be a symlink")
    require(stat.S_ISREG(before.st_mode), f"{label} must be a regular file")
    require(before.st_size <= maximum_bytes, f"{label} exceeds its size limit")

    flags = os.O_RDONLY
    flags |= getattr(os, "O_CLOEXEC", 0)
    flags |= getattr(os, "O_NOFOLLOW", 0)
    try:
        descriptor = os.open(path, flags)
    except OSError as error:
        raise AdmissionError(f"{label} could not be opened safely") from error
    try:
        opened = os.fstat(descriptor)
        require(stat.S_ISREG(opened.st_mode), f"{label} must be a regular file")
        require(
            (opened.st_dev, opened.st_ino) == (before.st_dev, before.st_ino),
            f"{label} changed while it was opened",
        )
        require(opened.st_size <= maximum_bytes, f"{label} exceeds its size limit")

        chunks: list[bytes] = []
        observed = 0
        while True:
            chunk = os.read(descriptor, min(65536, maximum_bytes + 1 - observed))
            if not chunk:
                break
            chunks.append(chunk)
            observed += len(chunk)
            require(observed <= maximum_bytes, f"{label} exceeds its size limit")
        after = os.fstat(descriptor)
        require(
            _stat_identity(after) == _stat_identity(opened),
            f"{label} changed while it was read",
        )
        raw = b"".join(chunks)
        require(len(raw) == after.st_size, f"{label} changed while it was read")
        return raw
    except OSError as error:
        raise AdmissionError(f"{label} could not be read safely") from error
    finally:
        os.close(descriptor)


def regular_directory(path: pathlib.Path, label: str) -> pathlib.Path:
    require(path.is_absolute(), f"{label} path must be absolute")
    try:
        observed = os.lstat(path)
    except OSError as error:
        raise AdmissionError(f"{label} is unavailable") from error
    require(not stat.S_ISLNK(observed.st_mode), f"{label} must not be a symlink")
    require(stat.S_ISDIR(observed.st_mode), f"{label} must be a directory")
    return path


def canonical_json_bytes(value: object) -> bytes:
    return (json.dumps(value, indent=2, sort_keys=True) + "\n").encode("utf-8")


def _canonical_object(raw: bytes, label: str) -> dict[str, Any]:
    try:
        value = json.loads(raw)
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise AdmissionError(f"{label} is not valid UTF-8 JSON") from error
    require(isinstance(value, dict), f"{label} must be a JSON object")
    require(raw == canonical_json_bytes(value), f"{label} is not canonical JSON")
    return value


def load_canonical_object(
    path: pathlib.Path, label: str
) -> tuple[dict[str, Any], str]:
    """Return a parsed object and digest derived from the same single read."""
    raw = read_regular_bytes(path, label)
    return _canonical_object(raw, label), sha256_bytes(raw)


def prepare_output_root(path: pathlib.Path) -> pathlib.Path:
    require(path.is_absolute(), "output root path must be absolute")
    regular_directory(path.parent, "output parent")
    try:
        os.mkdir(path, mode=0o700)
    except OSError as error:
        raise AdmissionError("output root must be new and creatable") from error
    return path


def _write_exclusive(path: pathlib.Path, payload: bytes) -> None:
    flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL
    flags |= getattr(os, "O_CLOEXEC", 0)
    flags |= getattr(os, "O_NOFOLLOW", 0)
    try:
        descriptor = os.open(path, flags, 0o400)
    except OSError as error:
        raise AdmissionError("provider output could not be created safely") from error
    try:
        offset = 0
        while offset < len(payload):
            written = os.write(descriptor, payload[offset:])
            require(written > 0, "provider output write did not progress")
            offset += written
        os.fchmod(descriptor, 0o444)
    except OSError as error:
        raise AdmissionError("provider output could not be written safely") from error
    finally:
        os.close(descriptor)


def write_anchored_json(
    output_root: pathlib.Path, filename: str, value: object
) -> tuple[pathlib.Path, pathlib.Path]:
    require(
        re.fullmatch(r"[a-z][a-z0-9-]*\.json", filename) is not None,
        "record filename is invalid",
    )
    payload = canonical_json_bytes(value)
    record = output_root / filename
    anchor = output_root / (filename.removesuffix(".json") + ".sha256")
    _write_exclusive(record, payload)
    _write_exclusive(anchor, f"{sha256_bytes(payload)}  {filename}\n".encode("ascii"))
    return record, anchor


def verify_anchored_object(
    record: pathlib.Path,
    anchor: pathlib.Path,
    expected_filename: str,
    label: str,
) -> tuple[dict[str, Any], str, str]:
    require(record.name == expected_filename, f"{label} record filename is invalid")
    expected_anchor_name = expected_filename.removesuffix(".json") + ".sha256"
    require(anchor.name == expected_anchor_name, f"{label} anchor filename is invalid")
    raw = read_regular_bytes(record, f"{label} record")
    anchor_raw = read_regular_bytes(
        anchor, f"{label} anchor", maximum_bytes=MAX_ANCHOR_BYTES
    )
    digest = sha256_bytes(raw)
    expected = f"{digest}  {expected_filename}\n".encode("ascii")
    require(anchor_raw == expected, f"{label} anchor syntax or digest drifted")
    value = _canonical_object(raw, f"{label} record")
    return value, digest, sha256_bytes(anchor_raw)

"""Safely inventory and hash explicit Community artifact and log downloads."""

from __future__ import annotations

import hashlib
import os
import pathlib
import stat
from typing import Any

from record_format import AdmissionError, require


MAX_ARTIFACT_BYTES = 2 * 1024 * 1024 * 1024
MAX_LOG_BYTES = 256 * 1024 * 1024


def _identity(value: os.stat_result) -> tuple[int, int, int, int, int, int]:
    return (
        value.st_dev,
        value.st_ino,
        value.st_mode,
        value.st_size,
        value.st_mtime_ns,
        value.st_ctime_ns,
    )


def _open_directory(root: pathlib.Path, label: str) -> tuple[int, os.stat_result]:
    """Open one stable directory capability without following a replacement."""

    require(root.is_absolute(), f"{label} path must be absolute")
    try:
        before = os.lstat(root)
    except OSError as error:
        raise AdmissionError(f"{label} is unavailable") from error
    require(not stat.S_ISLNK(before.st_mode), f"{label} must not be a symlink")
    require(stat.S_ISDIR(before.st_mode), f"{label} must be a directory")
    directory_flag = getattr(os, "O_DIRECTORY", None)
    nofollow_flag = getattr(os, "O_NOFOLLOW", None)
    require(directory_flag is not None and nofollow_flag is not None,
            "platform lacks safe directory-open support")
    flags = os.O_RDONLY | os.O_CLOEXEC | directory_flag | nofollow_flag
    try:
        descriptor = os.open(root, flags)
    except OSError as error:
        raise AdmissionError(f"{label} could not be opened safely") from error
    try:
        opened = os.fstat(descriptor)
        require(stat.S_ISDIR(opened.st_mode), f"{label} must be a directory")
        require(
            (opened.st_dev, opened.st_ino) == (before.st_dev, before.st_ino),
            f"{label} changed while it was opened",
        )
        return descriptor, opened
    except BaseException:
        os.close(descriptor)
        raise


def _inventory_directory(
    descriptor: int, expected: set[str], label: str
) -> dict[str, tuple[int, int, int, int, int, int]]:
    try:
        names = os.listdir(descriptor)
    except OSError as error:
        raise AdmissionError(f"{label} could not be inventoried") from error
    observed = set(names)
    require(observed == expected, f"{label} has missing or extra files")
    result: dict[str, tuple[int, int, int, int, int, int]] = {}
    for name in names:
        try:
            details = os.stat(name, dir_fd=descriptor, follow_symlinks=False)
        except OSError as error:
            raise AdmissionError(f"{label} entry is unavailable") from error
        require(not stat.S_ISLNK(details.st_mode),
                f"{label} entries must not be symlinks")
        require(stat.S_ISREG(details.st_mode), f"{label} entries must be regular files")
        result[name] = _identity(details)
    return result


def _digest_file_at(
    directory: int, filename: str, label: str, maximum_bytes: int
) -> dict[str, Any]:
    try:
        before = os.stat(filename, dir_fd=directory, follow_symlinks=False)
    except OSError as error:
        raise AdmissionError(f"{label} is unavailable") from error
    require(not stat.S_ISLNK(before.st_mode), f"{label} must not be a symlink")
    require(stat.S_ISREG(before.st_mode), f"{label} must be a regular file")
    require(before.st_size <= maximum_bytes, f"{label} exceeds its size limit")
    flags = os.O_RDONLY | getattr(os, "O_CLOEXEC", 0) | getattr(os, "O_NOFOLLOW", 0)
    try:
        descriptor = os.open(filename, flags, dir_fd=directory)
    except OSError as error:
        raise AdmissionError(f"{label} could not be opened safely") from error
    try:
        opened = os.fstat(descriptor)
        require((opened.st_dev, opened.st_ino) == (before.st_dev, before.st_ino),
                f"{label} changed while it was opened")
        digest = hashlib.sha256()
        observed = 0
        while True:
            chunk = os.read(descriptor, 1024 * 1024)
            if not chunk:
                break
            observed += len(chunk)
            require(observed <= maximum_bytes, f"{label} exceeds its size limit")
            digest.update(chunk)
        after = os.fstat(descriptor)
        require(_identity(after) == _identity(opened), f"{label} changed while it was read")
        require(observed == after.st_size, f"{label} changed while it was read")
        return {"sha256": digest.hexdigest(), "size_in_bytes": observed}
    except OSError as error:
        raise AdmissionError(f"{label} could not be read safely") from error
    finally:
        os.close(descriptor)


def _verify_directory_unchanged(
    descriptor: int,
    root: pathlib.Path,
    opened: os.stat_result,
    entries: dict[str, tuple[int, int, int, int, int, int]],
    label: str,
) -> None:
    """Reject directory, path-binding, inventory, or entry replacement drift."""

    try:
        after = os.fstat(descriptor)
        path_after = os.lstat(root)
        names = os.listdir(descriptor)
    except OSError as error:
        raise AdmissionError(f"{label} changed during collection") from error
    require(_identity(after) == _identity(opened),
            f"{label} changed during collection")
    require(
        not stat.S_ISLNK(path_after.st_mode)
        and stat.S_ISDIR(path_after.st_mode)
        and (path_after.st_dev, path_after.st_ino)
        == (opened.st_dev, opened.st_ino),
        f"{label} changed during collection",
    )
    require(set(names) == set(entries), f"{label} changed during collection")
    for name, identity in entries.items():
        try:
            current = os.stat(name, dir_fd=descriptor, follow_symlinks=False)
        except OSError as error:
            raise AdmissionError(f"{label} changed during collection") from error
        require(_identity(current) == identity,
                f"{label} changed during collection")


def collect_downloads(
    artifact_root: pathlib.Path,
    log_root: pathlib.Path,
    jobs: list[dict[str, Any]],
    artifacts: list[dict[str, Any]],
) -> tuple[dict[int, list[dict[str, Any]]], dict[int, dict[str, Any]]]:
    """Bind every expected download byte and reject any unaccounted file."""

    require(artifact_root.name == "artifacts",
            "artifact downloads root name is invalid")
    require(log_root.name == "logs", "job logs root name is invalid")
    artifact_descriptor = -1
    log_descriptor = -1
    try:
        artifact_descriptor, artifact_opened = _open_directory(
            artifact_root, "artifact downloads root"
        )
        artifact_files = _inventory_directory(
            artifact_descriptor,
            {artifact["filename"] for artifact in artifacts},
            "artifact downloads root",
        )
        log_descriptor, log_opened = _open_directory(log_root, "job logs root")
        log_files = _inventory_directory(
            log_descriptor,
            {job["log"]["filename"] for job in jobs},
            "job logs root",
        )
        by_job: dict[int, list[dict[str, Any]]] = {job["id"]: [] for job in jobs}
        for artifact in artifacts:
            observed = _digest_file_at(
                artifact_descriptor,
                artifact["filename"],
                "artifact download",
                MAX_ARTIFACT_BYTES,
            )
            require(
                observed
                == {
                    "sha256": artifact["sha256"],
                    "size_in_bytes": artifact["size_in_bytes"],
                },
                "artifact download bytes drifted from the approved export",
            )
            by_job[artifact["job_id"]].append(
                {
                    "filename": artifact["filename"],
                    "id": artifact["id"],
                    "name": artifact["name"],
                    "relative_path": f"artifacts/{artifact['filename']}",
                    **observed,
                }
            )
        logs: dict[int, dict[str, Any]] = {}
        for job in jobs:
            filename = job["log"]["filename"]
            observed = _digest_file_at(
                log_descriptor, filename, "job log download", MAX_LOG_BYTES
            )
            require(
                observed
                == {
                    "sha256": job["log"]["sha256"],
                    "size_in_bytes": job["log"]["size_in_bytes"],
                },
                "job log download bytes drifted from the approved export",
            )
            logs[job["id"]] = {
                "filename": filename,
                "relative_path": f"logs/{filename}",
                **observed,
            }
        _verify_directory_unchanged(
            artifact_descriptor,
            artifact_root,
            artifact_opened,
            artifact_files,
            "artifact downloads root",
        )
        _verify_directory_unchanged(
            log_descriptor,
            log_root,
            log_opened,
            log_files,
            "job logs root",
        )
        return by_job, logs
    finally:
        if log_descriptor >= 0:
            os.close(log_descriptor)
        if artifact_descriptor >= 0:
            os.close(artifact_descriptor)

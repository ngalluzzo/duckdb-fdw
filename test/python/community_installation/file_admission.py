"""Descriptor-stably admit and stage bounded host input files."""

from __future__ import annotations

import hashlib
import os
import pathlib
import re
import stat
from dataclasses import dataclass


SHA256 = re.compile(r"[0-9a-f]{64}")
MAX_STAGED_ARTIFACT_BYTES = 256 * 1024 * 1024


class FileAdmissionError(ValueError):
    """A host input file is unbounded, mutable, or content-ambiguous."""


@dataclass
class StagedFile:
    """One exact staged inode retained open until its consumer finishes."""

    path: pathlib.Path
    descriptor: int
    size: int
    sha256: str

    def close(self) -> None:
        if self.descriptor >= 0:
            os.close(self.descriptor)
            self.descriptor = -1


def regular_file(path: pathlib.Path, label: str) -> pathlib.Path:
    lexical = pathlib.Path(path).expanduser().absolute()
    try:
        metadata = lexical.lstat()
    except OSError as error:
        raise FileAdmissionError(f"{label} is unavailable") from error
    if stat.S_ISLNK(metadata.st_mode) or not stat.S_ISREG(metadata.st_mode):
        raise FileAdmissionError(f"{label} must be a regular non-symlink file")
    return lexical.resolve(strict=True)


def descriptor_stable_identity(
    path: pathlib.Path,
    *,
    label: str,
    limit_bytes: int,
) -> tuple[int, str]:
    """Hash one bounded regular file through the same no-follow descriptor."""

    lexical = pathlib.Path(path).expanduser().absolute()
    try:
        before = lexical.lstat()
    except OSError as error:
        raise FileAdmissionError(f"{label} is unavailable") from error
    if (
        stat.S_ISLNK(before.st_mode)
        or not stat.S_ISREG(before.st_mode)
        or before.st_size > limit_bytes
    ):
        raise FileAdmissionError(f"{label} is not a bounded regular file")
    descriptor: int | None = None
    try:
        descriptor = os.open(
            lexical, os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0)
        )
        opened = os.fstat(descriptor)
        identity = _identity(opened)
        if identity != _identity(before) or opened.st_size > limit_bytes:
            raise FileAdmissionError(f"{label} changed before it could be read")
        digest = hashlib.sha256()
        remaining = opened.st_size
        while remaining:
            chunk = os.read(descriptor, min(remaining, 64 * 1024))
            if not chunk:
                raise FileAdmissionError(f"{label} changed while it was read")
            digest.update(chunk)
            remaining -= len(chunk)
        if os.read(descriptor, 1) or _identity(os.fstat(descriptor)) != identity:
            raise FileAdmissionError(f"{label} changed while it was read")
        return opened.st_size, digest.hexdigest()
    except FileAdmissionError:
        raise
    except OSError as error:
        raise FileAdmissionError(f"{label} could not be read") from error
    finally:
        if descriptor is not None:
            os.close(descriptor)


def open_content_identified_descriptor(
    path: pathlib.Path,
    *,
    expected_size: int,
    expected_sha256: str,
    label: str,
    limit_bytes: int,
) -> int:
    """Return the same verified descriptor that a child must execute or read."""

    if expected_size < 0 or expected_size > limit_bytes:
        raise FileAdmissionError(f"{label} size authority is malformed")
    authority_descriptor: int | None = None
    read_descriptor: int | None = None
    try:
        lexical = pathlib.Path(path).expanduser().absolute()
        before = lexical.lstat()
        authority_descriptor = os.open(
            lexical,
            getattr(os, "O_EXEC", os.O_RDONLY)
            | getattr(os, "O_NOFOLLOW", 0),
        )
        read_descriptor = os.open(
            lexical, os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0)
        )
        opened = os.fstat(read_descriptor)
        identity = _identity(opened)
        if (
            stat.S_ISLNK(before.st_mode)
            or not stat.S_ISREG(before.st_mode)
            or _identity(before) != identity
            or _identity(os.fstat(authority_descriptor)) != identity
            or opened.st_size != expected_size
        ):
            raise FileAdmissionError(f"{label} changed before it could be read")
        digest = hashlib.sha256()
        remaining = expected_size
        while remaining:
            chunk = os.read(read_descriptor, min(remaining, 64 * 1024))
            if not chunk:
                raise FileAdmissionError(f"{label} changed while it was read")
            digest.update(chunk)
            remaining -= len(chunk)
        if (
            os.read(read_descriptor, 1)
            or _identity(os.fstat(read_descriptor)) != identity
            or _identity(os.fstat(authority_descriptor)) != identity
            or digest.hexdigest() != expected_sha256
        ):
            raise FileAdmissionError(f"{label} bytes changed")
        result = authority_descriptor
        authority_descriptor = None
        return result
    except FileAdmissionError:
        raise
    except OSError as error:
        raise FileAdmissionError(f"{label} could not be opened") from error
    finally:
        if authority_descriptor is not None:
            os.close(authority_descriptor)
        if read_descriptor is not None:
            os.close(read_descriptor)


def _identity(value: os.stat_result) -> tuple[int, ...]:
    return (
        value.st_dev,
        value.st_ino,
        value.st_mode,
        value.st_size,
        value.st_mtime_ns,
        value.st_ctime_ns,
    )


def stage_content_identified_file(
    source: pathlib.Path,
    destination: pathlib.Path,
    *,
    expected_size: int,
    expected_sha256: str,
    limit_bytes: int = MAX_STAGED_ARTIFACT_BYTES,
    mode: int = 0o400,
) -> StagedFile:
    """Copy one descriptor-stable bounded file into new private runner state."""

    if (
        SHA256.fullmatch(expected_sha256) is None
        or expected_size < 0
        or limit_bytes <= 0
        or expected_size > limit_bytes
    ):
        raise FileAdmissionError("staged artifact authority is malformed")
    lexical = pathlib.Path(source).expanduser().absolute()
    try:
        before = lexical.lstat()
    except OSError as error:
        raise FileAdmissionError("staged artifact is unavailable") from error
    if (
        stat.S_ISLNK(before.st_mode)
        or not stat.S_ISREG(before.st_mode)
        or before.st_size != expected_size
    ):
        raise FileAdmissionError("staged artifact is not a bounded regular file")

    source_descriptor: int | None = None
    destination_descriptor: int | None = None
    retained_descriptor: int | None = None
    created_identity: tuple[int, ...] | None = None
    created = False
    succeeded = False
    try:
        source_descriptor = os.open(
            lexical, os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0)
        )
        opened = os.fstat(source_descriptor)
        identity = _identity(opened)
        if identity != _identity(before) or opened.st_size != expected_size:
            raise FileAdmissionError("staged artifact changed before it could be read")
        destination_descriptor = os.open(
            destination, os.O_RDWR | os.O_CREAT | os.O_EXCL, mode
        )
        created = True
        digest = hashlib.sha256()
        remaining = opened.st_size
        while remaining:
            chunk = os.read(source_descriptor, min(remaining, 64 * 1024))
            if not chunk:
                raise FileAdmissionError("staged artifact changed while it was read")
            digest.update(chunk)
            remaining -= len(chunk)
            view = memoryview(chunk)
            while view:
                written = os.write(destination_descriptor, view)
                if written <= 0:
                    raise FileAdmissionError("staged artifact could not be copied")
                view = view[written:]
        if os.read(source_descriptor, 1):
            raise FileAdmissionError("staged artifact changed while it was read")
        if (
            _identity(os.fstat(source_descriptor)) != identity
            or digest.hexdigest() != expected_sha256
        ):
            raise FileAdmissionError("staged artifact bytes do not match their authority")
        os.fsync(destination_descriptor)
        os.fchmod(destination_descriptor, mode)
        created_identity = _identity(os.fstat(destination_descriptor))
        retained_descriptor = os.open(
            destination, os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0)
        )
        if _identity(os.fstat(retained_descriptor)) != created_identity:
            raise FileAdmissionError("staged artifact read capability drifted")
        os.lseek(retained_descriptor, 0, os.SEEK_SET)
        retained_digest = hashlib.sha256()
        retained_remaining = expected_size
        while retained_remaining:
            chunk = os.read(
                retained_descriptor, min(retained_remaining, 64 * 1024)
            )
            if not chunk:
                raise FileAdmissionError("staged artifact copy is incomplete")
            retained_digest.update(chunk)
            retained_remaining -= len(chunk)
        if os.read(retained_descriptor, 1) or retained_digest.hexdigest() != expected_sha256:
            raise FileAdmissionError("staged artifact copy identity drifted")
        os.lseek(retained_descriptor, 0, os.SEEK_SET)
        os.close(destination_descriptor)
        destination_descriptor = None
        succeeded = True
        result = StagedFile(
            destination.resolve(strict=True),
            retained_descriptor,
            expected_size,
            expected_sha256,
        )
        retained_descriptor = None
        return result
    except FileAdmissionError:
        raise
    except OSError as error:
        raise FileAdmissionError("staged artifact could not be copied") from error
    finally:
        if source_descriptor is not None:
            os.close(source_descriptor)
        if destination_descriptor is not None and not succeeded:
            os.close(destination_descriptor)
        if retained_descriptor is not None:
            os.close(retained_descriptor)
        if created and not succeeded:
            try:
                current = destination.lstat()
                if created_identity is not None and _identity(current) == created_identity:
                    destination.unlink()
            except OSError:
                pass

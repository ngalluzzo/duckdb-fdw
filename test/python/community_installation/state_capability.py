"""Own descriptor-bound persistent state for one stock-host action.

``StateCapability`` admits one named state directory beneath a caller-owned
root, retains its inode, and hides both DuckDB-visible leaves behind fresh
execution names.  The child receives only the retained directory descriptor
and those relative names.  ``finish`` restores the logical leaves through the
same descriptor, rejects a replaced public directory entry, validates the
published state, and closes the descriptor on every path.

Instances are single-action and single-orchestrator.  Callers must invoke
``finish`` exactly once after ``admit`` succeeds, including when the child
fails.  This module owns filesystem authority; it knows nothing about process
output, DuckDB observations, support policy, or provider evidence.
"""

from __future__ import annotations

import ctypes
from dataclasses import dataclass
import errno
import os
import pathlib
import re
import secrets
import stat
import sys


STATE_ID = re.compile(r"[a-z][a-z0-9_-]*")


class StateCapabilityError(AssertionError):
    """Persistent stock-host state was unsafe, ambiguous, or unavailable."""


def _rename_no_replace(source: str, destination: str, descriptor: int) -> None:
    """Atomically publish one hidden leaf without replacing caller data."""

    library = ctypes.CDLL(None, use_errno=True)
    if sys.platform == "darwin":
        rename = library.renameatx_np
        flags = 0x00000004  # RENAME_EXCL
    elif sys.platform.startswith("linux"):
        try:
            rename = library.renameat2
        except AttributeError as error:
            raise OSError(errno.ENOTSUP, "atomic no-replace rename is unavailable") from error
        flags = 0x00000001  # RENAME_NOREPLACE
    else:
        raise OSError(errno.ENOTSUP, "atomic no-replace rename is unavailable")
    rename.argtypes = (
        ctypes.c_int,
        ctypes.c_char_p,
        ctypes.c_int,
        ctypes.c_char_p,
        ctypes.c_uint,
    )
    rename.restype = ctypes.c_int
    result = rename(
        descriptor,
        os.fsencode(source),
        descriptor,
        os.fsencode(destination),
        flags,
    )
    if result != 0:
        error_number = ctypes.get_errno()
        raise OSError(error_number, os.strerror(error_number), destination)


def _validate_path(
    path: pathlib.Path, state_root: pathlib.Path, *, directory: bool
) -> None:
    """Re-admit one published state path without following a swapped leaf."""

    flags = os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0)
    if directory:
        flags |= getattr(os, "O_DIRECTORY", 0)
    descriptor: int | None = None
    try:
        metadata = path.lstat()
        descriptor = os.open(path, flags)
        opened = os.fstat(descriptor)
    except OSError as error:
        raise StateCapabilityError("persistent host state is unavailable") from error
    finally:
        if descriptor is not None:
            os.close(descriptor)
    expected_type = stat.S_ISDIR if directory else stat.S_ISREG
    if (
        stat.S_ISLNK(metadata.st_mode)
        or not expected_type(metadata.st_mode)
        or not expected_type(opened.st_mode)
        or (metadata.st_dev, metadata.st_ino) != (opened.st_dev, opened.st_ino)
    ):
        raise StateCapabilityError("persistent host state escaped its caller root")
    try:
        path.resolve(strict=True).relative_to(state_root)
    except (OSError, ValueError) as error:
        raise StateCapabilityError(
            "persistent host state escaped its caller root"
        ) from error


@dataclass
class StateCapability:
    """One retained state-directory descriptor and its private child leaves."""

    state_root: pathlib.Path
    state: pathlib.Path
    database: pathlib.Path
    extension_directory: pathlib.Path
    descriptor: int
    identity: tuple[int, int]
    child_database: str
    child_extension_directory: str
    _finished: bool = False

    @classmethod
    def admit(
        cls, state_root: pathlib.Path, state_id: str
    ) -> "StateCapability":
        """Admit and isolate one named state for a single child action."""

        if STATE_ID.fullmatch(state_id) is None:
            raise StateCapabilityError("host state identifier is malformed")
        state = state_root / state_id
        if not state.exists():
            try:
                state.mkdir(mode=0o700)
                (state / "extensions").mkdir(mode=0o700)
            except OSError as error:
                raise StateCapabilityError(
                    "persistent host state could not be created"
                ) from error
        _validate_path(state, state_root, directory=True)
        extensions = state / "extensions"
        _validate_path(extensions, state_root, directory=True)
        database = state / "query.duckdb"
        if database.exists() or database.is_symlink():
            _validate_path(database, state_root, directory=False)
        descriptor: int | None = None
        try:
            before = state.lstat()
            descriptor = os.open(
                state,
                os.O_RDONLY
                | getattr(os, "O_DIRECTORY", 0)
                | getattr(os, "O_NOFOLLOW", 0),
            )
            opened = os.fstat(descriptor)
            identity = (opened.st_dev, opened.st_ino)
            if (before.st_dev, before.st_ino) != identity:
                raise OSError("state directory changed during admission")
            child_database, child_extensions = cls._isolate(
                descriptor, database
            )
        except (OSError, StateCapabilityError) as error:
            if descriptor is not None:
                os.close(descriptor)
            if isinstance(error, StateCapabilityError):
                raise
            raise StateCapabilityError("persistent host state is unavailable") from error
        return cls(
            state_root,
            state,
            database,
            extensions,
            descriptor,
            identity,
            child_database,
            child_extensions,
        )

    @staticmethod
    def _isolate(descriptor: int, database: pathlib.Path) -> tuple[str, str]:
        """Atomically hide public leaves behind the retained directory fd."""

        token = secrets.token_hex(16)
        child_database = f".query-{token}.duckdb"
        child_extensions = f".extensions-{token}"
        database_moved = False
        extensions_moved = False
        try:
            if database.exists() or database.is_symlink():
                os.rename(
                    "query.duckdb",
                    child_database,
                    src_dir_fd=descriptor,
                    dst_dir_fd=descriptor,
                )
                database_moved = True
                moved_database = os.stat(
                    child_database, dir_fd=descriptor, follow_symlinks=False
                )
                if not stat.S_ISREG(moved_database.st_mode):
                    raise OSError("isolated database has the wrong file type")
            os.rename(
                "extensions",
                child_extensions,
                src_dir_fd=descriptor,
                dst_dir_fd=descriptor,
            )
            extensions_moved = True
            moved_extensions = os.stat(
                child_extensions, dir_fd=descriptor, follow_symlinks=False
            )
            if not stat.S_ISDIR(moved_extensions.st_mode):
                raise OSError("isolated extensions have the wrong file type")
        except OSError as error:
            if extensions_moved:
                try:
                    os.replace(
                        child_extensions,
                        "extensions",
                        src_dir_fd=descriptor,
                        dst_dir_fd=descriptor,
                    )
                except OSError:
                    pass
            if database_moved:
                try:
                    os.replace(
                        child_database,
                        "query.duckdb",
                        src_dir_fd=descriptor,
                        dst_dir_fd=descriptor,
                    )
                except OSError:
                    pass
            raise StateCapabilityError(
                "persistent host state could not be isolated"
            ) from error
        return child_database, child_extensions

    def _public_entry_matches(self) -> bool:
        try:
            published = self.state.lstat()
            retained = os.fstat(self.descriptor)
        except OSError:
            return False
        return (published.st_dev, published.st_ino) == (
            retained.st_dev,
            retained.st_ino,
        )

    def _restore(self) -> None:
        """Publish every non-conflicting child leaf without replacing callers."""

        failures: list[OSError] = []
        try:
            extension_metadata = os.stat(
                self.child_extension_directory,
                dir_fd=self.descriptor,
                follow_symlinks=False,
            )
            if not stat.S_ISDIR(extension_metadata.st_mode):
                raise OSError("child state has the wrong file type")
            _rename_no_replace(
                self.child_extension_directory,
                "extensions",
                self.descriptor,
            )
        except OSError as error:
            failures.append(error)
        try:
            try:
                database_metadata = os.stat(
                    self.child_database,
                    dir_fd=self.descriptor,
                    follow_symlinks=False,
                )
            except FileNotFoundError:
                database_metadata = None
            if database_metadata is not None and not stat.S_ISREG(
                database_metadata.st_mode
            ):
                raise OSError("child database has the wrong file type")
            if database_metadata is not None:
                _rename_no_replace(
                    self.child_database,
                    "query.duckdb",
                    self.descriptor,
                )
        except OSError as error:
            failures.append(error)
        if failures:
            raise StateCapabilityError(
                "persistent host state could not be restored"
            ) from failures[0]

    def finish(self) -> None:
        """Restore, close, and validate this single-use state capability."""

        if self._finished:
            raise StateCapabilityError("persistent host state was already finished")
        self._finished = True
        directory_replaced = not self._public_entry_matches()
        try:
            self._restore()
        finally:
            os.close(self.descriptor)
        if directory_replaced:
            raise StateCapabilityError(
                "persistent host state directory was replaced"
            )
        _validate_path(self.state, self.state_root, directory=True)
        published = self.state.lstat()
        if (published.st_dev, published.st_ino) != self.identity:
            raise StateCapabilityError(
                "persistent host state directory was replaced"
            )
        if self.database.exists() or self.database.is_symlink():
            _validate_path(self.database, self.state_root, directory=False)
        _validate_path(self.extension_directory, self.state_root, directory=True)

"""Admit one explicit, content-identified stock DuckDB host inventory."""

from __future__ import annotations

from collections.abc import Sequence
from dataclasses import dataclass
import hashlib
import json
import os
import pathlib
import re

try:
    from .file_admission import (
        FileAdmissionError,
        descriptor_stable_identity,
        stage_content_identified_file,
    )
except ImportError:
    from file_admission import (
        FileAdmissionError,
        descriptor_stable_identity,
        stage_content_identified_file,
    )


SHA256 = re.compile(r"[0-9a-f]{64}")
MAX_LAUNCHER_BYTES = 256 * 1024 * 1024
MAX_VENV_CONFIG_BYTES = 64 * 1024
MAX_MODULE_FILE_BYTES = 256 * 1024 * 1024


class LauncherError(ValueError):
    """The explicit stock launcher or its runtime inventory is unsafe."""


@dataclass(frozen=True)
class InventoryFile:
    """One exact regular file admitted into the private host capability."""

    logical_path: pathlib.PurePosixPath
    source: pathlib.Path
    size: int
    sha256: str


@dataclass(frozen=True)
class BoundCommand:
    """Argv plus the private executable the OS must execute."""

    arguments: tuple[str, ...]
    executable: pathlib.Path


@dataclass(frozen=True)
class StagedLauncher:
    """A runner-private executable and, when needed, complete DuckDB module set."""

    source: "Launcher"
    executable: pathlib.Path
    venv_launcher: pathlib.Path | None

    def command(
        self, program: pathlib.Path, arguments: Sequence[str]
    ) -> BoundCommand:
        argv = (
            str(self.source.executable),
            *self.source.arguments,
            str(program),
            *arguments,
        )
        return BoundCommand(argv, self.executable)


def _admit_file(
    source: pathlib.Path,
    logical_path: pathlib.PurePosixPath,
    *,
    label: str,
    limit_bytes: int,
) -> InventoryFile:
    if source.is_symlink():
        raise LauncherError(f"{label} must not be a symbolic link")
    try:
        size, digest = descriptor_stable_identity(
            source, label=label, limit_bytes=limit_bytes
        )
    except FileAdmissionError as error:
        raise LauncherError(str(error)) from error
    return InventoryFile(logical_path, source, size, digest)


def _inventory(
    executable: pathlib.Path,
) -> tuple[tuple[InventoryFile, ...], bool]:
    resolved_executable = executable.resolve(strict=True)
    files = [
        _admit_file(
            resolved_executable,
            pathlib.PurePosixPath("executable"),
            label="stock DuckDB launcher",
            limit_bytes=MAX_LAUNCHER_BYTES,
        )
    ]
    venv_root = executable.parent.parent
    config = venv_root / "pyvenv.cfg"
    if not config.exists():
        return tuple(files), False
    files.append(
        _admit_file(
            config,
            pathlib.PurePosixPath("pyvenv.cfg"),
            label="stock DuckDB virtual-environment configuration",
            limit_bytes=MAX_VENV_CONFIG_BYTES,
        )
    )
    site_roots = tuple(
        candidate
        for candidate in (venv_root / "lib").glob("python*/site-packages")
        if candidate.is_dir() and not candidate.is_symlink()
    )
    if len(site_roots) != 1:
        raise LauncherError(
            "stock DuckDB virtual environment has ambiguous site-packages"
        )
    site_root = site_roots[0]
    native = tuple(site_root.glob("_duckdb.*"))
    packages = tuple(site_root.glob("duckdb"))
    metadata = tuple(site_root.glob("duckdb-*.dist-info"))
    if (
        len(native) != 1
        or not native[0].is_file()
        or len(packages) != 1
        or not packages[0].is_dir()
        or len(metadata) != 1
        or not metadata[0].is_dir()
    ):
        raise LauncherError("stock DuckDB module inventory is incomplete")
    module_sources = [native[0]]
    for directory in (packages[0], metadata[0]):
        module_sources.extend(path for path in directory.rglob("*") if path.is_file())
    for source in sorted(module_sources):
        relative = source.relative_to(venv_root)
        files.append(
            _admit_file(
                source,
                pathlib.PurePosixPath(relative.as_posix()),
                label="stock DuckDB module inventory file",
                limit_bytes=MAX_MODULE_FILE_BYTES,
            )
        )
    return tuple(files), True


def _inventory_sha256(files: Sequence[InventoryFile]) -> str:
    value = [
        {"path": str(item.logical_path), "sha256": item.sha256, "size": item.size}
        for item in files
    ]
    canonical = json.dumps(value, sort_keys=True, separators=(",", ":")).encode()
    return hashlib.sha256(canonical).hexdigest()


def stock_host_inventory_sha256(executable: str | pathlib.Path) -> str:
    """Measure the canonical host inventory for provider admission.

    Providers retain this digest as the expected authority. ``Launcher.admit``
    independently remeasures every file before a passing run can be composed.
    """

    path = pathlib.Path(executable).expanduser().absolute()
    files, _ = _inventory(path)
    return _inventory_sha256(files)


@dataclass(frozen=True)
class Launcher:
    """A verified argv prefix plus its exact executable and DuckDB module files."""

    executable: pathlib.Path
    executable_size: int
    executable_sha256: str
    stock_host_inventory_sha256: str
    arguments: tuple[str, ...] = ()
    inventory: tuple[InventoryFile, ...] = ()
    virtual_environment: bool = False

    @classmethod
    def admit(
        cls,
        executable: str | pathlib.Path,
        arguments: Sequence[str] = (),
        *,
        executable_sha256: str,
        stock_host_inventory_sha256: str,
    ) -> "Launcher":
        path = pathlib.Path(executable).expanduser().absolute()
        if not path.is_file() or not os.access(path, os.X_OK):
            raise LauncherError("stock DuckDB launcher is not executable")
        normalized = tuple(arguments)
        if any(not isinstance(value, str) or not value for value in normalized):
            raise LauncherError("stock DuckDB launcher arguments are malformed")
        if SHA256.fullmatch(executable_sha256) is None:
            raise LauncherError("stock DuckDB launcher identity is malformed")
        if SHA256.fullmatch(stock_host_inventory_sha256) is None:
            raise LauncherError("stock DuckDB host inventory identity is malformed")
        inventory, virtual_environment = _inventory(path)
        executable_file = inventory[0]
        if executable_file.sha256 != executable_sha256:
            raise LauncherError("stock DuckDB launcher bytes changed")
        if _inventory_sha256(inventory) != stock_host_inventory_sha256:
            raise LauncherError("stock DuckDB host inventory bytes changed")
        policy_tokens = " ".join(normalized).lower()
        if "unsigned" in policy_tokens or "allow_unsigned_extensions" in policy_tokens:
            raise LauncherError("stock DuckDB launcher weakens signature policy")
        return cls(
            path,
            executable_file.size,
            executable_sha256,
            stock_host_inventory_sha256,
            normalized,
            inventory,
            virtual_environment,
        )

    def stage(self, root: pathlib.Path) -> StagedLauncher:
        """Stage every admitted byte under one private non-writable capability."""

        capability = root / ".launcher-capability"
        try:
            capability.mkdir(mode=0o700)
            executable = capability / "stock-host"
            if self.virtual_environment:
                (capability / "bin").mkdir(mode=0o700)
                executable = capability / "bin" / self.executable.name
            for item in self.inventory:
                if item.logical_path == pathlib.PurePosixPath("executable"):
                    destination = executable
                    mode = 0o500
                else:
                    destination = capability.joinpath(*item.logical_path.parts)
                    destination.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
                    mode = 0o400
                staged = stage_content_identified_file(
                    item.source,
                    destination,
                    expected_size=item.size,
                    expected_sha256=item.sha256,
                    limit_bytes=(
                        MAX_LAUNCHER_BYTES
                        if item.logical_path == pathlib.PurePosixPath("executable")
                        else MAX_MODULE_FILE_BYTES
                    ),
                    mode=mode,
                )
                staged.close()
            directories = sorted(
                (path for path in capability.rglob("*") if path.is_dir()),
                key=lambda path: len(path.parts),
                reverse=True,
            )
            for directory in directories:
                directory.chmod(0o500)
            capability.chmod(0o500)
            return StagedLauncher(
                self,
                executable,
                executable if self.virtual_environment else None,
            )
        except (FileAdmissionError, OSError) as error:
            raise LauncherError("stock DuckDB host inventory could not be staged") from error

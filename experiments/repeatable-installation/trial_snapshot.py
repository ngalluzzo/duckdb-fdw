"""Freeze provider data and the Query host harness before verification.

The external Python hosts and provider verifier remain executable interfaces.
Every file whose bytes authorize or reach DuckDB is copied into one private
trial root, verified there, and used only from that immutable snapshot.
"""

from __future__ import annotations

import os
import pathlib
import stat
from dataclasses import dataclass

from trial_inputs import MAX_INPUT_FILE_BYTES, TrialInputs


@dataclass(frozen=True)
class TrialSnapshot:
    """Private paths that remain stable for the complete scenario matrix."""

    inputs: TrialInputs
    query_host: pathlib.Path


def _copy_read_only(source: pathlib.Path, destination: pathlib.Path) -> pathlib.Path:
    """Copy one still-regular source through a bounded no-follow descriptor."""

    destination.parent.mkdir(parents=True, exist_ok=True)
    flags = os.O_RDONLY | os.O_NONBLOCK | getattr(os, "O_NOFOLLOW", 0)
    descriptor = os.open(source, flags)
    copied = 0
    try:
        metadata = os.fstat(descriptor)
        if not stat.S_ISREG(metadata.st_mode):
            raise AssertionError(f"snapshot source is not a regular file: {source}")
        if not 0 < metadata.st_size <= MAX_INPUT_FILE_BYTES:
            raise AssertionError(
                f"snapshot source size is outside the bound: {source}"
            )
        with os.fdopen(descriptor, "rb", closefd=False) as input_file, destination.open(
            "xb"
        ) as output_file:
            while True:
                block = input_file.read(1024 * 1024)
                if not block:
                    break
                copied += len(block)
                if copied > MAX_INPUT_FILE_BYTES:
                    raise AssertionError(f"snapshot source grew beyond its bound: {source}")
                output_file.write(block)
    except BaseException:
        destination.unlink(missing_ok=True)
        raise
    finally:
        os.close(descriptor)
    os.chmod(destination, 0o444)
    return destination


def _freeze_directories(root: pathlib.Path) -> None:
    """Remove ordinary write authority from every snapshotted directory entry."""

    directories = sorted(
        (path for path in root.rglob("*") if path.is_dir()),
        key=lambda path: len(path.parts),
        reverse=True,
    )
    for directory in directories:
        os.chmod(directory, 0o555)
    os.chmod(root, 0o555)


def create_trial_snapshot(
    inputs: TrialInputs,
    query_host: pathlib.Path,
    output: pathlib.Path,
) -> TrialSnapshot:
    """Copy all byte-bearing inputs once into a new private directory.

    Verification runs only after this copy. A concurrent or later source-path
    change can therefore produce at most a snapshot that verification rejects;
    it cannot change bytes between verification and native loading.
    """

    output.mkdir(mode=0o700)
    bundle = output / "bundle"
    fixtures = output / "fixtures"
    artifact = _copy_read_only(inputs.artifact, bundle / inputs.artifact.name)
    manifest = _copy_read_only(inputs.manifest, bundle / inputs.manifest.name)
    manifest_anchor = _copy_read_only(
        inputs.manifest_anchor,
        bundle / inputs.manifest_anchor.name,
    )
    inventory = _copy_read_only(
        inputs.negative_fixture_inventory,
        fixtures / inputs.negative_fixture_inventory.name,
    )
    wrong_platform = _copy_read_only(
        inputs.wrong_platform_artifact,
        fixtures / inputs.wrong_platform_artifact.name,
    )
    corrupted = _copy_read_only(
        inputs.corrupted_artifact,
        fixtures / "corrupted" / inputs.corrupted_artifact.name,
    )
    frozen_host = _copy_read_only(query_host, output / "harness/query_host.py")
    frozen_verifier = _copy_read_only(
        inputs.verifier,
        output / "verifier/provider_verifier.py",
    )
    snapshot_inputs = TrialInputs(
        supported_python=inputs.supported_python,
        mismatch_python=inputs.mismatch_python,
        artifact=artifact,
        manifest=manifest,
        manifest_anchor=manifest_anchor,
        verifier=frozen_verifier,
        negative_fixture_inventory=inventory,
        wrong_platform_artifact=wrong_platform,
        corrupted_artifact=corrupted,
    )
    _freeze_directories(output)
    return TrialSnapshot(inputs=snapshot_inputs, query_host=frozen_host)

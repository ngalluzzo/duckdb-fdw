"""Run the provider verifier through Query's bounded subprocess boundary."""

from __future__ import annotations

import pathlib
import subprocess
import sys
import tempfile

from host_process import (
    PROCESS_OUTPUT_LIMIT_BYTES,
    ProcessOutputLimitExceeded,
    isolated_environment,
    run_bounded_process,
)


VERIFIER_TIMEOUT_SECONDS = 30.0
VERIFIER_DIAGNOSTIC_LIMIT = 4096


def run_provider_verifier(
    *,
    verifier: pathlib.Path,
    manifest: pathlib.Path,
    artifact: pathlib.Path,
    manifest_anchor: pathlib.Path,
    release_artifact: pathlib.Path,
    wrong_platform_artifact: pathlib.Path,
    corrupted_artifact: pathlib.Path,
    timeout_seconds: float = VERIFIER_TIMEOUT_SECONDS,
    output_limit_bytes: int = PROCESS_OUTPUT_LIMIT_BYTES,
) -> subprocess.CompletedProcess[str]:
    """Execute one verifier exchange without caller configuration or secrets."""

    command = [
        sys.executable,
        "-I",
        str(verifier),
        str(manifest),
        str(artifact),
        str(manifest_anchor),
    ]
    with tempfile.TemporaryDirectory(
        prefix="duckdb-api-verifier-"
    ) as directory:
        verifier_root = pathlib.Path(directory).resolve(strict=True)
        try:
            completed = run_bounded_process(
                command,
                cwd=verifier_root,
                environment=isolated_environment(verifier_root),
                timeout_seconds=timeout_seconds,
                output_limit_bytes=output_limit_bytes,
            )
        except subprocess.TimeoutExpired as error:
            raise AssertionError(
                f"provider verifier timed out after {timeout_seconds:g} seconds; "
                "the child was reaped and its process group was killed"
            ) from error
        except ProcessOutputLimitExceeded as error:
            raise AssertionError(
                f"provider verifier exceeded the {output_limit_bytes}-byte output "
                "limit; the child was reaped and its process group was killed"
            ) from error

        replacement_candidates = (
                (str(release_artifact), "<release-artifact>"),
                (str(corrupted_artifact), "<corrupted-artifact>"),
                (str(wrong_platform_artifact), "<wrong-platform-artifact>"),
                (str(manifest), "<release-manifest>"),
                (str(manifest_anchor), "<manifest-anchor>"),
                (str(verifier), "<provider-verifier>"),
                (str(corrupted_artifact.parent), "<fixture-root>"),
                (str(wrong_platform_artifact.parent), "<fixture-root>"),
                (str(release_artifact.parent), "<input-root>"),
                (str(manifest.parent), "<input-root>"),
                (str(verifier.parent), "<provider-root>"),
                (str(verifier_root), "<verifier-root>"),
        )
        replacement_map: dict[str, str] = {}
        for source, replacement in replacement_candidates:
            path = pathlib.Path(source)
            for alias in (str(path), str(path.resolve(strict=True))):
                # Test fixtures can colocate bundle and fixture inputs. Keep
                # the first, most specific label for duplicate roots.
                replacement_map.setdefault(alias, replacement)
        replacements = sorted(
            replacement_map.items(),
            key=lambda item: len(item[0]),
            reverse=True,
        )

        def bounded(value: str) -> str:
            for source, replacement in replacements:
                value = value.replace(source, replacement)
            if len(value) > VERIFIER_DIAGNOSTIC_LIMIT:
                value = value[:VERIFIER_DIAGNOSTIC_LIMIT] + "\n<truncated>"
            return value

        return subprocess.CompletedProcess(
            command,
            completed.returncode,
            bounded(completed.stdout),
            bounded(completed.stderr),
        )

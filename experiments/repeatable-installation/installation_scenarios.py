"""Assert the positive and negative installation user journeys.

Each scenario owns one clean DuckDB state and judges only structured host
observations plus verified content identities. Provider verification is used
through ``trial_inputs.run_identity_bound_verifier``; DuckDB is used only through
``host_process.HostRunner``.
"""

from __future__ import annotations

import pathlib
from collections.abc import Sequence

from host_process import HostRunner
from trial_inputs import (
    EXPECTED_EXTENSION,
    EXPECTED_EXTENSION_VERSION,
    TrialInputs,
    VerifiedBundle,
    file_sha256,
    run_identity_bound_verifier,
)


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def require_duckdb_identity(
    observation: dict[str, object],
    expected: tuple[str, str],
    label: str,
) -> None:
    """Require both the host version and its ten-character source commit."""

    require(
        observation.get("duckdb") == list(expected),
        f"{label} used the wrong DuckDB host: {observation!r}",
    )


def require_extension(
    observation: dict[str, object], *, loaded: bool
) -> dict[str, object]:
    """Require the exact installed extension state for a successful action."""

    require(observation.get("ok") is True, f"DuckDB action failed: {observation!r}")
    extension = observation.get("extension")
    require(isinstance(extension, dict), "DuckDB did not report the extension")
    expected = {
        "name": EXPECTED_EXTENSION,
        "version": EXPECTED_EXTENSION_VERSION,
        "loaded": loaded,
        "installed": True,
        "install_mode": "CUSTOM_PATH",
    }
    actual = {name: extension.get(name) for name in expected}
    require(actual == expected, f"unexpected extension identity: {extension!r}")
    return extension


def installed_artifact(
    extension: dict[str, object], extension_directory: pathlib.Path
) -> pathlib.Path:
    """Resolve the installed bytes and enforce clean-directory containment."""

    value = extension.get("install_path")
    require(isinstance(value, str) and value, "DuckDB did not report an install path")
    path = pathlib.Path(value).resolve(strict=True)
    try:
        path.relative_to(extension_directory.resolve(strict=True))
    except ValueError as error:
        raise AssertionError(
            f"installed artifact escaped the clean extension directory: {path}"
        ) from error
    return path


def require_install_source(
    extension: dict[str, object],
    artifact: pathlib.Path,
    label: str,
) -> None:
    """Require DuckDB metadata to retain the admitted custom-path source."""

    value = extension.get("installed_from")
    require(
        isinstance(value, str) and value,
        f"{label} did not report its custom-path install source",
    )
    source = pathlib.Path(value)
    require(
        source.is_absolute() and source == artifact,
        f"{label} install source differs from the admitted artifact: {value}",
    )


def require_no_registration(observation: dict[str, object], label: str) -> str:
    """Require a refusal before metadata or functions become visible."""

    require(observation.get("ok") is False, f"{label} unexpectedly succeeded")
    require(
        observation.get("registered_functions") == [],
        f"{label} registered extension functions: {observation!r}",
    )
    require(
        observation.get("extension") is None,
        f"{label} left installed extension metadata: {observation!r}",
    )
    diagnostic = observation.get("diagnostic")
    require(
        isinstance(diagnostic, str) and diagnostic.strip(),
        f"{label} had no diagnostic",
    )
    return diagnostic


def require_fragments(
    diagnostic: str, label: str, fragments: Sequence[str]
) -> None:
    """Match stable actionable facts without freezing upstream prose."""

    lowered = diagnostic.lower()
    missing = [fragment for fragment in fragments if fragment.lower() not in lowered]
    require(not missing, f"{label} diagnostic lacks {missing!r}: {diagnostic!r}")


def extension_files(directory: pathlib.Path) -> list[str]:
    """Inventory installable artifacts left in one rejected clean state."""

    return sorted(
        str(path.relative_to(directory))
        for path in directory.rglob("*.duckdb_extension")
    )


def clean_state(root: pathlib.Path, name: str) -> tuple[pathlib.Path, pathlib.Path]:
    """Allocate one database and extension directory for one scenario."""

    state = root / name
    state.mkdir()
    extension_directory = state / "extensions"
    extension_directory.mkdir()
    return state / "trial.duckdb", extension_directory


def supported_scenario(
    runner: HostRunner,
    inputs: TrialInputs,
    verified: VerifiedBundle,
    root: pathlib.Path,
) -> dict[str, object]:
    """Install, repeat, restart, load by name, and query the accepted artifact."""

    database, extensions = clean_state(root, "supported")
    first = runner.run(
        inputs.supported_python,
        "install",
        database,
        extensions,
        artifact=inputs.artifact,
        allow_unsigned=True,
    )
    require_duckdb_identity(first, verified.supported_duckdb_identity, "first install")
    first_extension = require_extension(first, loaded=False)
    require_install_source(first_extension, inputs.artifact, "first install")
    require(
        first.get("registered_functions") == [],
        "INSTALL unexpectedly loaded the extension",
    )
    first_path = installed_artifact(first_extension, extensions)
    first_digest = file_sha256(first_path)
    require(
        first_digest == verified.artifact_sha256,
        "installed bytes differ from the verified input",
    )
    repeated = runner.run(
        inputs.supported_python,
        "install",
        database,
        extensions,
        artifact=inputs.artifact,
        allow_unsigned=True,
    )
    require_duckdb_identity(
        repeated,
        verified.supported_duckdb_identity,
        "repeated install",
    )
    repeated_extension = require_extension(repeated, loaded=False)
    require_install_source(repeated_extension, inputs.artifact, "repeated install")
    require(
        repeated.get("registered_functions") == [],
        "repeated INSTALL unexpectedly loaded the extension",
    )
    repeated_path = installed_artifact(repeated_extension, extensions)
    repeated_digest = file_sha256(repeated_path)
    require(
        repeated_path == first_path,
        "repeated INSTALL changed the destination path",
    )
    require(
        repeated_digest == verified.artifact_sha256,
        "repeated INSTALL changed the installed bytes",
    )
    require(
        repeated_extension == first_extension,
        "repeated INSTALL changed DuckDB extension metadata",
    )

    loaded = runner.run(
        inputs.supported_python,
        "load-query",
        database,
        extensions,
        allow_unsigned=True,
    )
    require_duckdb_identity(
        loaded,
        verified.supported_duckdb_identity,
        "load by name",
    )
    loaded_extension = require_extension(loaded, loaded=True)
    require_install_source(loaded_extension, inputs.artifact, "load by name")
    require(
        loaded.get("behavior") == verified.public_contract,
        "load-by-name behavior differs from the verified manifest contract",
    )
    require(
        loaded.get("registered_functions") == [["duckdb_api_scan", "table"]],
        f"unexpected loaded function inventory: {loaded.get('registered_functions')!r}",
    )
    require(
        file_sha256(installed_artifact(loaded_extension, extensions))
        == verified.artifact_sha256,
        "load-by-name changed the installed artifact",
    )

    process_ids = [first.get("pid"), repeated.get("pid"), loaded.get("pid")]
    require(
        all(isinstance(pid, int) and pid > 0 for pid in process_ids),
        "host PID evidence is missing",
    )
    require(
        [first.get("action"), repeated.get("action"), loaded.get("action")]
        == ["install", "install", "load-query"],
        "supported lifecycle actions were not observed in order",
    )
    return {
        "first_install": first,
        "installed_sha256": first_digest,
        "load_by_name": loaded,
        "process_ids": process_ids,
        "repeated_install": repeated,
    }


def rejected_install_scenario(
    runner: HostRunner,
    python: pathlib.Path,
    artifact: pathlib.Path,
    root: pathlib.Path,
    name: str,
    *,
    allow_unsigned: bool,
    expected_duckdb_identity: tuple[str, str],
    required_diagnostic_fragments: Sequence[str],
) -> dict[str, object]:
    """Require one clean host to reject before installation or registration."""

    database, extensions = clean_state(root, name)
    observation = runner.run(
        python,
        "install",
        database,
        extensions,
        artifact=artifact,
        allow_unsigned=allow_unsigned,
    )
    require_duckdb_identity(observation, expected_duckdb_identity, name)
    diagnostic = require_no_registration(observation, name)
    require_fragments(diagnostic, name, required_diagnostic_fragments)
    require(not extension_files(extensions), f"{name} wrote an installable artifact")
    return observation


def _verifier_summary(returncode: int, stdout: str, stderr: str) -> dict[str, object]:
    return {
        "returncode": returncode,
        "stderr": stderr.strip(),
        "stdout": stdout.strip(),
    }


def run_installation_scenarios(
    runner: HostRunner,
    inputs: TrialInputs,
    verified: VerifiedBundle,
    root: pathlib.Path,
) -> dict[str, object]:
    """Run the complete ordered scenario matrix and return raw observations."""

    supported = supported_scenario(
        runner,
        inputs,
        verified,
        root,
    )
    signature = rejected_install_scenario(
        runner,
        inputs.supported_python,
        inputs.artifact,
        root,
        "default-signature-policy",
        allow_unsigned=False,
        expected_duckdb_identity=verified.supported_duckdb_identity,
        required_diagnostic_fragments=("valid signature", "securing_extensions"),
    )

    mismatch = rejected_install_scenario(
        runner,
        inputs.mismatch_python,
        inputs.artifact,
        root,
        "mismatched-duckdb",
        allow_unsigned=True,
        expected_duckdb_identity=verified.mismatched_duckdb_identity,
        required_diagnostic_fragments=(
            verified.supported_duckdb_identity[0],
            verified.mismatched_duckdb_identity[0],
        ),
    )

    wrong_platform = rejected_install_scenario(
        runner,
        inputs.supported_python,
        inputs.wrong_platform_artifact,
        root,
        "wrong-platform",
        allow_unsigned=True,
        expected_duckdb_identity=verified.supported_duckdb_identity,
        required_diagnostic_fragments=(
            "platform",
            verified.source_platform,
            verified.wrong_platform,
        ),
    )

    invocations_before_corruption = runner.invocations
    corrupted_verification, _ = run_identity_bound_verifier(
        inputs,
        inputs.corrupted_artifact,
        expected_sha256=verified.verifier_sha256,
    )
    require(
        corrupted_verification.returncode != 0,
        "provider verifier accepted the corrupted release artifact",
    )
    corrupted_diagnostic = (
        corrupted_verification.stderr.strip()
        or corrupted_verification.stdout.strip()
    )
    require(corrupted_diagnostic, "corrupted artifact refusal had no diagnostic")
    require_fragments(
        corrupted_diagnostic,
        "corrupted artifact",
        ("artifact", "tracked trust record"),
    )
    require(
        runner.invocations == invocations_before_corruption,
        "corrupted artifact started a DuckDB host before verification",
    )

    return {
        "corrupted_artifact": {
            "query_host_invocations": 0,
            "verification": _verifier_summary(
                corrupted_verification.returncode,
                corrupted_verification.stdout,
                corrupted_verification.stderr,
            ),
        },
        "default_signature_policy": signature,
        "mismatched_duckdb": mismatch,
        "supported": supported,
        "wrong_platform": wrong_platform,
    }

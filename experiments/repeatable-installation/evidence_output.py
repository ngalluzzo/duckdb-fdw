"""Build path-normalized retained evidence for the installation trial.

All real path and byte assertions finish before this module is called. It owns
only the versioned evidence shape and recursive normalization needed to retain
results without leaking machine-local roots.
"""

from __future__ import annotations

import os
import pathlib
from collections.abc import Sequence

from trial_inputs import TrialInputs, VerifiedBundle


SCHEMA = "duckdb_api/repeatable-installation-evidence/v1"


def redact_evidence(
    value: object, replacements: Sequence[tuple[pathlib.Path, str]]
) -> object:
    """Recursively normalize machine-local paths after assertions finish.

    Longer paths take precedence so a trial input root can remain distinct from
    its containing repository. This function changes retained evidence, never
    the paths used for containment or content-identity checks.
    """

    normalized = sorted(
        ((str(path), placeholder) for path, placeholder in replacements),
        key=lambda item: len(item[0]),
        reverse=True,
    )
    if isinstance(value, str):
        for path, placeholder in normalized:
            value = value.replace(path, placeholder)
        return value
    if isinstance(value, list):
        return [redact_evidence(item, replacements) for item in value]
    if isinstance(value, dict):
        return {
            key: redact_evidence(item, replacements)
            for key, item in value.items()
        }
    return value


def evidence_replacements(
    inputs: TrialInputs, trial_root: pathlib.Path, repository: pathlib.Path
) -> list[tuple[pathlib.Path, str]]:
    """Name every transient root that may appear in retained observations."""

    input_parents = [
        inputs.artifact.parent,
        inputs.manifest.parent,
        inputs.manifest_anchor.parent,
        inputs.negative_fixture_inventory.parent,
        inputs.wrong_platform_artifact.parent,
        inputs.corrupted_artifact.parent,
    ]
    common_input = pathlib.Path(os.path.commonpath(input_parents))
    replacements = [
        (trial_root, "<trial-root>"),
        (repository, "<repository-root>"),
    ]
    if common_input != pathlib.Path(common_input.anchor):
        replacements.append((common_input, "<input-root>"))
    else:
        # Unrelated absolute input roots have no useful common parent. Preserve
        # stable role names without replacing the filesystem root itself.
        replacements.extend(
            [
                (inputs.artifact.parent, "<input-root>"),
                (inputs.manifest.parent, "<manifest-root>"),
                (inputs.manifest_anchor.parent, "<anchor-root>"),
                (inputs.negative_fixture_inventory.parent, "<fixture-root>"),
                (inputs.wrong_platform_artifact.parent, "<fixture-root>"),
                (inputs.corrupted_artifact.parent, "<fixture-root>"),
            ]
        )
    return replacements


def build_retained_evidence(
    inputs: TrialInputs,
    verified: VerifiedBundle,
    scenarios: dict[str, object],
    trial_root: pathlib.Path,
    repository: pathlib.Path,
) -> dict[str, object]:
    """Compose the stable evidence schema and normalize its transient roots."""

    result = {
        "inputs": {
            "artifact_sha256": verified.artifact_sha256,
            "corrupted_artifact_sha256": verified.corrupted_artifact_sha256,
            "manifest_sha256": verified.manifest_sha256,
            "verifier_sha256": verified.verifier_sha256,
            "negative_fixture_inventory_sha256": (
                verified.negative_fixture_inventory_sha256
            ),
            "source": verified.manifest.get("source"),
            "wrong_platform_artifact_sha256": (
                verified.wrong_platform_artifact_sha256
            ),
        },
        "scenarios": scenarios,
        "schema": SCHEMA,
        "verifier": {
            "contract": "PYTHON VERIFIER MANIFEST ARTIFACT MANIFEST_ANCHOR",
            "original_bundle": "accepted",
        },
    }
    retained = redact_evidence(
        result,
        evidence_replacements(inputs, trial_root, repository),
    )
    if not isinstance(retained, dict):
        raise AssertionError("retained evidence is not an object")
    return retained

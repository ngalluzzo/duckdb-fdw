"""Compose the supported and representative-refusal Community scenarios.

This module owns scenario ordering and cross-scenario isolation.  It delegates
one-process mechanics to ``host_action`` and the observable state laws to the
existing ``lifecycle`` contract.  Supported and incompatible runs use separate
runner instances and caller-owned state roots; no failed row can inherit a
successful installation or process identity.
"""

from __future__ import annotations

from dataclasses import dataclass
import re
from typing import Any
from typing import Protocol

try:
    from .lifecycle import (
        HostObservation,
        HostRunner,
        LifecycleError,
        SupportedLifecycle,
        verify_incompatible_refusal,
        verify_supported_lifecycle,
    )
    from .matrix import RowIdentity
except ImportError:
    from lifecycle import (
        HostObservation,
        HostRunner,
        LifecycleError,
        SupportedLifecycle,
        verify_incompatible_refusal,
        verify_supported_lifecycle,
    )
    from matrix import RowIdentity


SHA256 = re.compile(r"[0-9a-f]{64}")


class InitializationProbe(Protocol):
    """Independent artifact-specific evidence that native init did not run."""

    evidence_sha256: str

    def arm(self) -> None:
        """Prepare the independent observable immediately before refusal."""

    def assert_not_initialized(self) -> None:
        """Fail when the incompatible artifact's initializer was observed."""


@dataclass(frozen=True)
class ScenarioResult:
    """Completed observations whose assertions all ran before evidence output."""

    supported: SupportedLifecycle
    incompatible: HostObservation
    initialization_probe_sha256: str


def run_scenarios(
    *,
    supported_runner: HostRunner,
    incompatible_runner: HostRunner,
    supported_row: RowIdentity,
    incompatible_row: RowIdentity,
    artifact_sha256: str,
    public_contract: Any,
    required_incompatible_facts: tuple[str, ...],
    initialization_probe: InitializationProbe,
    forbidden_diagnostic_values: tuple[str, ...] = (),
) -> ScenarioResult:
    """Run all lifecycle actions and require process/state isolation.

    Runner instances are intentionally explicit because a representative
    compatibility refusal may use a different stock DuckDB launcher or
    platform.  Each runner owns cancellation and close behavior for its
    processes; this function owns only ordering and the fact that the refusal
    cannot reuse a successful process observation.
    """

    if not required_incompatible_facts:
        raise LifecycleError("incompatible scenario requires actionable identity facts")
    if SHA256.fullmatch(initialization_probe.evidence_sha256) is None:
        raise LifecycleError("initialization probe evidence is not content-identified")
    supported = verify_supported_lifecycle(
        supported_runner,
        supported_row,
        artifact_sha256,
        public_contract,
        state_id="supported",
    )
    try:
        initialization_probe.arm()
    except Exception as error:
        raise LifecycleError("initialization probe could not be armed") from error
    incompatible = verify_incompatible_refusal(
        incompatible_runner,
        incompatible_row,
        required_facts=required_incompatible_facts,
        forbidden=forbidden_diagnostic_values,
        state_id="incompatible",
    )
    try:
        initialization_probe.assert_not_initialized()
    except Exception as error:
        raise LifecycleError(
            "independent probe observed incompatible native initialization"
        ) from error
    supported_tokens = {
        observation.process_token for observation in supported.observations
    }
    if incompatible.process_token in supported_tokens:
        raise LifecycleError(
            "incompatible scenario reused a supported lifecycle process"
        )
    return ScenarioResult(
        supported=supported,
        incompatible=incompatible,
        initialization_probe_sha256=initialization_probe.evidence_sha256,
    )

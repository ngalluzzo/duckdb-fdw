"""Query-owned lifecycle assertions over a bounded stock-host runner.

Each runner call represents one fresh DuckDB process. The runner owns process
creation, time/output limits, environment isolation, and teardown. This module
owns only the observable install/reinstall/restart/load/query and refusal law.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Any, Protocol

try:
    from .matrix import RowIdentity
except ImportError:
    from matrix import RowIdentity


class LifecycleError(AssertionError):
    """A host observation does not prove the Community user lifecycle."""


@dataclass(frozen=True)
class ExtensionObservation:
    name: str
    version: str
    installed: bool
    loaded: bool
    install_source: str
    install_path: str
    artifact_sha256: str


@dataclass(frozen=True)
class HostObservation:
    action: str
    process_token: str
    ok: bool
    row: RowIdentity
    allow_unsigned_extensions: bool
    extension: ExtensionObservation | None
    function_registered: bool
    behavior: Any | None = None
    diagnostic_category: str | None = None
    diagnostic: str | None = None


class HostRunner(Protocol):
    def run(self, action: str, state_id: str) -> HostObservation:
        """Run exactly one action in a fresh process over the named host state."""


@dataclass(frozen=True)
class SupportedLifecycle:
    observations: tuple[HostObservation, ...]


def _base(
    observation: HostObservation,
    action: str,
    row: RowIdentity,
    process_tokens: set[str],
) -> None:
    if observation.action != action:
        raise LifecycleError(f"{action} returned an observation for another action")
    if not observation.process_token or observation.process_token in process_tokens:
        raise LifecycleError("lifecycle actions did not use distinct fresh processes")
    process_tokens.add(observation.process_token)
    if observation.row != row:
        raise LifecycleError(f"{action} used the wrong DuckDB or platform row")
    if observation.allow_unsigned_extensions:
        raise LifecycleError(f"{action} weakened DuckDB signature policy")


def _successful_extension(
    observation: HostObservation,
    *,
    loaded: bool,
    registered: bool,
    artifact_sha256: str,
) -> ExtensionObservation:
    if not observation.ok or observation.extension is None:
        raise LifecycleError(f"{observation.action} did not expose the extension")
    extension = observation.extension
    if (
        extension.name != "duckdb_api"
        or extension.version != "0.2.0"
        or not extension.installed
        or extension.loaded is not loaded
        or extension.install_source != "community"
        or extension.artifact_sha256 != artifact_sha256
        or not extension.install_path
    ):
        raise LifecycleError(f"{observation.action} extension identity drifted")
    if observation.function_registered is not registered:
        raise LifecycleError(f"{observation.action} registration state drifted")
    if observation.diagnostic_category is not None or observation.diagnostic is not None:
        raise LifecycleError(f"{observation.action} reported a diagnostic on success")
    return extension


def verify_supported_lifecycle(
    runner: HostRunner,
    row: RowIdentity,
    artifact_sha256: str,
    public_contract: Any,
    *,
    state_id: str = "supported",
) -> SupportedLifecycle:
    """Prove empty state, install, repeat install, restart, load, and query."""

    process_tokens: set[str] = set()
    observations: list[HostObservation] = []
    for action in ("pre_install", "install", "repeat_install", "load_query"):
        observation = runner.run(action, state_id)
        _base(observation, action, row, process_tokens)
        observations.append(observation)

    pre_install, install, repeat_install, load_query = observations
    if (
        not pre_install.ok
        or pre_install.extension is not None
        or pre_install.function_registered
        or pre_install.behavior is not None
        or pre_install.diagnostic is not None
        or pre_install.diagnostic_category is not None
    ):
        raise LifecycleError("pre-install state was not empty")

    installed = _successful_extension(
        install,
        loaded=False,
        registered=False,
        artifact_sha256=artifact_sha256,
    )
    if install.behavior is not None:
        raise LifecycleError("INSTALL unexpectedly executed the public query")
    repeated = _successful_extension(
        repeat_install,
        loaded=False,
        registered=False,
        artifact_sha256=artifact_sha256,
    )
    if repeat_install.behavior is not None:
        raise LifecycleError("repeated INSTALL unexpectedly executed the public query")
    if repeated.install_path != installed.install_path:
        raise LifecycleError("repeated INSTALL changed the installed destination")

    loaded = _successful_extension(
        load_query,
        loaded=True,
        registered=True,
        artifact_sha256=artifact_sha256,
    )
    if loaded.install_path != installed.install_path:
        raise LifecycleError("LOAD did not use the repeatedly installed artifact")
    if load_query.behavior != public_contract:
        raise LifecycleError("loaded query behavior differs from the public contract")
    return SupportedLifecycle(tuple(observations))


def verify_incompatible_refusal(
    runner: HostRunner,
    row: RowIdentity,
    *,
    required_facts: tuple[str, ...],
    forbidden: tuple[str, ...] = (),
    state_id: str = "incompatible",
) -> HostObservation:
    """Require an actionable pre-initialization version or platform refusal."""

    observation = runner.run("incompatible", state_id)
    _base(observation, "incompatible", row, set())
    if observation.ok:
        raise LifecycleError("incompatible artifact unexpectedly succeeded")
    if observation.extension is not None or observation.function_registered:
        raise LifecycleError("incompatible artifact reached installation or registration")
    if observation.behavior is not None:
        raise LifecycleError("incompatible artifact executed the public query")
    if observation.diagnostic_category not in {"version", "platform"}:
        raise LifecycleError("incompatible refusal lacks a stable diagnostic category")
    diagnostic = observation.diagnostic
    if not diagnostic or any(fact not in diagnostic for fact in required_facts):
        raise LifecycleError("incompatible refusal lacks actionable identity facts")
    if any(value in diagnostic for value in forbidden):
        raise LifecycleError("incompatible refusal disclosed private context")
    return observation

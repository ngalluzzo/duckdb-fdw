"""Validate one stock-host JSON frame and normalize its diagnostics.

The protocol admits only exact v1 fields and corroborates the content-bound
launcher with DuckDB's runtime version, abbreviated commit, and platform.
Unknown fields are rejected so child output cannot silently add environment or
secret data to retained Query evidence.
"""

from __future__ import annotations

import json
import os
import pathlib
import re
from collections.abc import Sequence
from typing import Any

try:
    from .lifecycle import ExtensionObservation, HostObservation
    from .matrix import RowIdentity
except ImportError:
    from lifecycle import ExtensionObservation, HostObservation
    from matrix import RowIdentity


HOST_OBSERVATION_SCHEMA = "duckdb_api/community-host-observation/v1"
DIAGNOSTIC_LIMIT_CHARACTERS = 4096
SHA256 = re.compile(r"[0-9a-f]{64}")
VERSION_FACT = re.compile(r"\bv?[0-9]+\.[0-9]+\.[0-9]+\b")
PLATFORM_FACT = re.compile(
    r"\b(?:linux|osx|windows)_[a-z0-9]+(?:_[a-z0-9]+)*\b",
    flags=re.IGNORECASE,
)


class ProtocolError(ValueError):
    """The child frame is malformed, ambiguous, or names the wrong row."""


def _path_forms(path: pathlib.Path) -> set[str]:
    forms = {str(path.expanduser().absolute())}
    try:
        forms.add(str(path.resolve(strict=True)))
    except OSError:
        pass
    return {value.rstrip(os.sep) for value in forms if value.rstrip(os.sep)}


def normalize_diagnostic(
    value: str,
    replacements: Sequence[tuple[pathlib.Path, str]],
    *,
    limit: int = DIAGNOSTIC_LIMIT_CHARACTERS,
) -> str:
    normalized: list[tuple[str, str]] = []
    for root, placeholder in replacements:
        normalized.extend((form, placeholder) for form in _path_forms(root))
    for source, replacement in sorted(
        set(normalized), key=lambda item: len(item[0]), reverse=True
    ):
        value = value.replace(source, replacement)
    if len(value) > limit:
        value = value[:limit] + "\n<truncated>"
    return value


def safe_diagnostic(
    value: str,
    category: str,
    replacements: Sequence[tuple[pathlib.Path, str]],
) -> str:
    """Retain only actionable public identity facts from an opaque error."""

    normalized = normalize_diagnostic(value, replacements)
    facts: list[str] = []
    if "duckdb_api" in normalized.lower():
        facts.append("duckdb_api")
    for pattern in (VERSION_FACT, PLATFORM_FACT):
        for fact in pattern.findall(normalized):
            if fact not in facts:
                facts.append(fact)
    suffix = f": {', '.join(facts)}" if facts else ""
    return f"{category} refusal{suffix}"


def _exact_object(value: Any, keys: set[str], label: str) -> dict[str, Any]:
    if not isinstance(value, dict) or set(value) != keys:
        raise ProtocolError(f"stock host {label} has an invalid shape")
    return value


def _string(value: Any, label: str) -> str:
    if not isinstance(value, str) or not value:
        raise ProtocolError(f"stock host {label} is not a non-empty string")
    return value


def _extension(value: Any) -> ExtensionObservation | None:
    if value is None:
        return None
    extension = _exact_object(
        value,
        {
            "artifact_sha256",
            "install_path",
            "install_source",
            "installed",
            "loaded",
            "name",
            "version",
        },
        "extension observation",
    )
    digest = _string(extension["artifact_sha256"], "artifact digest")
    if SHA256.fullmatch(digest) is None:
        raise ProtocolError("stock host artifact digest is malformed")
    if type(extension["installed"]) is not bool or type(extension["loaded"]) is not bool:
        raise ProtocolError("stock host extension state is malformed")
    return ExtensionObservation(
        name=_string(extension["name"], "extension name"),
        version=_string(extension["version"], "extension version"),
        installed=extension["installed"],
        loaded=extension["loaded"],
        install_source=_string(extension["install_source"], "extension source"),
        install_path=_string(extension["install_path"], "extension path"),
        artifact_sha256=digest,
    )


def parse_observation(
    stdout: str,
    expected_row: RowIdentity,
    replacements: Sequence[tuple[pathlib.Path, str]],
) -> HostObservation:
    """Parse exactly one JSON object and return the existing lifecycle type."""

    try:
        value = json.loads(stdout)
    except json.JSONDecodeError as error:
        raise ProtocolError("stock host did not emit one JSON observation") from error
    observation = _exact_object(
        value,
        {
            "action",
            "allow_unsigned_extensions",
            "behavior",
            "diagnostic",
            "diagnostic_category",
            "duckdb",
            "extension",
            "function_registered",
            "ok",
            "platform",
            "process_token",
            "schema",
        },
        "observation",
    )
    if observation["schema"] != HOST_OBSERVATION_SCHEMA:
        raise ProtocolError("stock host observation schema drifted")
    expected_duckdb = [
        f"v{expected_row.duckdb.version}",
        expected_row.duckdb.commit[:10],
    ]
    if observation["duckdb"] != expected_duckdb or observation["platform"] != expected_row.platform:
        raise ProtocolError("stock host used the wrong DuckDB or platform identity")
    for field in ("ok", "allow_unsigned_extensions", "function_registered"):
        if type(observation[field]) is not bool:
            raise ProtocolError("stock host boolean observation is malformed")
    category = observation["diagnostic_category"]
    diagnostic = observation["diagnostic"]
    if category not in {None, "installation", "platform", "version"}:
        raise ProtocolError("stock host diagnostic category is malformed")
    if (category is None) != (diagnostic is None):
        raise ProtocolError("stock host diagnostic fields are inconsistent")
    if diagnostic is not None:
        if not isinstance(diagnostic, str) or not diagnostic.strip():
            raise ProtocolError("stock host diagnostic is malformed")
        assert isinstance(category, str)
        diagnostic = safe_diagnostic(diagnostic, category, replacements)
    return HostObservation(
        action=_string(observation["action"], "action"),
        process_token=_string(observation["process_token"], "process token"),
        ok=observation["ok"],
        row=expected_row,
        allow_unsigned_extensions=observation["allow_unsigned_extensions"],
        extension=_extension(observation["extension"]),
        function_registered=observation["function_registered"],
        behavior=observation["behavior"],
        diagnostic_category=category,
        diagnostic=diagnostic,
    )

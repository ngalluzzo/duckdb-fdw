"""Canonical syntax and semantics for the tracked Community description.yml."""

from __future__ import annotations

import re
from typing import Any

from record_format import AdmissionError, require


PROJECT_DESCRIPTION = (
    "Loads a declarative connector package and exposes its typed relations as "
    "generated DuckDB table functions."
)
APPROVED_MAINTAINERS = ["ngalluzzo"]
SAFE_YAML_VALUE = re.compile(r"[A-Za-z0-9][A-Za-z0-9+._/' -]*")


def _mapping(value: object, label: str) -> dict[str, Any]:
    require(isinstance(value, dict), f"{label} must be an object")
    return value


def parse_description(payload: bytes) -> dict[str, Any]:
    """Parse only canonical scalar mappings and one maintainer list.

    Excluding general YAML aliases, tags, implicit types, and merge keys keeps
    proposal meaning independent of a YAML runtime and rejects ambiguity.
    """

    diagnostic = "Community descriptor proposal is not canonical exact YAML"
    try:
        text = payload.decode("utf-8")
    except UnicodeDecodeError as error:
        raise AdmissionError(diagnostic) from error
    result: dict[str, dict[str, Any]] = {}
    current: dict[str, Any] | None = None
    active_list: list[str] | None = None
    for line in text.splitlines():
        section = re.fullmatch(r"([a-z][a-z_]*)\:", line)
        if section is not None:
            name = section.group(1)
            require(name not in result, diagnostic)
            current = {}
            result[name] = current
            active_list = None
            continue
        item = re.fullmatch(r"    - (.+)", line)
        if item is not None:
            value = item.group(1)
            require(active_list is not None and SAFE_YAML_VALUE.fullmatch(value) is not None,
                    diagnostic)
            active_list.append(value)
            continue
        assignment = re.fullmatch(r"  ([a-z][a-z_]*):(.*)", line)
        require(current is not None and assignment is not None, diagnostic)
        key = assignment.group(1)
        require(key not in current, diagnostic)
        remainder = assignment.group(2)
        if remainder == "":
            require(key == "maintainers", diagnostic)
            active_list = []
            current[key] = active_list
            continue
        require(
            remainder.startswith(" ")
            and SAFE_YAML_VALUE.fullmatch(remainder[1:]) is not None,
            diagnostic,
        )
        current[key] = remainder[1:]
        active_list = None
    return result


def expected_description(
    candidate: dict[str, Any], pins: dict[str, Any]
) -> dict[str, Any]:
    project = _mapping(pins["project"], "project pins")
    license_pin = _mapping(project["license"], "project license pin")
    repository = project["repository"]
    require(isinstance(repository, str) and repository.startswith("https://github.com/"),
            "project repository cannot form Community descriptor metadata")
    source = _mapping(candidate["source"], "candidate source")
    return {
        "extension": {
            "name": project["extension"],
            "description": PROJECT_DESCRIPTION,
            "version": project["version"],
            "language": "C++",
            "build": "cmake",
            "license": license_pin["spdx"],
            "maintainers": APPROVED_MAINTAINERS,
        },
        "repo": {
            "github": repository.removeprefix("https://github.com/"),
            "ref": source["commit"],
        },
    }


def description_bytes(description: dict[str, Any]) -> bytes:
    extension = _mapping(description["extension"], "descriptor extension")
    repository = _mapping(description["repo"], "descriptor repository")
    maintainers = extension["maintainers"]
    assert isinstance(maintainers, list)
    lines = [
        "extension:",
        f"  name: {extension['name']}",
        f"  description: {extension['description']}",
        f"  version: {extension['version']}",
        f"  language: {extension['language']}",
        f"  build: {extension['build']}",
        f"  license: {extension['license']}",
        "  maintainers:",
        *(f"    - {maintainer}" for maintainer in maintainers),
        "repo:",
        f"  github: {repository['github']}",
        f"  ref: {repository['ref']}",
    ]
    return ("\n".join(lines) + "\n").encode("utf-8")


def validate_proposal(
    payload: bytes, candidate: dict[str, Any], pins: dict[str, Any]
) -> dict[str, Any]:
    parsed = parse_description(payload)
    expected = expected_description(candidate, pins)
    require(parsed == expected,
            "Community descriptor proposal fields drifted from candidate and pins")
    require(payload == description_bytes(expected),
            "Community descriptor proposal is not canonical exact YAML")
    return parsed

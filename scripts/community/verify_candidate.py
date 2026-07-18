#!/usr/bin/env python3
"""Bind one caller-supplied source commit to admitted Community provider inputs."""

from __future__ import annotations

import argparse
import pathlib
import re
import sys
from typing import Any


HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

from audit_dependencies import AUDIT_SCHEMA, validate_audit_record  # noqa: E402
from candidate_pins import validate_pins  # noqa: E402
from git_snapshot import blob, identity, tree_entries  # noqa: E402
from record_format import (  # noqa: E402
    AdmissionError,
    load_canonical_object,
    prepare_output_root,
    regular_directory,
    require,
    sha256_bytes,
    verify_anchored_object,
    write_anchored_json,
)
from candidate_record import validate_candidate_record  # noqa: E402
from descriptor_expectation import validate_expectation  # noqa: E402


CANDIDATE_SCHEMA = "duckdb_api/community-candidate/v1"
GITMODULE_SECTION = re.compile(r'[ \t]*\[submodule[ \t]+"([^"\\]+)"\][ \t]*')
GITMODULE_ASSIGNMENT = re.compile(
    r"[ \t]*([A-Za-z][A-Za-z0-9-]*)[ \t]*=[ \t]*(.*?)[ \t]*"
)


def _mapping(value: object, label: str) -> dict[str, Any]:
    require(isinstance(value, dict), f"{label} must be an object")
    return value


def _gitmodules_metadata(payload: bytes) -> dict[str, dict[str, str]]:
    """Parse the deliberately small, include-free .gitmodules contract.

    Candidate admission accepts only simple submodule sections and assignments,
    so duplicate sections/keys and Git config include behavior cannot introduce
    an ambient or ambiguous interpretation.
    """

    diagnostic = "candidate .gitmodules metadata does not match Community pins"
    try:
        text = payload.decode("utf-8")
    except UnicodeDecodeError as error:
        raise AdmissionError(diagnostic) from error
    sections: dict[str, dict[str, str]] = {}
    current: dict[str, str] | None = None
    for line in text.splitlines():
        stripped = line.strip()
        if stripped == "" or stripped.startswith(("#", ";")):
            continue
        section = GITMODULE_SECTION.fullmatch(line)
        if section is not None:
            name = section.group(1)
            require(name not in sections, diagnostic)
            current = {}
            sections[name] = current
            continue
        assignment = GITMODULE_ASSIGNMENT.fullmatch(line)
        require(current is not None and assignment is not None, diagnostic)
        key = assignment.group(1).lower()
        require(key not in current, diagnostic)
        current[key] = assignment.group(2)
    return sections


def _validate_source_layout(
    repository: pathlib.Path, commit: str, pins: dict[str, Any]
) -> None:
    entries = tree_entries(repository, commit)
    by_path = {entry.path: entry for entry in entries}
    duckdb = _mapping(pins["duckdb"], "DuckDB pins")
    ci_tools = _mapping(pins["extension_ci_tools"], "ci-tools pins")
    expected_links = {
        "duckdb": ("160000", "commit", duckdb["commit"]),
        "extension-ci-tools": ("160000", "commit", ci_tools["commit"]),
    }
    observed_links = {
        entry.path: (entry.mode, entry.object_type, entry.object_id)
        for entry in entries
        if entry.mode == "160000" or entry.object_type == "commit"
    }
    require(
        observed_links == expected_links
        and all(
            path in by_path
            and (by_path[path].mode, by_path[path].object_type, by_path[path].object_id)
            == expected_identity
            for path, expected_identity in expected_links.items()
        ),
        "candidate source gitlink layout does not match Community pins",
    )

    modules = by_path.get(".gitmodules")
    require(
        modules is not None
        and modules.mode in {"100644", "100755"}
        and modules.object_type == "blob",
        "candidate .gitmodules must be one regular blob",
    )
    metadata = _gitmodules_metadata(blob(repository, commit, ".gitmodules"))
    require(
        metadata
        == {
            "duckdb": {
                "path": "duckdb",
                "url": duckdb["repository"],
                "branch": "main",
            },
            "extension-ci-tools": {
                "path": "extension-ci-tools",
                "url": ci_tools["repository"],
                "branch": ci_tools["ref"],
            },
        },
        "candidate .gitmodules metadata does not match Community pins",
    )


def _bracket_end(text: str, offset: int) -> int | None:
    match = re.match(r"\[(=*)\[", text[offset:])
    if match is None:
        return None
    closing = "]" + match.group(1) + "]"
    end = text.find(closing, offset + len(match.group(0)))
    require(end >= 0, "extension_config.cmake has an unterminated bracket argument")
    return end + len(closing)


def _without_cmake_comments(text: str) -> str:
    result: list[str] = []
    offset = 0
    quoted = False
    while offset < len(text):
        character = text[offset]
        if quoted:
            result.append(character)
            if character == "\\" and offset + 1 < len(text):
                offset += 1
                result.append(text[offset])
            elif character == '"':
                quoted = False
            offset += 1
            continue
        if character == '"':
            quoted = True
            result.append(character)
            offset += 1
            continue
        bracket_end = _bracket_end(text, offset) if character == "[" else None
        if bracket_end is not None:
            result.append(text[offset:bracket_end])
            offset = bracket_end
            continue
        if character != "#":
            result.append(character)
            offset += 1
            continue

        comment_start = offset
        bracket_end = _bracket_end(text, offset + 1)
        if bracket_end is not None:
            offset = bracket_end
        else:
            newline = text.find("\n", offset)
            offset = len(text) if newline < 0 else newline
        result.extend("\n" if item == "\n" else " " for item in text[comment_start:offset])
    require(not quoted, "extension_config.cmake has an unterminated quoted argument")
    return "".join(result)


def _quoted_end(text: str, offset: int) -> int:
    cursor = offset + 1
    while cursor < len(text):
        if text[cursor] == "\\" and cursor + 1 < len(text):
            cursor += 2
        elif text[cursor] == '"':
            return cursor + 1
        else:
            cursor += 1
    raise AdmissionError("extension_config.cmake has an unterminated quoted argument")


def _sole_command_body(text: str, command: str) -> str:
    offset = 0
    while offset < len(text) and text[offset].isspace():
        offset += 1
    match = re.match(r"[A-Za-z_][A-Za-z0-9_]*", text[offset:])
    require(match is not None and match.group(0).lower() == command.lower(),
            f"extension_config.cmake must contain only one top-level {command} command")
    cursor = offset + len(match.group(0))
    while cursor < len(text) and text[cursor].isspace():
        cursor += 1
    require(cursor < len(text) and text[cursor] == "(",
            f"{command} must have an argument list")

    body_start = cursor + 1
    depth = 1
    cursor = body_start
    while cursor < len(text) and depth:
        if text[cursor] == '"':
            cursor = _quoted_end(text, cursor)
            continue
        bracket_end = _bracket_end(text, cursor) if text[cursor] == "[" else None
        if bracket_end is not None:
            cursor = bracket_end
            continue
        if text[cursor] == "(":
            depth += 1
        elif text[cursor] == ")":
            depth -= 1
            if depth == 0:
                body = text[body_start:cursor]
                cursor += 1
                break
        cursor += 1
    require(depth == 0, f"{command} has an unterminated argument list")
    require(text[cursor:].strip() == "",
            f"extension_config.cmake must contain only one top-level {command} command")
    return body


def _cmake_tokens(body: str) -> list[tuple[str, str]]:
    tokens: list[tuple[str, str]] = []
    offset = 0
    while offset < len(body):
        if body[offset].isspace():
            offset += 1
            continue
        if body[offset] == '"':
            end = _quoted_end(body, offset)
            tokens.append(("quoted", body[offset + 1:end - 1]))
            offset = end
            continue
        bracket_end = _bracket_end(body, offset) if body[offset] == "[" else None
        if bracket_end is not None:
            tokens.append(("quoted", body[offset:bracket_end]))
            offset = bracket_end
            continue
        end = offset
        while end < len(body) and not body[end].isspace() and body[end] != '"':
            end += 1
        require(end > offset, "extension_config.cmake contains an invalid argument")
        tokens.append(("bare", body[offset:end]))
        offset = end
    return tokens


def _extension_version(repository: pathlib.Path, commit: str) -> str:
    try:
        text = blob(repository, commit, "extension_config.cmake").decode("utf-8")
    except UnicodeDecodeError as error:
        raise AdmissionError("extension_config.cmake is not UTF-8") from error
    active = _without_cmake_comments(text)
    body = _sole_command_body(active, "duckdb_extension_load")
    tokens = _cmake_tokens(body)
    require(tokens and tokens[0] == ("bare", "duckdb_api"),
            "candidate load command must name duckdb_api")
    version_indices = [
        index
        for index, token in enumerate(tokens)
        if token == ("bare", "EXTENSION_VERSION")
    ]
    require(len(version_indices) == 1,
            "candidate duckdb_api extension block must declare one quoted version")
    version_index = version_indices[0]
    require(
        version_index + 1 < len(tokens)
        and tokens[version_index + 1][0] == "quoted",
        "candidate duckdb_api extension block must declare one quoted version",
    )
    return tokens[version_index + 1][1]


def candidate_record(
    repository: pathlib.Path,
    source_commit: str,
    pins: dict[str, Any],
    pins_digest: str,
    descriptor: dict[str, Any],
    descriptor_digest: str,
    dependency_audit: dict[str, Any],
    dependency_digest: str,
    dependency_anchor_digest: str,
) -> dict[str, Any]:
    validate_pins(pins)
    validate_expectation(descriptor, pins)
    commit, tree = identity(repository, source_commit)
    _validate_source_layout(repository, commit, pins)
    project = _mapping(pins["project"], "project pins")
    require(_extension_version(repository, commit) == project["version"],
            "candidate extension version does not equal 0.2.0")
    license_pin = _mapping(project["license"], "project license pin")
    require(sha256_bytes(blob(repository, commit, license_pin["path"])) == license_pin["sha256"],
            "candidate project license does not match the MIT pin")

    require(dependency_audit.get("schema") == AUDIT_SCHEMA,
            "dependency audit schema is unsupported")
    validate_audit_record(dependency_audit, pins)
    require(dependency_audit.get("result") == "input_admitted",
            "dependency inputs were not admitted")
    require(dependency_audit.get("pins_sha256") == pins_digest,
            "dependency audit was produced from different pins")
    audit_source = _mapping(dependency_audit.get("project_source"), "dependency audit source")
    require(audit_source.get("commit") == commit and audit_source.get("tree") == tree,
            "dependency audit names a different candidate source")
    audit_license = _mapping(audit_source.get("license"), "dependency audit project license")
    require(audit_license == license_pin, "dependency audit project license identity drifted")

    duckdb = _mapping(pins["duckdb"], "DuckDB pins")
    community = _mapping(pins["community_extensions"], "Community pins")
    template = _mapping(pins["extension_template"], "template pins")
    ci_tools = _mapping(pins["extension_ci_tools"], "ci-tools pins")
    record = {
        "community": {
            "commit": community["commit"],
            "extension_ci_tools": {
                "commit": ci_tools["commit"],
                "ref": ci_tools["ref"],
                "repository": ci_tools["repository"],
            },
            "extension_template": {
                "commit": template["commit"],
                "repository": template["repository"],
            },
            "repository": community["repository"],
        },
        "dependency_audit": {
            "anchor_sha256": dependency_anchor_digest,
            "schema": dependency_audit["schema"],
            "sha256": dependency_digest,
        },
        "descriptor_expectation": {
            "sha256": descriptor_digest,
            "status": "pending_non_authoritative",
        },
        "duckdb": {"commit": duckdb["commit"], "version": duckdb["version"]},
        "pins_sha256": pins_digest,
        "project": {
            "extension": project["extension"],
            "license": "MIT",
            "repository": project["repository"],
            "tag": project["tag"],
            "tag_state": "pending",
            "version": project["version"],
        },
        "schema": CANDIDATE_SCHEMA,
        "source": {"commit": commit, "tree": tree},
        "status": "admitted_candidate",
    }
    validate_candidate_record(record, pins, pins_digest, descriptor_digest)
    return record


def parser() -> argparse.ArgumentParser:
    value = argparse.ArgumentParser()
    value.add_argument("--repository", type=pathlib.Path, required=True)
    value.add_argument("--source-commit", required=True)
    value.add_argument("--pins", type=pathlib.Path, required=True)
    value.add_argument("--descriptor-expectation", type=pathlib.Path, required=True)
    value.add_argument("--dependency-audit", type=pathlib.Path, required=True)
    value.add_argument("--dependency-anchor", type=pathlib.Path, required=True)
    value.add_argument("--output-root", type=pathlib.Path, required=True)
    return value


def main() -> int:
    arguments = parser().parse_args()
    try:
        regular_directory(arguments.repository, "candidate repository")
        pins, pins_digest = load_canonical_object(arguments.pins, "Community pins")
        descriptor, descriptor_digest = load_canonical_object(
            arguments.descriptor_expectation, "descriptor expectation"
        )
        audit, audit_digest, audit_anchor_digest = verify_anchored_object(
            arguments.dependency_audit,
            arguments.dependency_anchor,
            "dependency-audit.json",
            "dependency audit",
        )
        candidate = candidate_record(
            arguments.repository,
            arguments.source_commit,
            pins,
            pins_digest,
            descriptor,
            descriptor_digest,
            audit,
            audit_digest,
            audit_anchor_digest,
        )
        output_root = prepare_output_root(arguments.output_root)
        write_anchored_json(output_root, "candidate.json", candidate)
        print("candidate.json")
        return 0
    except AdmissionError as error:
        print(f"candidate admission failed: {error}", file=sys.stderr)
        return 1
    except (OSError, ValueError):
        print("candidate admission failed: filesystem operation failed", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())

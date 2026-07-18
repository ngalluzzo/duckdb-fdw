#!/usr/bin/env python3
"""Admit candidate-cycle dependencies from exact local upstream Git objects."""

from __future__ import annotations

import argparse
import pathlib
import re
import sys
from typing import Any


HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

from candidate_pins import validate_pins  # noqa: E402
from git_snapshot import blob, has_blob, identity  # noqa: E402
from record_format import (  # noqa: E402
    AdmissionError,
    load_canonical_object,
    prepare_output_root,
    regular_directory,
    require,
    sha256_bytes,
    write_anchored_json,
)


EXPECTATION_SCHEMA = "duckdb_api/dependency-expectations/v1"
AUDIT_SCHEMA = "duckdb_api/dependency-audit/v1"
AUDIT_SCOPE = "candidate source and Community build inputs only"
EXPECTED_ROLES = {
    "duckdb": "compiled_host_api",
    "community_extensions": "upstream_distribution_service",
    "extension_template": "build_template",
    "extension_ci_tools": "upstream_build_tool",
}
EXPECTED_LOCAL_NAMES = {
    "duckdb": "duckdb",
    "community_extensions": "community-extensions",
    "extension_template": "extension-template",
    "extension_ci_tools": "extension-ci-tools",
}
LICENSE_CANDIDATE_PATHS = [
    "COPYING",
    "COPYING.md",
    "COPYING.txt",
    "LICENSE",
    "LICENSE.md",
    "LICENSE.txt",
]
DEFERRED_RELEASE_EVIDENCE = [
    "per-platform compiler runtime and operating-system library closure",
    "per-row Community artifact inventory",
    "notices required by dependencies discovered only in Community outputs",
]
EXTERNAL_LIMITATIONS = [
    "community_extensions has no root license file at the pinned commit and is admitted only as upstream_distribution_service; no project redistribution is authorized",
    "extension_ci_tools has no root license file at the pinned commit and is admitted only as upstream_build_tool; no project redistribution is authorized",
]
SAFE_COMPONENT = re.compile(r"[A-Za-z0-9][A-Za-z0-9._-]*")


def _mapping(value: object, label: str) -> dict[str, Any]:
    require(isinstance(value, dict), f"{label} must be an object")
    return value


def _safe_local_name(value: object, dependency: str) -> str:
    require(
        isinstance(value, str)
        and value not in {".", ".."}
        and SAFE_COMPONENT.fullmatch(value) is not None
        and pathlib.PurePath(value).name == value
        and pathlib.PurePosixPath(value).name == value,
        f"dependency {dependency} local name must be one safe path component",
    )
    return value


def _validate_expectations(value: dict[str, Any]) -> list[dict[str, Any]]:
    require(
        set(value)
        == {"deferred_release_evidence", "dependencies", "schema", "scope"},
        "dependency expectation fields drifted",
    )
    require(value.get("schema") == EXPECTATION_SCHEMA,
            "dependency expectation schema is unsupported")
    require(value.get("scope") == AUDIT_SCOPE,
            "dependency expectation scope drifted")
    require(value.get("deferred_release_evidence") == DEFERRED_RELEASE_EVIDENCE,
            "deferred dependency evidence drifted")
    dependencies = value.get("dependencies")
    require(isinstance(dependencies, list),
            "dependency expectations must contain entries")
    require(all(isinstance(entry, dict) for entry in dependencies),
            "dependency expectation entries must be objects")
    names = [entry.get("name") for entry in dependencies]
    require(names == list(EXPECTED_ROLES),
            "dependency expectation order or set drifted")
    local_names: list[str] = []
    for entry in dependencies:
        require(
            set(entry)
            == {
                "license",
                "local_name",
                "name",
                "notice_required",
                "pin",
                "redistributed_by_project",
                "role",
            },
            "dependency expectation entry fields drifted",
        )
        name = entry["name"]
        local_name = _safe_local_name(entry.get("local_name"), name)
        local_names.append(local_name)
        require(local_name == EXPECTED_LOCAL_NAMES[name],
                f"dependency {name} local repository name drifted")
        require(entry.get("pin") == name, f"dependency {name} must use its same-named pin")
        require(entry.get("role") == EXPECTED_ROLES[name], f"dependency {name} role drifted")
        require(entry.get("redistributed_by_project") is False,
                f"dependency {name} must not authorize project redistribution")
        require(entry.get("notice_required") is (name == "duckdb"),
                f"dependency {name} notice policy drifted")
        license_record = _mapping(entry.get("license"), f"{name} license expectation")
        if name in {"duckdb", "extension_template"}:
            require(
                set(license_record) == {"path", "sha256", "spdx", "status"},
                f"{name} license expectation fields drifted",
            )
            require(license_record.get("status") == "present",
                    f"{name} license expectation status drifted")
        else:
            require(
                license_record
                == {
                    "candidate_paths": LICENSE_CANDIDATE_PATHS,
                    "status": "absent_at_pinned_commit",
                },
                f"{name} absent-license expectation drifted",
            )
    require(len(local_names) == len(set(local_names)),
            "dependency local repository names are duplicated")
    return dependencies


def validate_audit_record(value: dict[str, Any], pins: dict[str, Any]) -> None:
    """Reject a self-anchored record that did not preserve provider semantics."""
    validate_pins(pins)
    require(
        set(value)
        == {
            "schema",
            "result",
            "scope",
            "pins_sha256",
            "dependency_expectations_sha256",
            "project_source",
            "dependencies",
            "notices",
            "limitations",
            "deferred_release_evidence",
        },
        "dependency audit fields drifted",
    )
    require(value.get("schema") == AUDIT_SCHEMA, "dependency audit schema is unsupported")
    require(value.get("result") == "input_admitted", "dependency inputs were not admitted")
    require(value.get("scope") == AUDIT_SCOPE, "dependency audit scope drifted")
    require(
        value.get("dependency_expectations_sha256")
        == pins["dependency_expectations_sha256"],
        "dependency audit expectation digest drifted",
    )
    require(
        isinstance(value.get("pins_sha256"), str)
        and re.fullmatch(r"[0-9a-f]{64}", value["pins_sha256"]) is not None,
        "dependency audit pin digest is invalid",
    )
    dependencies = value.get("dependencies")
    require(isinstance(dependencies, list) and len(dependencies) == 4,
            "dependency audit set is incomplete")
    by_name = {
        entry.get("name"): entry for entry in dependencies if isinstance(entry, dict)
    }
    require(set(by_name) == set(EXPECTED_ROLES), "dependency audit names drifted")
    require([entry.get("name") for entry in dependencies] == list(EXPECTED_ROLES),
            "dependency audit order drifted")
    for name, role in EXPECTED_ROLES.items():
        entry = by_name[name]
        require(
            set(entry)
            == {
                "commit",
                "license",
                "name",
                "notice_required",
                "redistributed_by_project",
                "repository",
                "role",
                "tree",
            },
            f"dependency audit fields drifted for {name}",
        )
        pin = _mapping(pins[name], f"{name} pins")
        require(entry.get("role") == role, f"dependency audit role drifted for {name}")
        require(entry.get("repository") == pin["repository"],
                f"dependency audit repository drifted for {name}")
        require(entry.get("commit") == pin["commit"] and entry.get("tree") == pin["tree"],
                f"dependency audit identity drifted for {name}")
        require(entry.get("redistributed_by_project") is False,
                f"dependency audit authorizes redistribution for {name}")
        require(entry.get("notice_required") is (name == "duckdb"),
                f"dependency audit notice policy drifted for {name}")
        license_record = _mapping(entry.get("license"), f"{name} audited license")
        if name in {"duckdb", "extension_template"}:
            pin_license = _mapping(pin.get("license"), f"{name} pinned license")
            require(
                license_record
                == {
                    "path": pin_license["path"],
                    "sha256": pin_license["sha256"],
                    "spdx": pin_license["spdx"],
                    "status": "present",
                },
                f"dependency audit license drifted for {name}",
            )
        else:
            require(
                license_record
                == {
                    "candidate_paths": LICENSE_CANDIDATE_PATHS,
                    "status": "absent_at_pinned_commit",
                },
                f"dependency audit external-only license status drifted for {name}",
            )
    notices = value.get("notices")
    duckdb_license = _mapping(pins["duckdb"]["license"], "DuckDB license pin")
    require(
        notices
        == [
            {
                "dependency": "duckdb",
                "path": duckdb_license["path"],
                "sha256": duckdb_license["sha256"],
            }
        ],
        "dependency audit notices are incomplete",
    )
    require(value.get("limitations") == EXTERNAL_LIMITATIONS,
            "external-only license limitations drifted")
    require(value.get("deferred_release_evidence") == DEFERRED_RELEASE_EVIDENCE,
            "deferred release dependency evidence drifted")
    project_source = _mapping(value.get("project_source"), "dependency audit source")
    require(set(project_source) == {"commit", "license", "tree"},
            "dependency audit source fields drifted")
    project_license = _mapping(pins["project"]["license"], "project license pin")
    require(project_source.get("license") == project_license,
            "dependency audit project license identity drifted")


def audit(
    pins: dict[str, Any],
    pins_digest: str,
    expectations: dict[str, Any],
    expectations_digest: str,
    repository: pathlib.Path,
    source_commit: str,
    upstreams_root: pathlib.Path,
) -> dict[str, Any]:
    validate_pins(pins)
    require(
        expectations_digest == pins["dependency_expectations_sha256"],
        "dependency expectations do not match the pinned digest",
    )
    entries = _validate_expectations(expectations)
    project_commit, project_tree = identity(repository, source_commit)
    project = _mapping(pins["project"], "project pins")
    project_license = _mapping(project["license"], "project license pins")
    project_license_bytes = blob(repository, project_commit, project_license["path"])
    require(sha256_bytes(project_license_bytes) == project_license["sha256"],
            "candidate project license does not match the MIT pin")

    observed: list[dict[str, Any]] = []
    notices: list[dict[str, str]] = []
    limitations: list[str] = []
    for entry in entries:
        name = entry["name"]
        pin_name = entry["pin"]
        local_name = _safe_local_name(entry["local_name"], name)
        pin = _mapping(pins[pin_name], f"{name} pins")
        upstream = upstreams_root / local_name
        require(upstream.parent == upstreams_root,
                f"dependency {name} must be a direct child of the upstreams root")
        regular_directory(upstream, f"{name} upstream repository")
        commit, tree = identity(upstream, pin["commit"])
        require(tree == pin["tree"], f"{name} source tree does not match its pin")

        license_expectation = _mapping(entry["license"], f"{name} license expectation")
        status = license_expectation["status"]
        if status == "present":
            path = license_expectation["path"]
            license_bytes = blob(upstream, commit, path)
            digest = sha256_bytes(license_bytes)
            require(digest == license_expectation["sha256"],
                    f"{name} license digest does not match its primary-source pin")
            require(license_expectation["spdx"] == "MIT",
                    f"{name} license disposition is not MIT")
            pin_license = _mapping(pin.get("license"), f"{name} pinned license")
            require(
                {
                    "path": license_expectation["path"],
                    "sha256": license_expectation["sha256"],
                    "spdx": license_expectation["spdx"],
                }
                == pin_license,
                f"{name} dependency expectation does not match the immutable pin",
            )
            license_record: dict[str, Any] = {
                "path": path,
                "sha256": digest,
                "spdx": "MIT",
                "status": "present",
            }
            if entry["notice_required"]:
                notices.append({"dependency": name, "path": path, "sha256": digest})
        else:
            candidates = license_expectation["candidate_paths"]
            present = [path for path in candidates if has_blob(upstream, commit, path)]
            require(not present, f"{name} now has license evidence and requires review")
            license_record = {
                "candidate_paths": candidates,
                "status": "absent_at_pinned_commit",
            }
            limitations.append(
                f"{name} has no root license file at the pinned commit and is admitted only as {entry['role']}; no project redistribution is authorized"
            )

        observed.append(
            {
                "commit": commit,
                "license": license_record,
                "name": name,
                "notice_required": entry["notice_required"],
                "redistributed_by_project": False,
                "repository": pin["repository"],
                "role": entry["role"],
                "tree": tree,
            }
        )

    report = {
        "dependencies": observed,
        "deferred_release_evidence": DEFERRED_RELEASE_EVIDENCE,
        "dependency_expectations_sha256": expectations_digest,
        "limitations": sorted(limitations),
        "notices": notices,
        "pins_sha256": pins_digest,
        "project_source": {
            "commit": project_commit,
            "license": project_license,
            "tree": project_tree,
        },
        "result": "input_admitted",
        "schema": AUDIT_SCHEMA,
        "scope": AUDIT_SCOPE,
    }
    validate_audit_record(report, pins)
    return report


def parser() -> argparse.ArgumentParser:
    value = argparse.ArgumentParser()
    value.add_argument("--pins", type=pathlib.Path, required=True)
    value.add_argument("--expectations", type=pathlib.Path, required=True)
    value.add_argument("--repository", type=pathlib.Path, required=True)
    value.add_argument("--source-commit", required=True)
    value.add_argument("--upstreams-root", type=pathlib.Path, required=True)
    value.add_argument("--output-root", type=pathlib.Path, required=True)
    return value


def main() -> int:
    arguments = parser().parse_args()
    try:
        pins, pins_digest = load_canonical_object(arguments.pins, "Community pins")
        expectations, expectations_digest = load_canonical_object(
            arguments.expectations, "dependency expectations"
        )
        regular_directory(arguments.repository, "candidate repository")
        regular_directory(arguments.upstreams_root, "upstreams root")
        report = audit(
            pins,
            pins_digest,
            expectations,
            expectations_digest,
            arguments.repository,
            arguments.source_commit,
            arguments.upstreams_root,
        )
        output_root = prepare_output_root(arguments.output_root)
        write_anchored_json(output_root, "dependency-audit.json", report)
        print("dependency-audit.json")
        return 0
    except AdmissionError as error:
        print(f"dependency admission failed: {error}", file=sys.stderr)
        return 1
    except (OSError, ValueError):
        print("dependency admission failed: filesystem operation failed", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())

"""Deterministic Git repositories and process helpers for provider tests."""

from __future__ import annotations

import hashlib
import json
import os
import pathlib
import subprocess
import sys
import tempfile
import unittest


HERE = pathlib.Path(__file__).resolve().parent
REPOSITORY = HERE.parents[2]
COMMUNITY_SCRIPTS = REPOSITORY / "scripts/community"


def canonical_write(path: pathlib.Path, value: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def sha256(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def git(repository: pathlib.Path, *arguments: str) -> str:
    return subprocess.check_output(
        ["git", "-C", str(repository), *arguments],
        text=True,
        env={
            "GIT_CONFIG_GLOBAL": os.devnull,
            "GIT_CONFIG_NOSYSTEM": "1",
            "LC_ALL": "C",
            "PATH": os.environ.get("PATH", ""),
        },
    ).strip()


def commit(repository: pathlib.Path, files: dict[str, bytes], message: str) -> tuple[str, str]:
    repository.mkdir(parents=True, exist_ok=True)
    if not (repository / ".git").exists():
        subprocess.run(["git", "init", "-q", str(repository)], check=True)
        git(repository, "config", "user.name", "Provider Test")
        git(repository, "config", "user.email", "provider@example.invalid")
    for relative, content in files.items():
        path = repository / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(content)
    git(repository, "add", "-A")
    environment = os.environ.copy()
    environment.update(
        {
            "GIT_AUTHOR_DATE": "2026-01-01T00:00:00Z",
            "GIT_COMMITTER_DATE": "2026-01-01T00:00:00Z",
        }
    )
    subprocess.run(
        ["git", "-C", str(repository), "commit", "-q", "-m", message],
        check=True,
        env=environment,
    )
    return git(repository, "rev-parse", "HEAD"), git(repository, "rev-parse", "HEAD^{tree}")


class ProviderFixture(unittest.TestCase):
    maxDiff = None

    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = pathlib.Path(self.temporary.name)
        self.project = self.root / "project"
        self.upstreams = self.root / "upstreams"
        self.project_license = b"synthetic project MIT license\n"
        self.duckdb_license = b"synthetic DuckDB MIT license\n"
        self.template_license = b"synthetic template MIT license\n"
        self.project_commit, self.project_tree = commit(
            self.project,
            {
                "LICENSE": self.project_license,
                "extension_config.cmake": (
                    b"duckdb_extension_load(duckdb_api\n"
                    b"    SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}\n"
                    b"    EXTENSION_VERSION \"0.2.0\"\n)\n"
                ),
            },
            "project candidate",
        )
        self.upstream_identities: dict[str, tuple[str, str]] = {}
        upstream_files = {
            "duckdb": {"LICENSE": self.duckdb_license, "IDENTITY": b"duckdb\n"},
            "community-extensions": {"IDENTITY": b"community\n"},
            "extension-template": {
                "LICENSE": self.template_license,
                "IDENTITY": b"template\n",
            },
            "extension-ci-tools": {"IDENTITY": b"ci-tools\n"},
        }
        for name, files in upstream_files.items():
            self.upstream_identities[name] = commit(
                self.upstreams / name, files, f"{name} fixture"
            )
        self.pins_path = self.root / "pins.json"
        self.expectations_path = self.root / "dependencies.json"
        self.descriptor_path = self.root / "descriptor.json"
        self.dependencies = self.make_dependencies()
        self.pins = self.make_pins()
        self.descriptor = self.make_descriptor()
        self.write_inputs()

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def make_pins(self) -> dict[str, object]:
        duckdb = self.upstream_identities["duckdb"]
        community = self.upstream_identities["community-extensions"]
        template = self.upstream_identities["extension-template"]
        ci_tools = self.upstream_identities["extension-ci-tools"]
        return {
            "community_extensions": {
                "commit": community[0],
                "ref": "main",
                "ref_type": "branch",
                "repository": "https://github.com/duckdb/community-extensions",
                "tree": community[1],
            },
            "dependency_expectations_sha256": sha256(
                (json.dumps(self.dependencies, indent=2, sort_keys=True) + "\n").encode("utf-8")
            ),
            "duckdb": {
                "commit": duckdb[0],
                "license": {
                    "path": "LICENSE",
                    "sha256": sha256(self.duckdb_license),
                    "spdx": "MIT",
                },
                "ref": "v1.5.4",
                "ref_type": "tag",
                "repository": "https://github.com/duckdb/duckdb",
                "tree": duckdb[1],
                "version": "1.5.4",
            },
            "extension_ci_tools": {
                "commit": ci_tools[0],
                "ref": "v1.5-variegata",
                "ref_type": "branch",
                "repository": "https://github.com/duckdb/extension-ci-tools",
                "tree": ci_tools[1],
            },
            "extension_template": {
                "commit": template[0],
                "license": {
                    "path": "LICENSE",
                    "sha256": sha256(self.template_license),
                    "spdx": "MIT",
                },
                "ref": template[0],
                "ref_type": "commit",
                "repository": "https://github.com/duckdb/extension-template",
                "tree": template[1],
            },
            "project": {
                "extension": "duckdb_api",
                "license": {
                    "path": "LICENSE",
                    "sha256": sha256(self.project_license),
                    "spdx": "MIT",
                },
                "repository": "https://github.com/ngalluzzo/duckdb-fdw",
                "tag": "v0.2.0",
                "version": "0.2.0",
            },
            "schema": "duckdb_api/community-enablement-pins/v1",
        }

    def make_dependencies(self) -> dict[str, object]:
        absent = {
            "candidate_paths": [
                "COPYING",
                "COPYING.md",
                "COPYING.txt",
                "LICENSE",
                "LICENSE.md",
                "LICENSE.txt",
            ],
            "status": "absent_at_pinned_commit",
        }
        return {
            "dependencies": [
                {
                    "license": {
                        "path": "LICENSE",
                        "sha256": sha256(self.duckdb_license),
                        "spdx": "MIT",
                        "status": "present",
                    },
                    "local_name": "duckdb",
                    "name": "duckdb",
                    "notice_required": True,
                    "pin": "duckdb",
                    "redistributed_by_project": False,
                    "role": "compiled_host_api",
                },
                {
                    "license": absent,
                    "local_name": "community-extensions",
                    "name": "community_extensions",
                    "notice_required": False,
                    "pin": "community_extensions",
                    "redistributed_by_project": False,
                    "role": "upstream_distribution_service",
                },
                {
                    "license": {
                        "path": "LICENSE",
                        "sha256": sha256(self.template_license),
                        "spdx": "MIT",
                        "status": "present",
                    },
                    "local_name": "extension-template",
                    "name": "extension_template",
                    "notice_required": False,
                    "pin": "extension_template",
                    "redistributed_by_project": False,
                    "role": "build_template",
                },
                {
                    "license": absent,
                    "local_name": "extension-ci-tools",
                    "name": "extension_ci_tools",
                    "notice_required": False,
                    "pin": "extension_ci_tools",
                    "redistributed_by_project": False,
                    "role": "upstream_build_tool",
                },
            ],
            "deferred_release_evidence": [
                "per-platform compiler runtime and operating-system library closure",
                "per-row Community artifact inventory",
                "notices required by dependencies discovered only in Community outputs",
            ],
            "schema": "duckdb_api/dependency-expectations/v1",
            "scope": "candidate source and Community build inputs only",
        }

    def make_descriptor(self) -> dict[str, object]:
        return {
            "authority": "none",
            "expected": {
                "build_language": "C++",
                "extension": "duckdb_api",
                "license": "MIT",
                "maintainers": [],
                "maintainers_status": "pending",
                "repository": "https://github.com/ngalluzzo/duckdb-fdw",
                "source_commit": None,
                "source_ref": None,
                "version": "0.2.0",
            },
            "publication_status": "not_submitted",
            "schema": "duckdb_api/community-descriptor-expectation/v1",
            "status": "pending_non_authoritative",
            "support_claims": [],
        }

    def write_inputs(self) -> None:
        canonical_write(self.expectations_path, self.dependencies)
        self.pins["dependency_expectations_sha256"] = sha256(
            self.expectations_path.read_bytes()
        )
        canonical_write(self.pins_path, self.pins)
        canonical_write(self.descriptor_path, self.descriptor)

    def write_bound_expectations(self) -> None:
        canonical_write(self.expectations_path, self.dependencies)
        self.pins["dependency_expectations_sha256"] = sha256(
            self.expectations_path.read_bytes()
        )
        canonical_write(self.pins_path, self.pins)

    def run_script(self, name: str, *arguments: object) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [
                sys.executable,
                "-I",
                "-B",
                str(COMMUNITY_SCRIPTS / name),
                *map(str, arguments),
            ],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )

    def run_audit(self, output: pathlib.Path | None = None) -> pathlib.Path:
        target = output or self.root / "audit"
        result = self.run_script(
            "audit_dependencies.py",
            "--pins",
            self.pins_path,
            "--expectations",
            self.expectations_path,
            "--repository",
            self.project,
            "--source-commit",
            self.project_commit,
            "--upstreams-root",
            self.upstreams,
            "--output-root",
            target,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "dependency-audit.json\n")
        return target

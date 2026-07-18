#!/usr/bin/env python3
"""Admission coverage for the tracked 0.2.0 provider records."""

from __future__ import annotations

import pathlib
import subprocess
import sys
import unittest


HERE = pathlib.Path(__file__).resolve().parent
REPOSITORY = HERE.parents[2]
COMMUNITY = HERE.parent
ENABLEMENT = REPOSITORY / "release/0.2.0/enablement"
sys.path.insert(0, str(COMMUNITY))

from audit_dependencies import _validate_expectations  # noqa: E402
from candidate_pins import validate_pins  # noqa: E402
from record_format import load_canonical_object  # noqa: E402
from verify_descriptor import validate_expectation  # noqa: E402


class TrackedRecordTest(unittest.TestCase):
    def test_community_source_layout_has_exact_toolchain_gitlinks(self) -> None:
        pins, _pins_digest = load_canonical_object(
            (ENABLEMENT / "pins.json").resolve(), "tracked Community pins"
        )
        validate_pins(pins)
        expected = {
            "duckdb": pins["duckdb"]["commit"],
            "extension-ci-tools": pins["extension_ci_tools"]["commit"],
        }
        observed: dict[str, tuple[str, str]] = {}
        entries = subprocess.check_output(
            [
                "git",
                "-C",
                str(REPOSITORY),
                "ls-files",
                "--stage",
                "--",
                *expected,
            ],
            text=True,
        ).splitlines()
        for entry in entries:
            metadata, path = entry.split("\t", 1)
            mode, object_id, stage = metadata.split()
            self.assertEqual(stage, "0")
            observed[path] = (mode, object_id)
        self.assertEqual(
            observed,
            {path: ("160000", commit) for path, commit in expected.items()},
        )

        modules = REPOSITORY / ".gitmodules"
        configuration = {
            key: subprocess.check_output(
                ["git", "config", "-f", str(modules), "--get", key], text=True
            ).strip()
            for key in (
                "submodule.duckdb.path",
                "submodule.duckdb.url",
                "submodule.duckdb.branch",
                "submodule.extension-ci-tools.path",
                "submodule.extension-ci-tools.url",
                "submodule.extension-ci-tools.branch",
            )
        }
        self.assertEqual(
            configuration,
            {
                "submodule.duckdb.path": "duckdb",
                "submodule.duckdb.url": "https://github.com/duckdb/duckdb",
                "submodule.duckdb.branch": "main",
                "submodule.extension-ci-tools.path": "extension-ci-tools",
                "submodule.extension-ci-tools.url": "https://github.com/duckdb/extension-ci-tools",
                "submodule.extension-ci-tools.branch": "v1.5-variegata",
            },
        )

    def test_tracked_records_are_canonical_and_mutually_admitted(self) -> None:
        pins, _pins_digest = load_canonical_object(
            (ENABLEMENT / "pins.json").resolve(), "tracked Community pins"
        )
        dependencies, dependencies_digest = load_canonical_object(
            (ENABLEMENT / "dependencies.json").resolve(),
            "tracked dependency expectations",
        )
        descriptor, _descriptor_digest = load_canonical_object(
            (ENABLEMENT / "descriptor.json").resolve(),
            "tracked descriptor expectation",
        )
        validate_pins(pins)
        self.assertEqual(
            dependencies_digest, pins["dependency_expectations_sha256"]
        )
        _validate_expectations(dependencies)
        validate_expectation(descriptor, pins)

        result = subprocess.run(
            [
                sys.executable,
                "-I",
                "-B",
                str(COMMUNITY / "verify_descriptor.py"),
                "--pins",
                str(ENABLEMENT / "pins.json"),
                "--descriptor-expectation",
                str(ENABLEMENT / "descriptor.json"),
            ],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "descriptor.json\n")


if __name__ == "__main__":
    unittest.main()

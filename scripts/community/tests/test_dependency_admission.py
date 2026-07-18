#!/usr/bin/env python3
"""Exact upstream dependency and license admission tests."""

from __future__ import annotations

import json
import pathlib
import sys


sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from test_support import ProviderFixture, canonical_write, commit  # noqa: E402


class DependencyAdmissionTest(ProviderFixture):
    def test_exact_upstream_inputs_emit_anchored_scoped_audit(self) -> None:
        output = self.run_audit()
        report_bytes = (output / "dependency-audit.json").read_bytes()
        report = json.loads(report_bytes)
        self.assertEqual(report["result"], "input_admitted")
        self.assertEqual(report["project_source"]["commit"], self.project_commit)
        self.assertEqual(len(report["dependencies"]), 4)
        self.assertEqual(
            [notice["dependency"] for notice in report["notices"]], ["duckdb"]
        )
        anchor = (output / "dependency-audit.sha256").read_text(encoding="utf-8")
        self.assertRegex(
            anchor, r"\A[0-9a-f]{64}  dependency-audit\.json\n\Z"
        )

    def test_new_license_in_external_only_input_requires_review(self) -> None:
        ci_root = self.upstreams / "extension-ci-tools"
        new_commit, new_tree = commit(
            ci_root,
            {"LICENSE": b"newly supplied license\n"},
            "add license evidence",
        )
        ci_pin = self.pins["extension_ci_tools"]
        assert isinstance(ci_pin, dict)
        ci_pin["commit"] = new_commit
        ci_pin["tree"] = new_tree
        canonical_write(self.pins_path, self.pins)
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
            self.root / "changed-license-audit",
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("now has license evidence and requires review", result.stderr)

    def test_wrong_primary_license_digest_is_rejected(self) -> None:
        duckdb = self.dependencies["dependencies"][0]
        assert isinstance(duckdb, dict)
        license_record = duckdb["license"]
        assert isinstance(license_record, dict)
        license_record["sha256"] = "0" * 64
        self.write_bound_expectations()
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
            self.root / "wrong-license-audit",
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("license digest", result.stderr)

    def test_dependency_expectation_digest_drift_is_rejected(self) -> None:
        duckdb = self.dependencies["dependencies"][0]
        assert isinstance(duckdb, dict)
        duckdb["notice_required"] = False
        canonical_write(self.expectations_path, self.dependencies)
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
            self.root / "digest-drift-audit",
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("pinned digest", result.stderr)

    def test_local_repository_name_must_be_one_safe_component(self) -> None:
        dependencies = self.dependencies["dependencies"]
        assert isinstance(dependencies, list)
        duckdb = dependencies[0]
        assert isinstance(duckdb, dict)
        for index, local_name in enumerate(("../duckdb", "/tmp/duckdb")):
            with self.subTest(local_name=local_name):
                duckdb["local_name"] = local_name
                self.write_bound_expectations()
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
                    self.root / f"unsafe-local-name-{index}",
                )
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("one safe path component", result.stderr)
                self.assertNotIn(str(self.root), result.stderr)


if __name__ == "__main__":
    import unittest

    unittest.main()

#!/usr/bin/env python3
"""Candidate identity and provider-record binding tests."""

from __future__ import annotations

import json
import hashlib
import pathlib
import sys


sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from test_support import (  # noqa: E402
    ProviderFixture,
    commit,
    commit_index,
    git,
    stage_gitlink,
)


class CandidateAdmissionTest(ProviderFixture):
    def run_candidate(
        self,
        audit_root: pathlib.Path,
        output: pathlib.Path,
        source_commit: str | None = None,
    ):
        return self.run_script(
            "verify_candidate.py",
            "--repository",
            self.project,
            "--source-commit",
            source_commit or self.project_commit,
            "--pins",
            self.pins_path,
            "--descriptor-expectation",
            self.descriptor_path,
            "--dependency-audit",
            audit_root / "dependency-audit.json",
            "--dependency-anchor",
            audit_root / "dependency-audit.sha256",
            "--output-root",
            output,
        )

    def commit_gitmodules(self, payload: bytes, message: str) -> None:
        self.project_commit, self.project_tree = commit(
            self.project, {".gitmodules": payload}, message
        )

    def assert_layout_rejected(self, label: str, diagnostic: str) -> None:
        audit = self.run_audit(self.root / f"{label}-audit")
        result = self.run_candidate(audit, self.root / f"{label}-candidate")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(diagnostic, result.stderr)

    def test_exact_candidate_is_anchored_without_support_or_artifact_claim(self) -> None:
        audit = self.run_audit()
        output = self.root / "candidate"
        result = self.run_candidate(audit, output)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "candidate.json\n")
        candidate = json.loads((output / "candidate.json").read_text(encoding="utf-8"))
        self.assertEqual(candidate["schema"], "duckdb_api/community-candidate/v1")
        self.assertEqual(candidate["source"]["commit"], self.project_commit)
        self.assertEqual(candidate["project"]["tag_state"], "pending")
        self.assertEqual(
            candidate["descriptor_expectation"]["status"],
            "pending_non_authoritative",
        )
        self.assertNotIn("support", candidate)
        self.assertNotIn("artifact", candidate)
        self.assertNotIn("custody", candidate)
        self.assertRegex(
            (output / "candidate.sha256").read_text(encoding="utf-8"),
            r"\A[0-9a-f]{64}  candidate\.json\n\Z",
        )

    def test_missing_gitlink_is_rejected(self) -> None:
        git(self.project, "rm", "-q", "--cached", "duckdb")
        self.project_commit, self.project_tree = commit_index(
            self.project, "missing duckdb gitlink"
        )
        self.assert_layout_rejected(
            "missing-gitlink", "gitlink layout does not match Community pins"
        )

    def test_extra_gitlink_is_rejected(self) -> None:
        stage_gitlink(
            self.project,
            "unexpected-tools",
            self.upstream_identities["extension-ci-tools"][0],
        )
        self.project_commit, self.project_tree = commit_index(
            self.project, "extra gitlink"
        )
        self.assert_layout_rejected(
            "extra-gitlink", "gitlink layout does not match Community pins"
        )

    def test_wrong_gitlink_object_is_rejected(self) -> None:
        stage_gitlink(
            self.project, "duckdb", self.upstream_identities["extension-ci-tools"][0]
        )
        self.project_commit, self.project_tree = commit_index(
            self.project, "wrong duckdb gitlink object"
        )
        self.assert_layout_rejected(
            "wrong-gitlink-object", "gitlink layout does not match Community pins"
        )

    def test_wrong_gitlink_path_is_rejected(self) -> None:
        git(self.project, "rm", "-q", "--cached", "extension-ci-tools")
        stage_gitlink(
            self.project,
            "renamed-ci-tools",
            self.upstream_identities["extension-ci-tools"][0],
        )
        self.project_commit, self.project_tree = commit_index(
            self.project, "wrong ci-tools gitlink path"
        )
        self.assert_layout_rejected(
            "wrong-gitlink-path", "gitlink layout does not match Community pins"
        )

    def test_wrong_gitlink_mode_and_type_are_rejected(self) -> None:
        git(self.project, "rm", "-q", "--cached", "duckdb")
        (self.project / "duckdb").write_bytes(b"not a gitlink\n")
        git(self.project, "add", "--", "duckdb")
        self.project_commit, self.project_tree = commit_index(
            self.project, "regular blob in place of duckdb gitlink"
        )
        self.assert_layout_rejected(
            "wrong-gitlink-mode", "gitlink layout does not match Community pins"
        )

    def test_wrong_gitmodule_urls_paths_and_branches_are_rejected(self) -> None:
        mutations = (
            (
                "duckdb-url",
                b"https://github.com/duckdb/duckdb",
                b"https://example.invalid/duckdb",
            ),
            (
                "ci-url",
                b"https://github.com/duckdb/extension-ci-tools",
                b"https://example.invalid/ci-tools",
            ),
            ("duckdb-path", b"path = duckdb", b"path = vendor/duckdb"),
            (
                "ci-path",
                b"path = extension-ci-tools",
                b"path = vendor/ci-tools",
            ),
            ("duckdb-branch", b"branch = main", b"branch = stable"),
            ("ci-branch", b"branch = v1.5-variegata", b"branch = main"),
        )
        for label, original, replacement in mutations:
            with self.subTest(label=label):
                payload = self.gitmodules.replace(original, replacement)
                self.commit_gitmodules(payload, f"wrong {label}")
                self.assert_layout_rejected(
                    label, ".gitmodules metadata does not match Community pins"
                )

    def test_missing_unexpected_and_duplicate_gitmodule_metadata_is_rejected(self) -> None:
        duckdb_section, ci_section = self.gitmodules.split(
            b'[submodule "extension-ci-tools"]\n', 1
        )
        ci_section = b'[submodule "extension-ci-tools"]\n' + ci_section
        mutations = (
            ("missing-key", self.gitmodules.replace(b"\tbranch = main\n", b"")),
            ("missing-section", duckdb_section),
            ("unexpected-key", self.gitmodules + b"\tupdate = checkout\n"),
            (
                "unexpected-section",
                self.gitmodules + b'[submodule "unexpected"]\n\tpath = unexpected\n',
            ),
            (
                "include-section",
                self.gitmodules + b"[include]\n\tpath = /tmp/ambient-config\n",
            ),
            (
                "duplicate-key",
                self.gitmodules.replace(
                    b"\tpath = duckdb\n", b"\tpath = duckdb\n\tpath = duckdb\n"
                ),
            ),
            ("duplicate-section", self.gitmodules + ci_section),
        )
        for label, payload in mutations:
            with self.subTest(label=label):
                self.commit_gitmodules(payload, label)
                self.assert_layout_rejected(
                    label, ".gitmodules metadata does not match Community pins"
                )

    def test_non_regular_gitmodules_blob_is_rejected(self) -> None:
        modules = self.project / ".gitmodules"
        modules.unlink()
        modules.symlink_to("LICENSE")
        git(self.project, "add", "--", ".gitmodules")
        self.project_commit, self.project_tree = commit_index(
            self.project, "symlink gitmodules"
        )
        self.assert_layout_rejected(
            "symlink-gitmodules", ".gitmodules must be one regular blob"
        )

    def test_missing_gitmodules_blob_is_rejected(self) -> None:
        git(self.project, "rm", "-q", "--", ".gitmodules")
        self.project_commit, self.project_tree = commit_index(
            self.project, "missing gitmodules"
        )
        self.assert_layout_rejected(
            "missing-gitmodules", ".gitmodules must be one regular blob"
        )

    def test_exact_commit_ignores_head_index_and_worktree_substitutions(self) -> None:
        exact_commit = self.project_commit
        audit = self.run_audit(self.root / "exact-snapshot-audit", exact_commit)
        self.commit_gitmodules(
            self.gitmodules.replace(
                b"https://github.com/duckdb/duckdb",
                b"https://example.invalid/head",
            ),
            "bad head metadata",
        )
        stage_gitlink(
            self.project, "duckdb", self.upstream_identities["extension-ci-tools"][0]
        )
        (self.project / ".gitmodules").write_bytes(
            self.gitmodules.replace(b"branch = main", b"branch = staged")
        )
        git(self.project, "add", "--", ".gitmodules")
        (self.project / ".gitmodules").write_bytes(
            self.gitmodules.replace(b"branch = main", b"branch = worktree")
        )
        result = self.run_candidate(
            audit, self.root / "exact-snapshot-candidate", exact_commit
        )
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_bad_committed_gitlink_cannot_be_masked_by_good_index(self) -> None:
        git(self.project, "rm", "-q", "--cached", "duckdb")
        self.project_commit, self.project_tree = commit_index(
            self.project, "bad committed gitlink layout"
        )
        bad_commit = self.project_commit
        audit = self.run_audit(self.root / "bad-gitlink-audit", bad_commit)
        stage_gitlink(
            self.project, "duckdb", self.upstream_identities["duckdb"][0]
        )
        result = self.run_candidate(
            audit, self.root / "bad-gitlink-candidate", bad_commit
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("gitlink layout does not match Community pins", result.stderr)

    def test_bad_committed_gitmodules_cannot_be_masked_by_good_index_and_worktree(
        self,
    ) -> None:
        self.commit_gitmodules(
            self.gitmodules.replace(b"branch = main", b"branch = wrong"),
            "bad committed gitmodules metadata",
        )
        bad_commit = self.project_commit
        audit = self.run_audit(self.root / "bad-gitmodules-audit", bad_commit)
        (self.project / ".gitmodules").write_bytes(self.gitmodules)
        git(self.project, "add", "--", ".gitmodules")
        result = self.run_candidate(
            audit, self.root / "bad-gitmodules-candidate", bad_commit
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(
            ".gitmodules metadata does not match Community pins", result.stderr
        )

    def test_candidate_version_mismatch_is_rejected(self) -> None:
        self.project_commit, self.project_tree = commit(
            self.project,
            {
                "extension_config.cmake": (
                    b"duckdb_extension_load(duckdb_api\n"
                    b"    EXTENSION_VERSION \"0.1.0\"\n)\n"
                )
            },
            "wrong project version",
        )
        audit = self.run_audit(self.root / "wrong-version-audit")
        result = self.run_candidate(audit, self.root / "wrong-version-candidate")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("does not equal 0.2.0", result.stderr)

    def test_commented_decoys_do_not_override_the_sole_active_block(self) -> None:
        self.project_commit, self.project_tree = commit(
            self.project,
            {
                "extension_config.cmake": (
                    b"# duckdb_extension_load(duckdb_api EXTENSION_VERSION \"9.9.9\")\n"
                    b"#[[\n"
                    b"duckdb_extension_load(duckdb_api EXTENSION_VERSION \"8.8.8\")\n"
                    b"]]\n"
                    b"duckdb_extension_load(duckdb_api\n"
                    b"    SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}\n"
                    b"    EXTENSION_VERSION \"0.2.0\"\n)\n"
                )
            },
            "commented version decoys",
        )
        audit = self.run_audit(self.root / "comment-decoy-audit")
        result = self.run_candidate(audit, self.root / "comment-decoy-candidate")
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_false_branch_or_uncalled_function_decoy_is_rejected(self) -> None:
        configurations = (
            (
                "false-branch",
                b"if(FALSE)\n"
                b"  duckdb_extension_load(duckdb_api EXTENSION_VERSION \"0.2.0\")\n"
                b"endif()\n",
            ),
            (
                "uncalled-function",
                b"function(decoy)\n"
                b"  duckdb_extension_load(duckdb_api EXTENSION_VERSION \"0.2.0\")\n"
                b"endfunction()\n",
            ),
        )
        for index, (label, configuration) in enumerate(configurations):
            with self.subTest(label=label):
                self.project_commit, self.project_tree = commit(
                    self.project,
                    {"extension_config.cmake": configuration},
                    label,
                )
                audit = self.run_audit(self.root / f"{label}-audit-{index}")
                result = self.run_candidate(
                    audit, self.root / f"{label}-candidate-{index}"
                )
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("only one top-level", result.stderr)

    def test_replace_object_cannot_change_the_admitted_source(self) -> None:
        replacement_commit, _replacement_tree = commit(
            self.project,
            {
                "extension_config.cmake": (
                    b"duckdb_extension_load(duckdb_api\n"
                    b"    EXTENSION_VERSION \"0.1.0\"\n)\n"
                )
            },
            "replacement decoy",
        )
        git(self.project, "replace", self.project_commit, replacement_commit)
        audit = self.run_audit(self.root / "replace-object-audit")
        result = self.run_candidate(audit, self.root / "replace-object-candidate")
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_changed_dependency_record_is_rejected_before_candidate_output(self) -> None:
        audit = self.run_audit()
        report = audit / "dependency-audit.json"
        report.chmod(0o644)
        report.write_bytes(report.read_bytes().replace(b"input_admitted", b"input_rejected"))
        result = self.run_candidate(audit, self.root / "tampered-candidate")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("anchor syntax or digest drifted", result.stderr)
        self.assertFalse((self.root / "tampered-candidate").exists())

    def test_self_anchored_incomplete_dependency_record_is_rejected(self) -> None:
        audit = self.run_audit()
        report_path = audit / "dependency-audit.json"
        anchor_path = audit / "dependency-audit.sha256"
        report = json.loads(report_path.read_text(encoding="utf-8"))
        report["dependencies"] = []
        payload = (json.dumps(report, indent=2, sort_keys=True) + "\n").encode("utf-8")
        report_path.chmod(0o644)
        anchor_path.chmod(0o644)
        report_path.write_bytes(payload)
        anchor_path.write_text(
            f"{hashlib.sha256(payload).hexdigest()}  dependency-audit.json\n",
            encoding="utf-8",
        )
        result = self.run_candidate(audit, self.root / "incomplete-candidate")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("dependency audit set is incomplete", result.stderr)

    def test_self_anchored_semantic_audit_drift_is_rejected(self) -> None:
        mutations = (
            (
                "limitations",
                lambda report: report["limitations"].__setitem__(
                    0, "x" * len(report["limitations"][0])
                ),
                "limitations drifted",
            ),
            (
                "deferred",
                lambda report: report["deferred_release_evidence"].__setitem__(
                    0, "x" * len(report["deferred_release_evidence"][0])
                ),
                "evidence drifted",
            ),
            (
                "expectation-digest",
                lambda report: report.__setitem__(
                    "dependency_expectations_sha256", "0" * 64
                ),
                "expectation digest drifted",
            ),
        )
        for label, mutation, diagnostic in mutations:
            with self.subTest(label=label):
                audit = self.run_audit(self.root / f"semantic-{label}-audit")
                report_path = audit / "dependency-audit.json"
                anchor_path = audit / "dependency-audit.sha256"
                report = json.loads(report_path.read_text(encoding="utf-8"))
                mutation(report)
                payload = (json.dumps(report, indent=2, sort_keys=True) + "\n").encode(
                    "utf-8"
                )
                report_path.chmod(0o644)
                anchor_path.chmod(0o644)
                report_path.write_bytes(payload)
                anchor_path.write_text(
                    f"{hashlib.sha256(payload).hexdigest()}  dependency-audit.json\n",
                    encoding="utf-8",
                )
                result = self.run_candidate(
                    audit, self.root / f"semantic-{label}-candidate"
                )
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(diagnostic, result.stderr)


if __name__ == "__main__":
    import unittest

    unittest.main()

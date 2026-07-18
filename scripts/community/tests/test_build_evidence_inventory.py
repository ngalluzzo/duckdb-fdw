#!/usr/bin/env python3
"""Complete Community job inventory and row-normalization tests."""

from __future__ import annotations

import copy
import json
import pathlib
import sys


sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from build_evidence_test_support import BuildEvidenceFixture  # noqa: E402
from test_support import canonical_write  # noqa: E402


class BuildEvidenceInventoryTest(BuildEvidenceFixture):
    def test_complete_inventory_preserves_nonpassing_jobs_and_raw_labels(self) -> None:
        output = self.root / "normalized"
        result = self.run_collector(output)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "community-builds.json\n")
        inventory = json.loads(
            (output / "community-builds.json").read_text(encoding="utf-8")
        )
        self.assertEqual(
            [row["conclusion"] for row in inventory["rows"]],
            ["success", "failure", "skipped"],
        )
        self.assertEqual(inventory["support_claims"], [])
        self.assertEqual(
            inventory["matrix_exclusions"],
            [{"architecture": "arm64", "os": "macos", "toolchain": "clang"}],
        )
        self.assertNotEqual(
            inventory["origin"]["head"]["repository"],
            inventory["origin"]["extension_source"]["repository"],
        )
        self.assertNotEqual(
            inventory["origin"]["head"]["sha"],
            inventory["origin"]["extension_source"]["commit"],
        )
        failure = json.loads(
            (output / "jobs/job-102/community-build.json").read_text(
                encoding="utf-8"
            )
        )
        self.assertEqual(failure["job"]["raw_matrix"]["os"], "windows")
        self.assertEqual(failure["job"]["runner_labels"], ["windows-2025", "x64"])
        self.assertEqual(failure["artifacts"], [])
        self.assertIn("sha256", failure["log"])
        for forbidden in ("supported", "signing", "deployment", "stock_host"):
            self.assertNotIn(forbidden, inventory)
            self.assertNotIn(forbidden, failure)

    def test_incomplete_duplicate_or_colliding_jobs_are_rejected(self) -> None:
        original = copy.deepcopy(self.exports["jobs"])
        mutations = (
            ("incomplete", lambda value: value.__setitem__("total_count", 4), "incomplete"),
            (
                "duplicate",
                lambda value: (
                    value["jobs"].append(copy.deepcopy(value["jobs"][0])),
                    value.__setitem__("total_count", 4),
                ),
                "duplicate job ids",
            ),
            (
                "collision",
                lambda value: (
                    value["jobs"][1].__setitem__("artifact_names", ["windows-amd64"]),
                    value["jobs"][1].__setitem__(
                        "raw_matrix", copy.deepcopy(value["jobs"][0]["raw_matrix"])
                    ),
                ),
                "colliding raw matrix labels",
            ),
        )
        for index, (label, mutate, message) in enumerate(mutations):
            with self.subTest(label=label):
                mutate(self.exports["jobs"])
                canonical_write(self.jobs_export, self.exports["jobs"])
                self.refresh_approval()
                result = self.run_collector(self.root / f"jobs-output-{index}")
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(message, result.stderr)
                self.reset_export("jobs", original)

    def test_missing_duplicate_or_unaccounted_matrix_rows_are_rejected(self) -> None:
        original = copy.deepcopy(self.exports["matrix"])
        mutations = (
            (
                "missing-exclusion",
                lambda value: value["entries"].pop(),
                "matrix export is incomplete",
            ),
            (
                "duplicate",
                lambda value: (
                    value["entries"].append(copy.deepcopy(value["entries"][0])),
                    value.__setitem__("total_count", 4),
                ),
                "duplicate combinations",
            ),
            (
                "unaccounted",
                lambda value: (
                    value["entries"].append(
                        {
                            "disposition": "job_expected",
                            "raw_matrix": {
                                "architecture": "arm64",
                                "os": "linux",
                                "toolchain": "gcc",
                            },
                        }
                    ),
                    value.__setitem__("total_count", 4),
                ),
                "unaccounted job rows",
            ),
        )
        for index, (label, mutate, message) in enumerate(mutations):
            with self.subTest(label=label):
                mutate(self.exports["matrix"])
                canonical_write(self.matrix_export, self.exports["matrix"])
                self.refresh_approval()
                result = self.run_collector(self.root / f"matrix-output-{index}")
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(message, result.stderr)
                self.reset_export("matrix", original)


if __name__ == "__main__":
    import unittest

    unittest.main()

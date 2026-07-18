#!/usr/bin/env python3
"""Community artifact, log, and filesystem-boundary tests."""

from __future__ import annotations

import pathlib
import sys
from unittest import mock


sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from build_evidence_test_support import BuildEvidenceFixture  # noqa: E402

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))
import build_evidence_downloads  # noqa: E402


class BuildEvidenceDownloadsTest(BuildEvidenceFixture):
    def test_missing_or_extra_artifacts_and_logs_are_rejected(self) -> None:
        cases = (
            (self.artifacts_root / "linux-amd64.zip", "missing-artifact"),
            (self.logs_root / "job-103.log", "missing-log"),
        )
        for index, (path, label) in enumerate(cases):
            with self.subTest(label=label):
                payload = path.read_bytes()
                path.unlink()
                result = self.run_collector(self.root / f"{label}-output-{index}")
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("missing or extra files", result.stderr)
                path.write_bytes(payload)
        for index, root in enumerate((self.artifacts_root, self.logs_root)):
            with self.subTest(extra_root=root.name):
                extra = root / "unexpected.bin"
                extra.write_bytes(b"unexpected")
                result = self.run_collector(self.root / f"extra-output-{index}")
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("missing or extra files", result.stderr)
                extra.unlink()

    def test_changed_bytes_and_symlink_downloads_are_rejected(self) -> None:
        artifact = self.artifacts_root / "linux-amd64.zip"
        original = artifact.read_bytes()
        artifact.write_bytes(original + b"changed")
        result = self.run_collector(self.root / "changed-artifact-output")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("artifact download bytes drifted", result.stderr)
        artifact.unlink()
        target = self.root / "artifact-target.zip"
        target.write_bytes(original)
        artifact.symlink_to(target)
        result = self.run_collector(self.root / "linked-artifact-output")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("must not be symlinks", result.stderr)

        artifact.unlink()
        artifact.write_bytes(original)
        log = self.logs_root / "job-102.log"
        log.write_bytes(log.read_bytes()[:5])
        result = self.run_collector(self.root / "truncated-log-output")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("job log download bytes drifted", result.stderr)

    def test_noncanonical_export_linked_root_and_reused_output_are_rejected(self) -> None:
        payload = self.jobs_export.read_bytes()
        self.jobs_export.write_bytes(payload + b" ")
        result = self.run_collector(self.root / "noncanonical-output")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("not canonical JSON", result.stderr)
        self.jobs_export.write_bytes(payload)

        linked_parent = self.root / "linked-downloads"
        linked_parent.mkdir()
        linked = linked_parent / "logs"
        linked.symlink_to(self.logs_root, target_is_directory=True)
        result = self.run_collector(self.root / "linked-root-output", logs_root=linked)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("must not be a symlink", result.stderr)

        output = self.root / "used-output"
        first = self.run_collector(output)
        self.assertEqual(first.returncode, 0, first.stderr)
        second = self.run_collector(output)
        self.assertNotEqual(second.returncode, 0)
        self.assertIn("output root must be new", second.stderr)

    def test_coordinated_root_swap_to_symlink_is_rejected(self) -> None:
        original_digest = build_evidence_downloads._digest_file_at
        swapped = False

        def swap_root_before_read(
            directory: int, filename: str, label: str, maximum_bytes: int
        ):
            nonlocal swapped
            if label == "artifact download" and not swapped:
                swapped = True
                original_root = self.root / "original-artifacts"
                replacement_root = self.root / "replacement-artifacts"
                self.artifacts_root.rename(original_root)
                replacement_root.mkdir()
                (replacement_root / filename).write_bytes(
                    (original_root / filename).read_bytes()
                )
                self.artifacts_root.symlink_to(
                    replacement_root, target_is_directory=True
                )
            return original_digest(directory, filename, label, maximum_bytes)

        with mock.patch.object(
            build_evidence_downloads,
            "_digest_file_at",
            side_effect=swap_root_before_read,
        ):
            result = self.run_collector(self.root / "root-swap-output")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(
            "artifact downloads root changed during collection", result.stderr
        )


if __name__ == "__main__":
    import unittest

    unittest.main()

"""Thin orchestration fixture for the offline build-evidence command."""

from __future__ import annotations

import contextlib
import copy
import io
import json
import pathlib
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

from build_evidence_authority_fixture import (
    digest,
    make_authority,
    write_descriptor,
    write_registry,
)
from build_evidence_download_fixture import write_downloads
from build_evidence_export_fixture import make_exports
from test_support import COMMUNITY_SCRIPTS, REPOSITORY, canonical_write


sys.path.insert(0, str(COMMUNITY_SCRIPTS))
import build_evidence_authority  # noqa: E402
import collect_build_evidence  # noqa: E402


class BuildEvidenceFixture(unittest.TestCase):
    """Wire responsibility-specific fixture builders through the real CLI."""

    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = pathlib.Path(self.temporary.name)
        self.pins_path = self.root / "pins.json"
        self.pins = json.loads(
            (REPOSITORY / "release/0.2.0/enablement/pins.json").read_text(
                encoding="utf-8"
            )
        )
        canonical_write(self.pins_path, self.pins)
        self.descriptor = self.root / "descriptor-admission.json"
        self.descriptor_anchor = self.root / "descriptor-admission.sha256"
        write_descriptor(
            self.descriptor, self.descriptor_anchor, digest(self.pins_path)
        )
        self.pull_request_export = self.root / "pull-request.json"
        self.run_export = self.root / "run.json"
        self.jobs_export = self.root / "jobs.json"
        self.matrix_export = self.root / "matrix.json"
        self.artifacts_export = self.root / "artifacts.json"
        (
            self.artifacts_root,
            self.logs_root,
            artifact_bytes,
            self.log_bytes,
        ) = write_downloads(self.root)
        self.exports = make_exports(artifact_bytes, self.log_bytes)
        self.write_exports()
        self.registry = self.root / "build-authorities.json"
        self.authority = make_authority(
            self.descriptor, self.pins_path, self.export_paths()
        )
        self.refresh_approval()

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def write_exports(self) -> None:
        for name, path in self.export_paths().items():
            canonical_write(path, self.exports[name])

    def export_paths(self) -> dict[str, pathlib.Path]:
        return {
            "artifacts": self.artifacts_export,
            "jobs": self.jobs_export,
            "matrix": self.matrix_export,
            "pull_request": self.pull_request_export,
            "run": self.run_export,
        }

    def refresh_approval(self) -> None:
        self.approved_registry_digest = write_registry(
            self.registry, self.authority, self.export_paths()
        )

    def reset_export(self, name: str, original: dict[str, object]) -> None:
        self.exports[name] = copy.deepcopy(original)
        canonical_write(self.export_paths()[name], self.exports[name])
        self.refresh_approval()

    def run_collector(
        self,
        output: pathlib.Path,
        *,
        artifacts_root: pathlib.Path | None = None,
        logs_root: pathlib.Path | None = None,
    ) -> subprocess.CompletedProcess[str]:
        arguments = [
            "collect_build_evidence.py",
            "--authority-registry",
            str(self.registry),
            "--pins",
            str(self.pins_path),
            "--descriptor-admission",
            str(self.descriptor),
            "--descriptor-anchor",
            str(self.descriptor_anchor),
            "--pull-request-export",
            str(self.pull_request_export),
            "--run-export",
            str(self.run_export),
            "--jobs-export",
            str(self.jobs_export),
            "--matrix-export",
            str(self.matrix_export),
            "--artifacts-export",
            str(self.artifacts_export),
            "--artifacts-root",
            str(artifacts_root or self.artifacts_root),
            "--logs-root",
            str(logs_root or self.logs_root),
            "--output-root",
            str(output),
        ]
        stdout = io.StringIO()
        stderr = io.StringIO()
        with (
            mock.patch.object(
                build_evidence_authority,
                "APPROVED_REGISTRY_SHA256",
                self.approved_registry_digest,
            ),
            mock.patch.object(sys, "argv", arguments),
            contextlib.redirect_stdout(stdout),
            contextlib.redirect_stderr(stderr),
        ):
            returncode = collect_build_evidence.main()
        return subprocess.CompletedProcess(
            arguments, returncode, stdout.getvalue(), stderr.getvalue()
        )

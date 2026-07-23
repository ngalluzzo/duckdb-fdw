"""Deterministic fixtures for Community descriptor admission tests."""

from __future__ import annotations

import contextlib
import hashlib
import io
import json
import pathlib
import subprocess
import sys
from unittest import mock

from test_support import COMMUNITY_SCRIPTS, ProviderFixture


sys.path.insert(0, str(COMMUNITY_SCRIPTS))
import descriptor_cycle  # noqa: E402
import verify_descriptor  # noqa: E402


PUBLISHED_COMMIT = "47dc6169ae820f70beb0c2722b8a8f5288cd1469"
PUBLISHED_TREE = "6356b5296276aff08f81a6ec3ef9da6d0a6b8f7a"


class DescriptorFixture(ProviderFixture):
    """Build one internally consistent descriptor handoff per test."""

    def setUp(self) -> None:
        super().setUp()
        self.audit = self.run_audit(self.root / "descriptor-audit")
        self.candidate = self.root / "candidate"
        result = self.run_script(
            "verify_candidate.py",
            "--repository",
            self.project,
            "--source-commit",
            self.project_commit,
            "--pins",
            self.pins_path,
            "--descriptor-expectation",
            self.descriptor_path,
            "--dependency-audit",
            self.audit / "dependency-audit.json",
            "--dependency-anchor",
            self.audit / "dependency-audit.sha256",
            "--output-root",
            self.candidate,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.reanchor_published_source()
        self.proposal = self.root / "description.yml"
        self.proposal.write_bytes(self.proposal_bytes())
        self.cycle = self.root / "descriptor-cycle.json"
        self.write_cycle()
        self.approved_cycle_digest = self.digest(self.cycle)

    def digest(self, path: pathlib.Path) -> str:
        return hashlib.sha256(path.read_bytes()).hexdigest()

    def write_cycle(
        self,
        source_commit: str = PUBLISHED_COMMIT,
        source_tree: str = PUBLISHED_TREE,
    ) -> None:
        cycle = {
            "candidate": {
                "anchor_sha256": self.digest(self.candidate / "candidate.sha256"),
                "sha256": self.digest(self.candidate / "candidate.json"),
            },
            "dependency_audit": {
                "anchor_sha256": self.digest(
                    self.audit / "dependency-audit.sha256"
                ),
                "sha256": self.digest(self.audit / "dependency-audit.json"),
            },
            "descriptor_expectation_sha256": self.digest(self.descriptor_path),
            "pins_sha256": self.digest(self.pins_path),
            "proposal_sha256": self.digest(self.proposal),
            "schema": "duckdb_api/community-descriptor-cycle/v1",
            "source": {"commit": source_commit, "tree": source_tree},
        }
        self.cycle.write_text(
            json.dumps(cycle, indent=2, sort_keys=True) + "\n", encoding="utf-8"
        )

    def proposal_bytes(self) -> bytes:
        return (
            "extension:\n"
            "  name: duckdb_api\n"
            "  description: Exposes a typed example relation through DuckDB's "
            "duckdb_api_scan table function.\n"
            "  version: 0.2.0\n"
            "  language: C++\n"
            "  build: cmake\n"
            "  license: MIT\n"
            "  maintainers:\n"
            "    - ngalluzzo\n"
            "repo:\n"
            "  github: ngalluzzo/duckdb-fdw\n"
            f"  ref: {PUBLISHED_COMMIT}\n"
        ).encode("utf-8")

    def reanchor_published_source(self) -> None:
        audit_record = self.audit / "dependency-audit.json"
        audit_anchor = self.audit / "dependency-audit.sha256"
        audit = json.loads(audit_record.read_text(encoding="utf-8"))
        audit["project_source"]["commit"] = PUBLISHED_COMMIT
        audit["project_source"]["tree"] = PUBLISHED_TREE
        audit_payload = (json.dumps(audit, indent=2, sort_keys=True) + "\n").encode()
        audit_record.chmod(0o644)
        audit_anchor.chmod(0o644)
        audit_record.write_bytes(audit_payload)
        audit_anchor.write_text(
            f"{hashlib.sha256(audit_payload).hexdigest()}  dependency-audit.json\n",
            encoding="ascii",
        )

        candidate_record = self.candidate / "candidate.json"
        candidate_anchor = self.candidate / "candidate.sha256"
        candidate = json.loads(candidate_record.read_text(encoding="utf-8"))
        candidate["source"] = {
            "commit": PUBLISHED_COMMIT,
            "tree": PUBLISHED_TREE,
        }
        candidate["dependency_audit"]["sha256"] = self.digest(audit_record)
        candidate["dependency_audit"]["anchor_sha256"] = self.digest(audit_anchor)
        candidate_payload = (
            json.dumps(candidate, indent=2, sort_keys=True) + "\n"
        ).encode()
        candidate_record.chmod(0o644)
        candidate_anchor.chmod(0o644)
        candidate_record.write_bytes(candidate_payload)
        candidate_anchor.write_text(
            f"{hashlib.sha256(candidate_payload).hexdigest()}  candidate.json\n",
            encoding="ascii",
        )

    def run_descriptor(
        self,
        output: pathlib.Path,
        proposal: pathlib.Path | None = None,
        candidate: pathlib.Path | None = None,
        candidate_anchor: pathlib.Path | None = None,
        dependency_audit: pathlib.Path | None = None,
        dependency_anchor: pathlib.Path | None = None,
    ) -> subprocess.CompletedProcess[str]:
        arguments = [
            "verify_descriptor.py",
            "--pins",
            str(self.pins_path),
            "--descriptor-expectation",
            str(self.descriptor_path),
            "--descriptor-cycle",
            str(self.cycle),
            "--proposal",
            str(proposal or self.proposal),
            "--candidate",
            str(candidate or self.candidate / "candidate.json"),
            "--candidate-anchor",
            str(candidate_anchor or self.candidate / "candidate.sha256"),
            "--dependency-audit",
            str(dependency_audit or self.audit / "dependency-audit.json"),
            "--dependency-anchor",
            str(dependency_anchor or self.audit / "dependency-audit.sha256"),
            "--output-root",
            str(output),
        ]
        stdout = io.StringIO()
        stderr = io.StringIO()
        with (
            mock.patch.object(
                descriptor_cycle,
                "APPROVED_CYCLE_SHA256",
                self.approved_cycle_digest,
            ),
            mock.patch.object(sys, "argv", arguments),
            contextlib.redirect_stdout(stdout),
            contextlib.redirect_stderr(stderr),
        ):
            returncode = verify_descriptor.main()
        return subprocess.CompletedProcess(
            arguments, returncode, stdout.getvalue(), stderr.getvalue()
        )

    def rewrite_candidate(self, mutation) -> None:
        record = self.candidate / "candidate.json"
        anchor = self.candidate / "candidate.sha256"
        document = json.loads(record.read_text(encoding="utf-8"))
        mutation(document)
        payload = (json.dumps(document, indent=2, sort_keys=True) + "\n").encode()
        record.chmod(0o644)
        anchor.chmod(0o644)
        record.write_bytes(payload)
        anchor.write_text(
            f"{hashlib.sha256(payload).hexdigest()}  candidate.json\n",
            encoding="ascii",
        )

from __future__ import annotations

import pathlib
import tempfile
import unittest

try:
    from .matrix import (
        BuildEvidence,
        MatrixError,
        QueryEvidence,
        RowIdentity,
        claimable_rows,
    )
    from .test_support import SHA_A, SHA_B, SHA_C, admitted_candidate, row
except ImportError:
    from matrix import (
        BuildEvidence,
        MatrixError,
        QueryEvidence,
        RowIdentity,
        claimable_rows,
    )
    from test_support import SHA_A, SHA_B, SHA_C, admitted_candidate, row


class MatrixLawTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.candidate = admitted_candidate(pathlib.Path(self.temporary.name))
        self.contract_sha256 = SHA_C

    def build(
        self,
        target: RowIdentity | None = None,
        *,
        status: str = "passed",
        channel: str = "community",
    ) -> BuildEvidence:
        return BuildEvidence(
            candidate_sha256=self.candidate.sha256,
            row=target or row(),
            status=status,
            channel=channel,
            artifact_sha256=SHA_A if status == "passed" else None,
            custody_sha256=SHA_B if status == "passed" else None,
        )

    def query(
        self,
        target: RowIdentity | None = None,
        *,
        status: str = "passed",
        artifact_sha256: str = SHA_A,
        channel: str = "community",
        default_signature_enforced: bool = True,
    ) -> QueryEvidence:
        return QueryEvidence(
            candidate_sha256=self.candidate.sha256,
            row=target or row(),
            status=status,
            channel=channel,
            artifact_sha256=artifact_sha256,
            default_signature_enforced=default_signature_enforced,
            extension_version="0.2.0",
            public_contract_sha256=self.contract_sha256,
        )

    def test_claims_exact_complete_passing_intersection(self) -> None:
        linux = row("linux_amd64")
        osx = row("osx_arm64")
        claimed = claimable_rows(
            self.candidate,
            [self.build(osx), self.build(linux)],
            [self.query(osx), self.query(linux, status="failed")],
            self.contract_sha256,
        )
        self.assertEqual(claimed, (osx,))

    def test_failed_and_excluded_build_rows_remain_unclaimed(self) -> None:
        self.assertEqual(
            claimable_rows(
                self.candidate,
                [
                    self.build(status="failed"),
                    self.build(row("windows_amd64"), status="excluded"),
                ],
                [],
                self.contract_sha256,
            ),
            (),
        )

    def test_passing_build_requires_query_evidence(self) -> None:
        with self.assertRaisesRegex(MatrixError, "lacks Query evidence"):
            claimable_rows(
                self.candidate, [self.build()], [], self.contract_sha256
            )

    def test_rejects_duplicate_or_orphan_rows(self) -> None:
        with self.assertRaisesRegex(MatrixError, "duplicate row"):
            claimable_rows(
                self.candidate,
                [self.build(), self.build()],
                [self.query()],
                self.contract_sha256,
            )
        with self.assertRaisesRegex(MatrixError, "no Community build"):
            claimable_rows(
                self.candidate, [], [self.query()], self.contract_sha256
            )

    def test_rejects_weakened_policy_or_noncommunity_channel(self) -> None:
        with self.assertRaisesRegex(MatrixError, "signature enforcement"):
            claimable_rows(
                self.candidate,
                [self.build()],
                [self.query(default_signature_enforced=False)],
                self.contract_sha256,
            )
        with self.assertRaisesRegex(MatrixError, "not from the Community"):
            claimable_rows(
                self.candidate,
                [self.build(channel="custom")],
                [],
                self.contract_sha256,
            )

    def test_rejects_artifact_contract_and_candidate_mismatch(self) -> None:
        with self.assertRaisesRegex(MatrixError, "different artifact bytes"):
            claimable_rows(
                self.candidate,
                [self.build()],
                [self.query(artifact_sha256=SHA_B)],
                self.contract_sha256,
            )
        wrong_candidate = self.query()
        wrong_candidate = QueryEvidence(
            candidate_sha256=SHA_B,
            row=wrong_candidate.row,
            status=wrong_candidate.status,
            channel=wrong_candidate.channel,
            artifact_sha256=wrong_candidate.artifact_sha256,
            default_signature_enforced=True,
            extension_version="0.2.0",
            public_contract_sha256=self.contract_sha256,
        )
        with self.assertRaisesRegex(MatrixError, "different source candidate"):
            claimable_rows(
                self.candidate,
                [self.build()],
                [wrong_candidate],
                self.contract_sha256,
            )

    def test_nonpassing_build_cannot_claim_artifact_or_query(self) -> None:
        failed = BuildEvidence(
            candidate_sha256=self.candidate.sha256,
            row=row(),
            status="failed",
            channel="community",
            artifact_sha256=SHA_A,
            custody_sha256=SHA_B,
        )
        with self.assertRaisesRegex(MatrixError, "claims artifact custody"):
            claimable_rows(
                self.candidate, [failed], [], self.contract_sha256
            )
        with self.assertRaisesRegex(MatrixError, "non-passing Community build"):
            claimable_rows(
                self.candidate,
                [self.build(status="failed")],
                [self.query()],
                self.contract_sha256,
            )


if __name__ == "__main__":
    unittest.main()

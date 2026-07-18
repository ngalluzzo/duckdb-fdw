from __future__ import annotations

import pathlib
import tempfile
import unittest

try:
    from .deployment_admission import DEPLOYMENT_SCHEMA, admit_deployment
    from .input_admission import AdmissionError
    from .test_support import (
        SHA_A,
        SHA_B,
        admitted_candidate,
        deployment_document,
        write_deployment,
    )
except ImportError:
    from deployment_admission import DEPLOYMENT_SCHEMA, admit_deployment
    from input_admission import AdmissionError
    from test_support import (
        SHA_A,
        SHA_B,
        admitted_candidate,
        deployment_document,
        write_deployment,
    )


class DeploymentAdmissionTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = pathlib.Path(self.temporary.name)
        candidate_root = self.root / "candidate"
        candidate_root.mkdir()
        self.candidate = admitted_candidate(candidate_root)

    def test_accepts_exact_anchored_native_deployment(self) -> None:
        record, anchor = write_deployment(self.root, self.candidate)
        admitted = admit_deployment(record, anchor, self.candidate)
        self.assertEqual(deployment_document(self.candidate)["schema"], DEPLOYMENT_SCHEMA)
        self.assertEqual(admitted.unsigned_artifact_sha256, SHA_B)
        self.assertEqual(admitted.deployed_artifact_sha256, SHA_A)
        self.assertNotEqual(
            admitted.deployment_record_sha256,
            admitted.deployment_anchor_sha256,
        )

    def test_rejects_tampering_unknown_fields_and_unsigned_substitution(self) -> None:
        record, anchor = write_deployment(self.root, self.candidate)
        record.write_bytes(record.read_bytes() + b" ")
        with self.assertRaisesRegex(AdmissionError, "does not match"):
            admit_deployment(record, anchor, self.candidate)

        document = deployment_document(self.candidate)
        document["unexpected"] = True
        record, anchor = write_deployment(self.root, self.candidate, document)
        with self.assertRaisesRegex(AdmissionError, "fields differ"):
            admit_deployment(record, anchor, self.candidate)

        document = deployment_document(self.candidate)
        deployed = document["deployment"]
        assert isinstance(deployed, dict)
        deployed["signed_artifact_sha256"] = SHA_B
        record, anchor = write_deployment(self.root, self.candidate, document)
        with self.assertRaisesRegex(AdmissionError, "identities are equal"):
            admit_deployment(record, anchor, self.candidate)

    def test_rejects_candidate_row_and_endpoint_drift(self) -> None:
        document = deployment_document(self.candidate)
        document["candidate_sha256"] = "f" * 64
        record, anchor = write_deployment(self.root, self.candidate, document)
        with self.assertRaisesRegex(AdmissionError, "candidate_sha256"):
            admit_deployment(record, anchor, self.candidate)

        document = deployment_document(self.candidate)
        row = document["row"]
        assert isinstance(row, dict)
        duckdb = row["duckdb"]
        assert isinstance(duckdb, dict)
        duckdb["version"] = "1.5.3"
        record, anchor = write_deployment(self.root, self.candidate, document)
        with self.assertRaisesRegex(AdmissionError, "duckdb.version"):
            admit_deployment(record, anchor, self.candidate)

        document = deployment_document(self.candidate)
        row = document["row"]
        assert isinstance(row, dict)
        row["platform"] = "wasm_mvp"
        record, anchor = write_deployment(self.root, self.candidate, document)
        with self.assertRaisesRegex(AdmissionError, "native Community rows only"):
            admit_deployment(record, anchor, self.candidate)

        document = deployment_document(self.candidate)
        row = document["row"]
        assert isinstance(row, dict)
        row["platform"] = "not-a-community-row"
        record, anchor = write_deployment(self.root, self.candidate, document)
        with self.assertRaisesRegex(AdmissionError, "Community platform"):
            admit_deployment(record, anchor, self.candidate)

        document = deployment_document(self.candidate)
        community = document["community"]
        assert isinstance(community, dict)
        community["endpoint"] = "https://example.invalid/duckdb_api.duckdb_extension.gz"
        record, anchor = write_deployment(self.root, self.candidate, document)
        with self.assertRaisesRegex(AdmissionError, "endpoint"):
            admit_deployment(record, anchor, self.candidate)


if __name__ == "__main__":
    unittest.main()

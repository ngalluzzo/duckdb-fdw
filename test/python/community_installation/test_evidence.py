from __future__ import annotations

from dataclasses import replace
import hashlib
import json
import pathlib
import tempfile
import unittest
from unittest import mock

try:
    from .evidence import (
        EvidenceError,
        QUERY_EVIDENCE_SCHEMA,
        build_failed_evidence,
        build_passed_evidence,
        canonical_bytes,
        write_evidence,
    )
    from .evidence_admission import query_evidence
    from .scenarios import run_scenarios
    from .matrix import claimable_rows
    from .test_support import (
        FakeInitializationProbe,
        FakeRunner,
        SHA_A,
        SHA_C,
        SHA_D,
        admitted_candidate,
        deployment_evidence,
        extension,
        incompatible_observation,
        public_contract,
        row,
        supported_observations,
    )
except ImportError:
    from evidence import (
        EvidenceError,
        QUERY_EVIDENCE_SCHEMA,
        build_failed_evidence,
        build_passed_evidence,
        canonical_bytes,
        write_evidence,
    )
    from evidence_admission import query_evidence
    from scenarios import run_scenarios
    from matrix import claimable_rows
    from test_support import (
        FakeInitializationProbe,
        FakeRunner,
        SHA_A,
        SHA_C,
        SHA_D,
        admitted_candidate,
        deployment_evidence,
        extension,
        incompatible_observation,
        public_contract,
        row,
        supported_observations,
    )


def contract_sha256() -> str:
    return hashlib.sha256(
        json.dumps(public_contract(), sort_keys=True, separators=(",", ":")).encode()
    ).hexdigest()


class EvidenceTests(unittest.TestCase):
    def test_passed_evidence_is_bound_normalized_and_matrix_ready(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            candidate_root = root / "candidate"
            candidate_root.mkdir()
            candidate = admitted_candidate(candidate_root)
            deployment = deployment_evidence(candidate)
            install_path = root / "state/extensions/duckdb_api.duckdb_extension"
            observations = supported_observations()
            for index, loaded in ((1, False), (2, False), (3, True)):
                observations[index] = replace(
                    observations[index],
                    extension=extension(
                        loaded=loaded, install_path=str(install_path)
                    ),
                )
            scenarios = run_scenarios(
                supported_runner=FakeRunner(observations),
                incompatible_runner=FakeRunner([incompatible_observation()]),
                supported_row=row(),
                incompatible_row=row(),
                artifact_sha256=SHA_A,
                public_contract=public_contract(),
                required_incompatible_facts=("v1.5.4", "v1.5.3"),
                initialization_probe=FakeInitializationProbe(),
            )
            result = build_passed_evidence(
                candidate=candidate,
                deployment=deployment,
                scenarios=scenarios,
                incompatible_artifact_size=6,
                incompatible_artifact_sha256=SHA_A,
                supported_launcher_sha256=SHA_C,
                incompatible_launcher_sha256=SHA_D,
                supported_host_inventory_sha256=SHA_D,
                incompatible_host_inventory_sha256=SHA_C,
                public_contract_sha256=contract_sha256(),
                replacements=((root, "<trial-root>"),),
            )
            self.assertEqual(result["schema"], QUERY_EVIDENCE_SCHEMA)
            self.assertEqual(
                set(result),
                {
                    "artifact_sha256",
                    "candidate",
                    "channel",
                    "community",
                    "default_signature_enforced",
                    "extension_version",
                    "incompatible",
                    "incompatible_artifact_sha256",
                    "incompatible_artifact_size",
                    "initialization_probe_sha256",
                    "launcher_sha256",
                    "stock_host_inventory_sha256",
                    "public_contract_sha256",
                    "row",
                    "schema",
                    "status",
                    "supported",
                },
            )
            self.assertEqual(
                set(result["community"]),
                {
                    "deployment_anchor_sha256",
                    "deployment_record_sha256",
                    "repository",
                    "source_commit",
                },
            )
            self.assertTrue(result["default_signature_enforced"])
            self.assertNotIn(str(root.resolve()), json.dumps(result))
            self.assertIn("<trial-root>", json.dumps(result))
            normalized = query_evidence(result, candidate)
            self.assertEqual(normalized.row, row())
            self.assertEqual(normalized.artifact_sha256, SHA_A)
            self.assertEqual(
                result["launcher_sha256"],
                {"supported": SHA_C, "incompatible": SHA_D},
            )
            self.assertEqual(
                result["stock_host_inventory_sha256"],
                {"supported": SHA_D, "incompatible": SHA_C},
            )
            with self.assertRaisesRegex(EvidenceError, "fields differ"):
                query_evidence(
                    {"schema": QUERY_EVIDENCE_SCHEMA, "status": "passed"},
                    candidate,
                )
            for section, field in (
                ("candidate", "source_commit"),
                ("candidate", "source_tree"),
                ("community", "source_commit"),
            ):
                malformed = json.loads(json.dumps(result))
                malformed[section][field] = "f" * 40
                with self.subTest(section=section, field=field):
                    with self.assertRaisesRegex(EvidenceError, "different"):
                        query_evidence(malformed, candidate)

    def test_writer_is_canonical_and_refuses_existing_target(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            output = pathlib.Path(directory) / "query-result.json"
            value = {"schema": QUERY_EVIDENCE_SCHEMA, "status": "failed"}
            write_evidence(output, value)
            original = canonical_bytes(value)
            self.assertEqual(output.read_bytes(), original)
            with self.assertRaisesRegex(EvidenceError, "new caller-owned"):
                write_evidence(output, value)
            self.assertEqual(output.read_bytes(), original)

    def test_writer_does_not_remove_a_racing_callers_file(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            output = pathlib.Path(directory) / "query-result.json"
            competing = b"competing writer\n"

            def collide(path: object, flags: int, mode: int) -> int:
                del flags, mode
                pathlib.Path(path).write_bytes(competing)
                raise FileExistsError(str(path))

            with mock.patch(
                f"{write_evidence.__module__}.os.open", side_effect=collide
            ):
                with self.assertRaisesRegex(EvidenceError, "new caller-owned"):
                    write_evidence(
                        output,
                        {"schema": QUERY_EVIDENCE_SCHEMA, "status": "failed"},
                    )
            self.assertEqual(output.read_bytes(), competing)

    def test_failed_evidence_is_nonclaimable_and_path_normalized(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            candidate_root = root / "candidate"
            candidate_root.mkdir()
            candidate = admitted_candidate(candidate_root)
            result = build_failed_evidence(
                candidate=candidate,
                deployment=deployment_evidence(candidate),
                public_contract_sha256=SHA_C,
                category="stock_host_lifecycle",
                diagnostic=f"failed in {root}/secret",
                incompatible_artifact_size=6,
                incompatible_artifact_sha256=SHA_A,
                initialization_probe_sha256=SHA_D,
                supported_launcher_sha256=SHA_C,
                incompatible_launcher_sha256=SHA_D,
                supported_host_inventory_sha256=SHA_D,
                incompatible_host_inventory_sha256=SHA_C,
                replacements=((root, "<root>"),),
            )
            self.assertEqual(result["status"], "failed")
            self.assertNotIn(str(root), json.dumps(result))
            self.assertEqual(
                set(result),
                {
                    "artifact_sha256",
                    "candidate",
                    "channel",
                    "community",
                    "default_signature_enforced",
                    "extension_version",
                    "failure",
                    "incompatible_artifact_sha256",
                    "incompatible_artifact_size",
                    "initialization_probe_sha256",
                    "launcher_sha256",
                    "stock_host_inventory_sha256",
                    "public_contract_sha256",
                    "row",
                    "schema",
                    "status",
                },
            )
            normalized = query_evidence(result, candidate)
            self.assertEqual(normalized.status, "failed")
            self.assertEqual(
                claimable_rows(
                    candidate,
                    [deployment_evidence(candidate)],
                    [normalized],
                    SHA_C,
                ),
                (),
            )

            malformed = json.loads(json.dumps(result))
            malformed["community"]["deployment_record_sha256"] = "not-a-digest"
            with self.assertRaisesRegex(EvidenceError, "SHA-256"):
                query_evidence(malformed, candidate)


if __name__ == "__main__":
    unittest.main()

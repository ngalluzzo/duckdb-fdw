from __future__ import annotations

from dataclasses import replace
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
        query_evidence,
        write_evidence,
    )
    from .scenarios import run_scenarios
    from .test_support import (
        FakeInitializationProbe,
        FakeRunner,
        SHA_A,
        SHA_C,
        SHA_D,
        admitted_candidate,
        build_evidence,
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
        query_evidence,
        write_evidence,
    )
    from scenarios import run_scenarios
    from test_support import (
        FakeInitializationProbe,
        FakeRunner,
        SHA_A,
        SHA_C,
        SHA_D,
        admitted_candidate,
        build_evidence,
        extension,
        incompatible_observation,
        public_contract,
        row,
        supported_observations,
    )


class EvidenceTests(unittest.TestCase):
    def test_passed_evidence_is_bound_normalized_and_matrix_ready(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            candidate_root = root / "candidate"
            candidate_root.mkdir()
            candidate = admitted_candidate(candidate_root)
            build = build_evidence(candidate)
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
                build=build,
                scenarios=scenarios,
                incompatible_artifact_size=6,
                incompatible_artifact_sha256=SHA_A,
                supported_launcher_sha256=SHA_C,
                incompatible_launcher_sha256=SHA_D,
                supported_host_inventory_sha256=SHA_D,
                incompatible_host_inventory_sha256=SHA_C,
                public_contract_sha256=SHA_C,
                replacements=((root, "<trial-root>"),),
            )
            self.assertEqual(result["schema"], QUERY_EVIDENCE_SCHEMA)
            self.assertTrue(result["default_signature_enforced"])
            self.assertNotIn(str(root.resolve()), json.dumps(result))
            self.assertIn("<trial-root>", json.dumps(result))
            normalized = query_evidence(result)
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
                build=build_evidence(candidate),
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
            with self.assertRaisesRegex(EvidenceError, "only passed"):
                query_evidence(result)


if __name__ == "__main__":
    unittest.main()

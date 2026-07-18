from __future__ import annotations

from dataclasses import replace
import json
import pathlib
import tempfile
import unittest

try:
    from .oracle import (
        OracleError,
        PUBLIC_CONTRACT_SHA256,
        evaluate_row,
        load_public_contract,
    )
    from .test_support import (
        FakeInitializationProbe,
        FakeRunner,
        SHA_C,
        SHA_D,
        admitted_candidate,
        deployment_evidence,
        incompatible_observation,
        public_contract,
        row,
        supported_observations,
    )
except ImportError:
    from oracle import (
        OracleError,
        PUBLIC_CONTRACT_SHA256,
        evaluate_row,
        load_public_contract,
    )
    from test_support import (
        FakeInitializationProbe,
        FakeRunner,
        SHA_C,
        SHA_D,
        admitted_candidate,
        deployment_evidence,
        incompatible_observation,
        public_contract,
        row,
        supported_observations,
    )


def accepted_contract_path() -> pathlib.Path:
    return pathlib.Path(__file__).resolve().parents[3] / "release/0.2.0/public_contract.json"


class OracleCompositionTests(unittest.TestCase):
    def test_exact_public_contract_is_content_bound(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            candidate = admitted_candidate(root)
            contract, digest = load_public_contract(accepted_contract_path(), candidate)
            self.assertEqual(contract, public_contract())
            self.assertEqual(digest, PUBLIC_CONTRACT_SHA256)
            pins = json.loads(
                (accepted_contract_path().parent / "pins.json").read_text(
                    encoding="utf-8"
                )
            )
            self.assertEqual(
                digest, pins["identities"]["public_contract_sha256"]
            )

            changed = root / "changed-contract.json"
            value = public_contract()
            value["rows"] = []
            changed.write_text(
                json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8"
            )
            with self.assertRaisesRegex(OracleError, "identity drifted"):
                load_public_contract(changed, candidate)

    def test_evaluates_complete_row_before_claiming_it(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            candidate_root = root / "candidate"
            candidate_root.mkdir()
            candidate = admitted_candidate(candidate_root)
            output = root / "query-result.json"
            supported = FakeRunner(supported_observations())
            incompatible = FakeRunner([incompatible_observation()])
            result = evaluate_row(
                candidate=candidate,
                deployment=deployment_evidence(candidate),
                supported_runner=supported,
                incompatible_runner=incompatible,
                incompatible_row=row(),
                incompatible_artifact_size=6,
                incompatible_artifact_sha256="d" * 64,
                initialization_probe=FakeInitializationProbe(),
                supported_launcher_sha256=SHA_C,
                incompatible_launcher_sha256=SHA_D,
                supported_host_inventory_sha256=SHA_D,
                incompatible_host_inventory_sha256=SHA_C,
                public_contract=public_contract(),
                public_contract_sha256=PUBLIC_CONTRACT_SHA256,
                required_incompatible_facts=("v1.5.4", "v1.5.3"),
                forbidden_diagnostic_values=(),
                replacements=((root, "<root>"),),
                output_path=output,
            )
            self.assertEqual(result["status"], "passed")
            self.assertEqual(json.loads(output.read_text(encoding="utf-8")), result)
            self.assertEqual(len(supported.calls), 4)
            self.assertEqual(incompatible.calls, [("incompatible", "incompatible")])

    def test_lifecycle_failure_emits_nonclaimable_result(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            candidate_root = root / "candidate"
            candidate_root.mkdir()
            candidate = admitted_candidate(candidate_root)
            observations = supported_observations()
            observations[1] = replace(
                observations[1], ok=False, extension=None, diagnostic="not published"
            )
            result = evaluate_row(
                candidate=candidate,
                deployment=deployment_evidence(candidate),
                supported_runner=FakeRunner(observations),
                incompatible_runner=FakeRunner([incompatible_observation()]),
                incompatible_row=row(),
                incompatible_artifact_size=6,
                incompatible_artifact_sha256="d" * 64,
                initialization_probe=FakeInitializationProbe(),
                supported_launcher_sha256=SHA_C,
                incompatible_launcher_sha256=SHA_D,
                supported_host_inventory_sha256=SHA_D,
                incompatible_host_inventory_sha256=SHA_C,
                public_contract=public_contract(),
                public_contract_sha256=PUBLIC_CONTRACT_SHA256,
                required_incompatible_facts=("v1.5.4", "v1.5.3"),
                forbidden_diagnostic_values=(),
                replacements=((root, "<root>"),),
                output_path=root / "failed.json",
            )
            self.assertEqual(result["status"], "failed")
            self.assertEqual(result["failure"]["category"], "stock_host_lifecycle")

    def test_observed_native_initialization_can_only_emit_failure(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            candidate_root = root / "candidate"
            candidate_root.mkdir()
            candidate = admitted_candidate(candidate_root)
            probe = FakeInitializationProbe(initialized=True)
            result = evaluate_row(
                candidate=candidate,
                deployment=deployment_evidence(candidate),
                supported_runner=FakeRunner(supported_observations()),
                incompatible_runner=FakeRunner([incompatible_observation()]),
                incompatible_row=row(),
                incompatible_artifact_size=6,
                incompatible_artifact_sha256="d" * 64,
                initialization_probe=probe,
                supported_launcher_sha256=SHA_C,
                incompatible_launcher_sha256=SHA_D,
                supported_host_inventory_sha256=SHA_D,
                incompatible_host_inventory_sha256=SHA_C,
                public_contract=public_contract(),
                public_contract_sha256=PUBLIC_CONTRACT_SHA256,
                required_incompatible_facts=("v1.5.4", "v1.5.3"),
                forbidden_diagnostic_values=(),
                replacements=((root, "<root>"),),
                output_path=root / "failed-init.json",
            )
            self.assertEqual(result["status"], "failed")
            self.assertTrue(probe.checked)

    def test_invalid_deployment_never_starts_query_processes(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            candidate = admitted_candidate(root)
            deployment = replace(
                deployment_evidence(candidate),
                status="failed",
                deployed_artifact_sha256=None,
            )
            supported = FakeRunner(supported_observations())
            with self.assertRaisesRegex(OracleError, "passing admitted"):
                evaluate_row(
                    candidate=candidate,
                    deployment=deployment,
                    supported_runner=supported,
                    incompatible_runner=FakeRunner([incompatible_observation()]),
                    incompatible_row=row(),
                    incompatible_artifact_size=6,
                    incompatible_artifact_sha256="d" * 64,
                    initialization_probe=FakeInitializationProbe(),
                    supported_launcher_sha256=SHA_C,
                    incompatible_launcher_sha256=SHA_D,
                    supported_host_inventory_sha256=SHA_D,
                    incompatible_host_inventory_sha256=SHA_C,
                    public_contract=public_contract(),
                    public_contract_sha256=PUBLIC_CONTRACT_SHA256,
                    required_incompatible_facts=("v1.5.4",),
                    forbidden_diagnostic_values=(),
                    replacements=(),
                    output_path=root / "must-not-exist.json",
                )
            self.assertEqual(supported.calls, [])
            self.assertFalse((root / "must-not-exist.json").exists())


if __name__ == "__main__":
    unittest.main()

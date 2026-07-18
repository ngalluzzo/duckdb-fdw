#!/usr/bin/env python3

from __future__ import annotations

import importlib.util
import json
import pathlib
import shutil
import tempfile
import unittest


REPOSITORY = pathlib.Path(__file__).resolve().parents[2]
VERIFIER_PATH = REPOSITORY / "scripts/verify-source-identities.py"
SPEC = importlib.util.spec_from_file_location("source_identity_verifier", VERIFIER_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("source identity verifier could not be imported")
VERIFIER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(VERIFIER)

SOURCE_PATHS = (
    "extension_config.cmake",
    "fixtures/example/compiled_connector.snapshot",
    "fixtures/example/items.json",
    "release/0.1.0/pins.json",
    "release/0.1.0/public_contract.json",
    "release/0.2.0/pins.json",
    "release/0.2.0/public_contract.json",
    "src/include/duckdb_api/embedded_example.hpp",
)


class SourceIdentityContractTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = pathlib.Path(self.temporary.name)
        for relative in SOURCE_PATHS:
            source = REPOSITORY / relative
            target = self.root / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, target)

    def json(self, relative: str) -> dict:
        return json.loads((self.root / relative).read_text())

    def write_json(self, relative: str, value: dict) -> None:
        (self.root / relative).write_text(
            json.dumps(value, indent=2, sort_keys=True) + "\n"
        )

    def test_selects_current_version_and_preserves_historical_contract(self) -> None:
        result = VERIFIER.verify(self.root)
        self.assertEqual(result["version"], "0.2.0")
        self.assertEqual(result["duckdb_version"], "1.5.4")
        self.assertEqual(
            result["public_contract_sha256"],
            "bbba900cb94f6289c9282750ed6d15a5356f6c0de9aa00fa5ae3a0ed8e452160",
        )

    def test_rejects_current_and_historical_contract_pin_drift(self) -> None:
        current_pins = self.json("release/0.2.0/pins.json")
        current_pins["identities"]["public_contract_sha256"] = "0" * 64
        self.write_json("release/0.2.0/pins.json", current_pins)
        with self.assertRaisesRegex(AssertionError, "current public contract digest"):
            VERIFIER.verify(self.root)

        shutil.copyfile(
            REPOSITORY / "release/0.2.0/pins.json",
            self.root / "release/0.2.0/pins.json",
        )
        historical_pins = self.json("release/0.1.0/pins.json")
        historical_pins["identities"]["public_contract_sha256"] = "0" * 64
        self.write_json("release/0.1.0/pins.json", historical_pins)
        with self.assertRaisesRegex(AssertionError, "historical 0.1"):
            VERIFIER.verify(self.root)

    def test_rejects_public_behavior_drift_even_when_repinned(self) -> None:
        contract_path = self.root / "release/0.2.0/public_contract.json"
        contract = self.json("release/0.2.0/public_contract.json")
        contract["rows"][0][1] = "drifted"
        self.write_json("release/0.2.0/public_contract.json", contract)
        pins = self.json("release/0.2.0/pins.json")
        pins["identities"]["public_contract_sha256"] = VERIFIER.canonical_digest(
            json.loads(contract_path.read_text())
        )
        self.write_json("release/0.2.0/pins.json", pins)
        with self.assertRaisesRegex(AssertionError, "beyond extension version"):
            VERIFIER.verify(self.root)

    def test_rejects_jointly_repinned_historical_and_current_behavior(self) -> None:
        for version in ("0.1.0", "0.2.0"):
            contract_path = f"release/{version}/public_contract.json"
            pins_path = f"release/{version}/pins.json"
            contract = self.json(contract_path)
            contract["rows"][0][1] = "coordinated-drift"
            self.write_json(contract_path, contract)
            pins = self.json(pins_path)
            pins["identities"]["public_contract_sha256"] = (
                VERIFIER.canonical_digest(contract)
            )
            self.write_json(pins_path, pins)
        with self.assertRaisesRegex(AssertionError, "historical 0.1 adjacent pin"):
            VERIFIER.verify(self.root)

    def test_rejects_project_and_duckdb_identity_drift(self) -> None:
        pins = self.json("release/0.2.0/pins.json")
        pins["project"]["tag"] = "v0.2.1"
        self.write_json("release/0.2.0/pins.json", pins)
        with self.assertRaisesRegex(AssertionError, "current project identity"):
            VERIFIER.verify(self.root)

        shutil.copyfile(
            REPOSITORY / "release/0.2.0/pins.json",
            self.root / "release/0.2.0/pins.json",
        )
        pins = self.json("release/0.2.0/pins.json")
        pins["dependencies"]["duckdb"]["commit"] = "0" * 40
        pins["dependencies"]["duckdb"]["git_describe"] = "v1.5.4-0-g0000000000"
        self.write_json("release/0.2.0/pins.json", pins)
        with self.assertRaisesRegex(AssertionError, "different DuckDB identity"):
            VERIFIER.verify(self.root)

    def test_rejects_anything_except_the_single_extension_declaration(self) -> None:
        accepted = (REPOSITORY / "extension_config.cmake").read_text()
        invalid = {
            "comment decoy": '# EXTENSION_VERSION "0.1.0"\n',
            "false block": (
                "if(FALSE)\n"
                "  duckdb_extension_load(duckdb_api\n"
                "    SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}\n"
                '    EXTENSION_VERSION "0.1.0"\n'
                "  )\n"
                "endif()\n"
            ),
            "decoy command": 'set(DECOY "EXTENSION_VERSION 0.1.0")\n',
            "extra command": accepted + "set(EXTRA_COMMAND TRUE)\n",
            "missing version": (
                "duckdb_extension_load(duckdb_api\n"
                "  SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}\n"
                ")\n"
            ),
            "unquoted version": accepted.replace('"0.2.0"', "0.2.0"),
            "nonrelease version": accepted.replace('"0.2.0"', '"../../0.1.0"'),
        }
        for label, content in invalid.items():
            with self.subTest(label=label):
                (self.root / "extension_config.cmake").write_text(content)
                with self.assertRaisesRegex(AssertionError, "sole accepted"):
                    VERIFIER.verify(self.root)


if __name__ == "__main__":
    unittest.main()

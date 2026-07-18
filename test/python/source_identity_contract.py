#!/usr/bin/env python3

from __future__ import annotations

import importlib.util
import json
import os
import pathlib
import shutil
import subprocess
import sys
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
    "release/0.1.0/pins.json",
    "release/0.1.0/public_contract.json",
    "release/0.2.0/pins.json",
    "release/0.2.0/public_contract.json",
    "release/0.3.0/pins.json",
    "release/0.3.0/public_contract.json",
    "src/connector.cpp",
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

    def test_selects_current_version_without_legacy_fixture_sources(self) -> None:
        result = VERIFIER.verify(self.root)
        self.assertFalse((self.root / "fixtures").exists())
        self.assertEqual(result["version"], "0.3.0")
        self.assertEqual(result["duckdb_version"], "1.5.4")
        self.assertEqual(
            result["public_contract_sha256"],
            "f5d9a5c14ef603fef34bf7154ad2272e86742fec0af994aacfbfec4afe84c8e9",
        )

    def test_rejects_current_and_historical_contract_pin_drift(self) -> None:
        current_pins = self.json("release/0.3.0/pins.json")
        current_pins["identities"]["public_contract"][
            "canonical_json_sha256"
        ] = "0" * 64
        self.write_json("release/0.3.0/pins.json", current_pins)
        with self.assertRaisesRegex(AssertionError, "current public contract digest"):
            VERIFIER.verify(self.root)

        shutil.copyfile(
            REPOSITORY / "release/0.3.0/pins.json",
            self.root / "release/0.3.0/pins.json",
        )
        historical_pins = self.json("release/0.1.0/pins.json")
        historical_pins["identities"]["public_contract_sha256"] = "0" * 64
        self.write_json("release/0.1.0/pins.json", historical_pins)
        with self.assertRaisesRegex(AssertionError, "historical 0.1"):
            VERIFIER.verify(self.root)

    def test_rejects_historical_source_identity_pin_drift(self) -> None:
        historical_pins = self.json("release/0.2.0/pins.json")
        historical_pins["identities"]["fixture_sha256"] = "0" * 64
        self.write_json("release/0.2.0/pins.json", historical_pins)
        with self.assertRaisesRegex(
            AssertionError, "historical 0.2.0 pins record drifted"
        ):
            VERIFIER.verify(self.root)

    def test_rejects_complete_historical_pin_record_drift(self) -> None:
        mutations = (
            ("DuckDB tree", "0.2.0", ("dependencies", "duckdb", "tree"), "0" * 40),
            ("product cell", "0.1.0", ("product_cell", "host"), "changed"),
            (
                "sanitizer cell",
                "0.1.0",
                ("sanitizer_cell", "base_image"),
                "docker.io/example/changed@sha256:" + "0" * 64,
            ),
            ("tool", "0.1.0", ("tools", "ninja_linux", "sha256"), "0" * 64),
            (
                "dependency",
                "0.1.0",
                ("dependencies", "extension_template", "tree"),
                "0" * 40,
            ),
        )
        for label, version, field_path, replacement in mutations:
            with self.subTest(label=label):
                relative = f"release/{version}/pins.json"
                shutil.copyfile(REPOSITORY / relative, self.root / relative)
                pins = self.json(relative)
                target = pins
                for field in field_path[:-1]:
                    target = target[field]
                target[field_path[-1]] = replacement
                self.write_json(relative, pins)
                with self.assertRaisesRegex(
                    AssertionError, f"historical {version} pins record drifted"
                ):
                    VERIFIER.verify(self.root)

    def test_rejects_connector_leaf_symlink_into_legacy_fixture_tree(self) -> None:
        legacy = self.root / "fixtures/example"
        legacy.mkdir(parents=True)
        shutil.copyfile(REPOSITORY / "src/connector.cpp", legacy / "connector.cpp")
        connector = self.root / "src/connector.cpp"
        connector.unlink()
        connector.symlink_to("../fixtures/example/connector.cpp")
        with self.assertRaisesRegex(AssertionError, "identity path contains a symlink"):
            VERIFIER.verify(self.root)

    def test_rejects_connector_parent_symlink_to_external_tree(self) -> None:
        external_temporary = tempfile.TemporaryDirectory()
        self.addCleanup(external_temporary.cleanup)
        external = pathlib.Path(external_temporary.name)
        shutil.copyfile(REPOSITORY / "src/connector.cpp", external / "connector.cpp")
        shutil.rmtree(self.root / "src")
        (self.root / "src").symlink_to(external, target_is_directory=True)
        with self.assertRaisesRegex(AssertionError, "identity path contains a symlink"):
            VERIFIER.verify(self.root)

    def test_rejects_connector_hard_link_into_legacy_fixture_tree(self) -> None:
        legacy = self.root / "fixtures/example"
        legacy.mkdir(parents=True)
        os.link(self.root / "src/connector.cpp", legacy / "connector.cpp")
        with self.assertRaisesRegex(AssertionError, "identity file has multiple hard links"):
            VERIFIER.verify(self.root)

    def test_rejects_duplicate_json_keys_at_every_depth(self) -> None:
        relative = "release/0.2.0/pins.json"
        path = self.root / relative
        original = path.read_text()
        mutations = {
            "top level": original.rstrip()[:-1] + ',\n  "project": {}\n}\n',
            "nested": original.replace(
                '    "version": "0.2.0"',
                '    "version": "0.2.0",\n    "version": "0.2.0"',
                1,
            ),
        }
        for label, mutation in mutations.items():
            with self.subTest(label=label):
                path.write_text(mutation)
                with self.assertRaisesRegex(
                    AssertionError, "identity file contains a duplicate JSON key"
                ):
                    VERIFIER.verify(self.root)
                path.write_text(original)

    def test_rejects_connector_fifo_without_blocking(self) -> None:
        connector = self.root / "src/connector.cpp"
        connector.unlink()
        os.mkfifo(connector)
        probe = """
import importlib.util
import pathlib
import sys

spec = importlib.util.spec_from_file_location("source_identity_verifier", sys.argv[1])
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
try:
    module.verify(pathlib.Path(sys.argv[2]))
except AssertionError as error:
    print(error)
    raise SystemExit(0)
raise SystemExit("FIFO was accepted")
"""
        completed = subprocess.run(
            [sys.executable, "-I", "-B", "-c", probe, str(VERIFIER_PATH), str(self.root)],
            check=False,
            capture_output=True,
            text=True,
            timeout=2,
        )
        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertIn("identity path does not name a regular file", completed.stdout)

    def test_rejects_current_public_contract_and_connector_drift(self) -> None:
        contract = self.json("release/0.3.0/public_contract.json")
        contract["relation"]["cardinality"]["maximum"] = 4
        self.write_json("release/0.3.0/public_contract.json", contract)
        with self.assertRaisesRegex(AssertionError, "public contract digest"):
            VERIFIER.verify(self.root)

        shutil.copyfile(
            REPOSITORY / "release/0.3.0/public_contract.json",
            self.root / "release/0.3.0/public_contract.json",
        )
        with (self.root / "src/connector.cpp").open("a", encoding="utf-8") as connector:
            connector.write("// drift\n")
        with self.assertRaisesRegex(AssertionError, "connector source digest"):
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
        with self.assertRaisesRegex(AssertionError, "historical 0.1.0 pins record drifted"):
            VERIFIER.verify(self.root)

    def test_rejects_project_and_duckdb_identity_drift(self) -> None:
        pins = self.json("release/0.3.0/pins.json")
        pins["project"]["tag"] = "v0.3.1"
        self.write_json("release/0.3.0/pins.json", pins)
        with self.assertRaisesRegex(AssertionError, "current project identity"):
            VERIFIER.verify(self.root)

        shutil.copyfile(
            REPOSITORY / "release/0.3.0/pins.json",
            self.root / "release/0.3.0/pins.json",
        )
        pins = self.json("release/0.3.0/pins.json")
        pins["dependencies"]["duckdb"]["commit"] = "0" * 40
        pins["dependencies"]["duckdb"]["git_describe"] = "v1.5.4-0-g0000000000"
        self.write_json("release/0.3.0/pins.json", pins)
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
            "unquoted version": accepted.replace('"0.3.0"', "0.3.0"),
            "nonrelease version": accepted.replace('"0.3.0"', '"../../0.1.0"'),
        }
        for label, content in invalid.items():
            with self.subTest(label=label):
                (self.root / "extension_config.cmake").write_text(content)
                with self.assertRaisesRegex(AssertionError, "sole accepted"):
                    VERIFIER.verify(self.root)


if __name__ == "__main__":
    unittest.main()

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
GRAPH_VERIFIER_PATH = REPOSITORY / "scripts/verify-native-product-sources.py"


def load_module(name: str, path: pathlib.Path):
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot import {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


VERIFIER = load_module("source_identity_verifier", VERIFIER_PATH)
GRAPH_VERIFIER = load_module("native_product_source_verifier", GRAPH_VERIFIER_PATH)
CURRENT_PINS = json.loads((REPOSITORY / "release/0.5.0/pins.json").read_text())
CURRENT_NATIVE_PATHS = tuple(
    CURRENT_PINS["identities"]["native_product_sources"]["paths"]
)
CURRENT_CONTROLLED_PATHS = tuple(
    CURRENT_PINS["identities"]["controlled_product_sources"]["paths"]
)
SOURCE_PATHS = (
    "extension_config.cmake",
    "cmake/DuckDBApiProductSources.cmake",
    "release/0.1.0/pins.json",
    "release/0.1.0/public_contract.json",
    "release/0.2.0/pins.json",
    "release/0.2.0/public_contract.json",
    "release/0.3.0/pins.json",
    "release/0.3.0/public_contract.json",
    "release/0.4.0/pins.json",
    "release/0.4.0/public_contract.json",
    "release/0.5.0/pins.json",
    "release/0.5.0/public_contract.json",
) + CURRENT_NATIVE_PATHS + CURRENT_CONTROLLED_PATHS


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

    def write_json(self, relative: str, value: object) -> None:
        (self.root / relative).write_text(
            json.dumps(value, indent=2, sort_keys=True) + "\n"
        )

    def restore(self, relative: str) -> None:
        shutil.copyfile(REPOSITORY / relative, self.root / relative)

    def test_selects_complete_current_path_bound_identity(self) -> None:
        result = VERIFIER.verify(self.root)
        self.assertEqual(result["version"], "0.5.0")
        self.assertEqual(result["duckdb_version"], "1.5.4")
        self.assertEqual(
            result["public_contract_sha256"],
            CURRENT_PINS["identities"]["public_contract"][
                "canonical_json_sha256"
            ],
        )
        self.assertEqual(
            result["native_product_sources_sha256"],
            CURRENT_PINS["identities"]["native_product_sources"]["sha256"],
        )
        self.assertEqual(
            result["controlled_product_sources_sha256"],
            CURRENT_PINS["identities"]["controlled_product_sources"]["sha256"],
        )

    def test_identity_is_independent_of_absolute_worktree_path(self) -> None:
        first = VERIFIER.verify(self.root)
        second_temporary = tempfile.TemporaryDirectory(
            prefix="duckdb-api-other-worktree-"
        )
        self.addCleanup(second_temporary.cleanup)
        second = pathlib.Path(second_temporary.name) / "nested checkout"
        shutil.copytree(self.root, second)
        self.assertEqual(VERIFIER.verify(second), first)

    def test_rejects_mutated_native_and_controlled_sources(self) -> None:
        for label, relative, expected in (
            (
                "native",
                "src/connector.cpp",
                "native product source digest",
            ),
            (
                "controlled",
                "test/cpp/support/controlled_product_composition.cpp",
                "controlled product source digest",
            ),
        ):
            with self.subTest(label=label):
                path = self.root / relative
                original = path.read_bytes()
                path.write_bytes(original + b"\n// identity drift\n")
                with self.assertRaisesRegex(AssertionError, expected):
                    VERIFIER.verify(self.root)
                path.write_bytes(original)

    def test_rejects_removed_added_and_renamed_native_inputs(self) -> None:
        removed = self.root / "src/connector.cpp"
        removed_bytes = removed.read_bytes()
        removed.unlink()
        with self.assertRaisesRegex(
            AssertionError, "source inventory is incomplete|cannot be read"
        ):
            VERIFIER.verify(self.root)
        removed.write_bytes(removed_bytes)

        extra = self.root / "src/unregistered_product_input.cpp"
        extra.write_text("int unregistered_product_input;\n")
        with self.assertRaisesRegex(AssertionError, "source inventory is incomplete"):
            VERIFIER.verify(self.root)
        extra.unlink()

        renamed = self.root / "src/renamed_connector.cpp"
        removed.rename(renamed)
        with self.assertRaisesRegex(
            AssertionError, "source inventory is incomplete|cannot be read"
        ):
            VERIFIER.verify(self.root)

    def test_rejects_source_path_pin_substitution(self) -> None:
        mutations = {
            "absolute": ["/tmp/substitute.cpp"],
            "parent": ["src/../outside.cpp"],
            "duplicate": [CURRENT_NATIVE_PATHS[0], CURRENT_NATIVE_PATHS[0]],
            "unsorted": list(reversed(CURRENT_NATIVE_PATHS)),
        }
        for label, paths in mutations.items():
            with self.subTest(label=label):
                pins = self.json("release/0.5.0/pins.json")
                pins["identities"]["native_product_sources"]["paths"] = paths
                self.write_json("release/0.5.0/pins.json", pins)
                with self.assertRaisesRegex(
                    AssertionError, "repository-relative|sorted unique"
                ):
                    VERIFIER.verify(self.root)
                self.restore("release/0.5.0/pins.json")

    def test_rejects_public_and_controlled_build_graph_drift(self) -> None:
        mutations = {
            "public omission": ("public_translation_units", lambda values: values[:-1]),
            "public duplicate": (
                "public_translation_units",
                lambda values: values + values[-1:],
            ),
            "controlled omission": (
                "controlled_translation_units",
                lambda values: values[:-1],
            ),
            "controlled private substitution": (
                "controlled_translation_units",
                lambda values: values[:-1] + ["test/cpp/fake_private.cpp"],
            ),
        }
        for label, (key, mutate) in mutations.items():
            with self.subTest(label=label):
                pins = self.json("release/0.5.0/pins.json")
                values = pins["identities"]["build_graph"][key]
                pins["identities"]["build_graph"][key] = mutate(values)
                self.write_json("release/0.5.0/pins.json", pins)
                with self.assertRaisesRegex(
                    AssertionError,
                    "translation units are malformed|build graph omits|wrong composition",
                ):
                    VERIFIER.verify(self.root)
                self.restore("release/0.5.0/pins.json")

    def test_configured_build_graph_must_match_release_order_and_inventory(self) -> None:
        pins_path = self.root / "release/0.5.0/pins.json"
        expected = self.json("release/0.5.0/pins.json")["identities"][
            "build_graph"
        ]
        observed = self.root / "observed.json"
        self.write_json("observed.json", expected)
        counts = GRAPH_VERIFIER.verify(pins_path, observed)
        self.assertEqual(
            counts["public_translation_units"],
            len(expected["public_translation_units"]),
        )

        for label, mutation in (
            (
                "omission",
                expected["public_translation_units"][:-1],
            ),
            (
                "reorder",
                list(reversed(expected["public_translation_units"])),
            ),
            (
                "extra",
                expected["public_translation_units"] + ["src/extra.cpp"],
            ),
        ):
            with self.subTest(label=label):
                changed = json.loads(json.dumps(expected))
                changed["public_translation_units"] = mutation
                self.write_json("observed.json", changed)
                with self.assertRaisesRegex(
                    AssertionError, "differs from the release identity"
                ):
                    GRAPH_VERIFIER.verify(pins_path, observed)

    def test_finalized_target_graph_captures_late_target_sources(self) -> None:
        fixture = self.root / "configured-graph-fixture"
        fixture.mkdir()
        (fixture / "first.cpp").write_text("int first_product_source;\n")
        (fixture / "late.cpp").write_text("int late_product_source;\n")
        (fixture / "CMakeLists.txt").write_text(
            """
cmake_minimum_required(VERSION 3.19)
project(product_source_fixture LANGUAGES CXX)
include("${CMAKE_CURRENT_LIST_DIR}/../cmake/DuckDBApiProductSources.cmake")
add_library(public_static STATIC first.cpp)
add_library(public_loadable MODULE first.cpp)
add_library(controlled MODULE first.cpp)
cmake_language(DEFER CALL target_sources public_static PRIVATE late.cpp)
cmake_language(DEFER CALL target_sources public_loadable PRIVATE late.cpp)
duckdb_api_defer_product_source_record(
  OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/observed.json"
  PUBLIC_STATIC public_static
  PUBLIC_LOADABLE public_loadable
  CONTROLLED controlled)
""".lstrip()
        )
        build = fixture / "build"
        completed = subprocess.run(
            ["cmake", "-S", str(fixture), "-B", str(build)],
            check=False,
            capture_output=True,
            text=True,
        )
        self.assertEqual(completed.returncode, 0, completed.stderr)
        observed = self.json("configured-graph-fixture/build/observed.json")
        self.assertEqual(
            observed["public_translation_units"], ["first.cpp", "late.cpp"]
        )
        self.assertEqual(observed["controlled_translation_units"], ["first.cpp"])

        pins = json.loads(json.dumps(CURRENT_PINS))
        pins["identities"]["build_graph"] = {
            "public_translation_units": ["first.cpp"],
            "controlled_translation_units": ["first.cpp"],
        }
        self.write_json("configured-graph-fixture/pins.json", pins)
        with self.assertRaisesRegex(
            AssertionError, "differs from the release identity"
        ):
            GRAPH_VERIFIER.verify(
                fixture / "pins.json", build / "observed.json"
            )

    def test_finalized_target_graph_rejects_unbound_inputs(self) -> None:
        fixture_root = self.root / "invalid-configured-graph-fixtures"
        fixture_root.mkdir()
        outside = self.root / "outside.cpp"
        outside.write_text("int outside_product_source;\n")
        cases = {
            "generated": (
                "set_source_files_properties(generated.cpp PROPERTIES GENERATED TRUE)\n"
                "target_sources(public_static PRIVATE generated.cpp)\n"
                "target_sources(public_loadable PRIVATE generated.cpp)\n",
                "product target source is generated",
            ),
            "outside": (
                f'target_sources(public_static PRIVATE "{outside}")\n'
                f'target_sources(public_loadable PRIVATE "{outside}")\n',
                "product target source is outside the repository",
            ),
            "duplicate": (
                "target_sources(public_static PRIVATE first.cpp)\n"
                "target_sources(public_loadable PRIVATE first.cpp)\n",
                "product target source is duplicated",
            ),
            "symlink parent": (
                "target_sources(public_static PRIVATE alias/aliased.cpp)\n"
                "target_sources(public_loadable PRIVATE alias/aliased.cpp)\n",
                "product target source uses a filesystem alias",
            ),
        }
        for label, (mutation, diagnostic) in cases.items():
            with self.subTest(label=label):
                fixture = fixture_root / label
                fixture.mkdir()
                (fixture / "first.cpp").write_text("int first_product_source;\n")
                if label == "symlink parent":
                    real = fixture / "real"
                    real.mkdir()
                    (real / "aliased.cpp").write_text("int aliased_product_source;\n")
                    (fixture / "alias").symlink_to("real", target_is_directory=True)
                (fixture / "CMakeLists.txt").write_text(
                    (
                        """
cmake_minimum_required(VERSION 3.19)
project(product_source_fixture LANGUAGES CXX)
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/DuckDBApiProductSources.cmake")
add_library(public_static STATIC first.cpp)
add_library(public_loadable MODULE first.cpp)
add_library(controlled MODULE first.cpp)
duckdb_api_defer_product_source_record(
  OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/observed.json"
  PUBLIC_STATIC public_static
  PUBLIC_LOADABLE public_loadable
  CONTROLLED controlled)
""".lstrip()
                        + mutation
                    )
                )
                completed = subprocess.run(
                    ["cmake", "-S", str(fixture), "-B", str(fixture / "build")],
                    check=False,
                    capture_output=True,
                    text=True,
                )
                self.assertNotEqual(completed.returncode, 0)
                self.assertIn(diagnostic, completed.stderr)

    def test_cached_and_fresh_workflows_project_the_graph_authority(self) -> None:
        cached = (REPOSITORY / "scripts/lib/native-dev-build.sh").read_text()
        fresh = (REPOSITORY / "scripts/run-native-product-tests.sh").read_text()
        for label, script in (("cached", cached), ("fresh", fresh)):
            with self.subTest(label=label):
                self.assertIn("/cmake/", script)
                self.assertIn("verify-native-product-sources.py", script)
                self.assertIn("duckdb_api_product_sources.json", script)

    def test_product_graph_observer_is_the_final_cmake_command(self) -> None:
        cmake = (REPOSITORY / "CMakeLists.txt").read_text()
        active = "\n".join(
            line for line in cmake.splitlines() if not line.lstrip().startswith("#")
        ).rstrip()
        expected = """duckdb_api_defer_product_source_record(
  OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/duckdb_api_product_sources.json"
  PUBLIC_STATIC "${TARGET_NAME}_extension"
  PUBLIC_LOADABLE "${TARGET_NAME}_loadable_extension"
  CONTROLLED "duckdb_api_controlled_loadable_extension")"""
        self.assertEqual(active.count("duckdb_api_defer_product_source_record("), 1)
        self.assertTrue(active.endswith(expected))

    def test_rejects_current_contract_and_identity_pin_drift(self) -> None:
        contract = self.json("release/0.5.0/public_contract.json")
        contract["relations"][1]["cardinality"]["maximum"] = 2
        self.write_json("release/0.5.0/public_contract.json", contract)
        with self.assertRaisesRegex(AssertionError, "public contract digest"):
            VERIFIER.verify(self.root)
        self.restore("release/0.5.0/public_contract.json")

        pins = self.json("release/0.5.0/pins.json")
        pins["identities"]["native_product_sources"]["sha256"] = "0" * 64
        self.write_json("release/0.5.0/pins.json", pins)
        with self.assertRaisesRegex(AssertionError, "native product source digest"):
            VERIFIER.verify(self.root)

    def test_preserves_every_historical_release_record(self) -> None:
        for version in ("0.1.0", "0.2.0", "0.3.0", "0.4.0"):
            with self.subTest(version=version):
                relative = f"release/{version}/pins.json"
                pins = self.json(relative)
                pins["project"]["version"] = "9.9.9"
                self.write_json(relative, pins)
                with self.assertRaisesRegex(
                    AssertionError, f"historical {version} pins record drifted"
                ):
                    VERIFIER.verify(self.root)
                self.restore(relative)

    def test_rejects_jointly_repinned_historical_behavior(self) -> None:
        contract_path = "release/0.4.0/public_contract.json"
        pins_path = "release/0.4.0/pins.json"
        contract = self.json(contract_path)
        contract["relations"][1]["cardinality"]["maximum"] = 4
        self.write_json(contract_path, contract)
        pins = self.json(pins_path)
        pins["identities"]["public_contract"][
            "canonical_json_sha256"
        ] = VERIFIER.canonical_digest(contract)
        self.write_json(pins_path, pins)
        with self.assertRaisesRegex(AssertionError, "historical 0.4.0 pins record"):
            VERIFIER.verify(self.root)

    def test_rejects_duplicate_json_keys(self) -> None:
        path = self.root / "release/0.5.0/pins.json"
        original = path.read_text()
        path.write_text(original.rstrip()[:-1] + ',\n  "project": {}\n}\n')
        with self.assertRaisesRegex(AssertionError, "duplicate JSON key"):
            VERIFIER.verify(self.root)

    def test_rejects_source_symlinks_hard_links_and_special_files(self) -> None:
        source = self.root / "src/connector.cpp"
        source_bytes = source.read_bytes()
        source.unlink()
        source.symlink_to("authorization.cpp")
        with self.assertRaisesRegex(AssertionError, "inventory contains a symlink"):
            VERIFIER.verify(self.root)
        source.unlink()
        source.write_bytes(source_bytes)

        alias = self.root / "src/connector-hard-link.cpp"
        os.link(source, alias)
        with self.assertRaisesRegex(
            AssertionError, "inventory is incomplete|multiple hard links"
        ):
            VERIFIER.verify(self.root)
        alias.unlink()

        special = self.root / "src/unregistered.fifo"
        os.mkfifo(special)
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
        self.assertIn("inventory contains a special file", completed.stdout)

    def test_rejects_anything_except_the_single_extension_declaration(self) -> None:
        accepted = (REPOSITORY / "extension_config.cmake").read_text()
        invalid = {
            "comment decoy": '# EXTENSION_VERSION "0.5.0"\n',
            "false block": (
                "if(FALSE)\n"
                "  duckdb_extension_load(duckdb_api\n"
                "    SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}\n"
                '    EXTENSION_VERSION "0.5.0"\n'
                "  )\n"
                "endif()\n"
            ),
            "extra command": accepted + "set(EXTRA_COMMAND TRUE)\n",
            "missing version": (
                "duckdb_extension_load(duckdb_api\n"
                "  SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}\n"
                ")\n"
            ),
            "unquoted version": accepted.replace('"0.5.0"', "0.5.0"),
            "nonrelease version": accepted.replace('"0.5.0"', '"../../0.1.0"'),
        }
        for label, content in invalid.items():
            with self.subTest(label=label):
                (self.root / "extension_config.cmake").write_text(content)
                with self.assertRaisesRegex(AssertionError, "sole accepted"):
                    VERIFIER.verify(self.root)


if __name__ == "__main__":
    unittest.main()

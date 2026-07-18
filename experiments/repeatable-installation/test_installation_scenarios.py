"""Focused assertion tests for installation scenario observations."""

from __future__ import annotations

import pathlib
import tempfile
import unittest

from installation_scenarios import (
    installed_artifact,
    require_duckdb_identity,
    require_fragments,
    require_install_source,
    require_no_registration,
    run_installation_scenarios,
)
from query_oracle_test_support import trial_package
from trial_inputs import verify_trial_inputs


class FakeScenarioRunner:
    """Provide deterministic host observations while exercising orchestration."""

    def __init__(self, behavior: dict[str, object]):
        self.behavior = behavior
        self.invocations = 0
        self.actions: list[tuple[str, pathlib.Path | None, bool]] = []

    def run(
        self,
        python: pathlib.Path,
        action: str,
        database: pathlib.Path,
        extension_directory: pathlib.Path,
        *,
        artifact: pathlib.Path | None = None,
        allow_unsigned: bool,
    ) -> dict[str, object]:
        del python, database
        self.invocations += 1
        self.actions.append((action, artifact, allow_unsigned))
        index = self.invocations
        supported_identity = ["v1.5.4", "08e34c447b"]
        if index <= 3:
            installed = (
                extension_directory
                / "v1.5.4/osx_arm64/duckdb_api.duckdb_extension"
            )
            installed.parent.mkdir(parents=True, exist_ok=True)
            assert artifact is not None or action == "load-query"
            if artifact is not None:
                installed.write_bytes(artifact.read_bytes())
            extension = {
                "install_mode": "CUSTOM_PATH",
                "install_path": str(installed),
                "installed": True,
                "installed_from": str(self.actions[0][1]),
                "loaded": action == "load-query",
                "name": "duckdb_api",
                "version": "0.1.0",
            }
            observation: dict[str, object] = {
                "action": action,
                "duckdb": supported_identity,
                "extension": extension,
                "ok": True,
                "pid": 1000 + index,
                "registered_functions": (
                    [["duckdb_api_scan", "table"]]
                    if action == "load-query"
                    else []
                ),
            }
            if action == "load-query":
                observation["behavior"] = self.behavior
            return observation

        diagnostics = {
            4: "doesn't have a valid signature; see securing_extensions",
            5: "built for v1.5.4; this host is v1.5.3",
            6: "built for linux_amd64; host accepts osx_arm64 platform",
        }
        return {
            "action": action,
            "diagnostic": diagnostics[index],
            "duckdb": (
                ["v1.5.3", "14eca11bd9"]
                if index == 5
                else supported_identity
            ),
            "extension": None,
            "ok": False,
            "registered_functions": [],
        }


class ScenarioAssertionTests(unittest.TestCase):
    def test_complete_matrix_orders_hosts_and_stops_corruption_before_host(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            inputs, _, _ = trial_package(root)
            inputs.verifier.write_text(
                """
import hashlib
import json
import pathlib
import sys

manifest = json.loads(pathlib.Path(sys.argv[1]).read_text())
artifact = pathlib.Path(sys.argv[2]).read_bytes()
if hashlib.sha256(artifact).hexdigest() == manifest["artifact"]["sha256"]:
    raise SystemExit(0)
print("release artifact does not match the tracked trust record", file=sys.stderr)
raise SystemExit(1)
""".lstrip(),
                encoding="utf-8",
            )
            verified = verify_trial_inputs(inputs)
            runner = FakeScenarioRunner(verified.public_contract)
            scenario_root = root / "scenarios"
            scenario_root.mkdir()

            result = run_installation_scenarios(
                runner,
                inputs,
                verified,
                scenario_root,
            )

            self.assertEqual(
                [action for action, _, _ in runner.actions],
                ["install", "install", "load-query", "install", "install", "install"],
            )
            self.assertEqual(runner.invocations, 6)
            self.assertEqual(
                result["corrupted_artifact"]["query_host_invocations"],
                0,
            )
            self.assertEqual(runner.actions[0][1], inputs.artifact)
            self.assertEqual(runner.actions[5][1], inputs.wrong_platform_artifact)

    def test_rejection_requires_diagnostic_and_no_visible_registration(self) -> None:
        observation = {
            "diagnostic": "built for linux_amd64, host accepts osx_arm64",
            "extension": None,
            "ok": False,
            "registered_functions": [],
        }

        diagnostic = require_no_registration(observation, "wrong-platform")
        require_fragments(
            diagnostic,
            "wrong-platform",
            ("linux_amd64", "osx_arm64"),
        )

        observation["registered_functions"] = [["duckdb_api_scan", "table"]]
        with self.assertRaisesRegex(AssertionError, "registered extension functions"):
            require_no_registration(observation, "wrong-platform")

    def test_complete_duckdb_identity_includes_source_commit(self) -> None:
        expected = ("v1.5.4", "08e34c447b")
        require_duckdb_identity({"duckdb": list(expected)}, expected, "supported")

        with self.assertRaisesRegex(AssertionError, "wrong DuckDB host"):
            require_duckdb_identity(
                {"duckdb": ["v1.5.4", "counterfeit"]},
                expected,
                "supported",
            )

    def test_install_paths_must_match_admitted_clean_state(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            extensions = root / "extensions"
            extensions.mkdir()
            accepted = extensions / "duckdb_api.duckdb_extension"
            accepted.write_bytes(b"accepted")
            outside = root / "outside.duckdb_extension"
            outside.write_bytes(b"outside")
            alias = root / "artifact-alias.duckdb_extension"
            alias.symlink_to(accepted)

            require_install_source(
                {"installed_from": str(accepted)},
                accepted,
                "first install",
            )
            with self.assertRaisesRegex(AssertionError, "differs"):
                require_install_source(
                    {"installed_from": str(outside)},
                    accepted,
                    "first install",
                )
            with self.assertRaisesRegex(AssertionError, "differs"):
                require_install_source(
                    {"installed_from": str(alias)},
                    accepted,
                    "first install",
                )
            self.assertEqual(
                installed_artifact({"install_path": str(accepted)}, extensions),
                accepted.resolve(),
            )
            with self.assertRaisesRegex(AssertionError, "escaped"):
                installed_artifact({"install_path": str(outside)}, extensions)

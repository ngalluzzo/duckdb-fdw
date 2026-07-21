#!/usr/bin/env python3
"""Mutation tests for the fail-closed public-surface inventory gate."""

from __future__ import annotations

import copy
import hashlib
import json
import pathlib
import sys
import tempfile
import unittest

REPOSITORY_ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPOSITORY_ROOT / "scripts"))

from public_surface.inventory import (  # noqa: E402
    InventoryError,
    load_json,
    verify_baseline_contract,
    verify_inventory,
    verify_query_contract,
    verify_rfc_decisions,
)


class PublicSurfaceInventoryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.schema = load_json(REPOSITORY_ROOT / "release" / "public-surface" / "inventory.schema.json")
        cls.inventory = load_json(REPOSITORY_ROOT / "release" / "public-surface" / "inventory.json")
        cls.baseline_contract = load_json(REPOSITORY_ROOT / "release" / "0.7.0" / "public_contract.json")
        cls.query_contract = load_json(REPOSITORY_ROOT / "release" / "public-surface" / "query-contract.json")
        cls.rfc_directory = REPOSITORY_ROOT / "docs" / "rfcs"

    def require_rejected(self, mutation, fragment: str) -> None:
        candidate = copy.deepcopy(self.inventory)
        mutation(candidate)
        with self.assertRaisesRegex(InventoryError, fragment):
            verify_inventory(candidate, self.schema)

    def test_canonical_inventory_passes(self) -> None:
        verify_inventory(copy.deepcopy(self.inventory), self.schema)
        verify_baseline_contract(copy.deepcopy(self.inventory), copy.deepcopy(self.baseline_contract))
        verify_query_contract(copy.deepcopy(self.inventory), copy.deepcopy(self.query_contract))
        verify_rfc_decisions(copy.deepcopy(self.inventory), self.rfc_directory, frozenset({"RFC 0012"}))

    def test_baseline_contract_drift_fails_closed(self) -> None:
        contract = copy.deepcopy(self.baseline_contract)
        contract["function"]["named_parameters"].pop("secret")
        with self.assertRaisesRegex(InventoryError, "disagree on named parameters"):
            verify_baseline_contract(copy.deepcopy(self.inventory), contract)

    def test_release_view_omission_fails_closed(self) -> None:
        self.require_rejected(
            lambda value: value["release_views"][1]["active"].pop(),
            "active inventory omission/extra",
        )

    def test_release_view_extra_fails_closed(self) -> None:
        self.require_rejected(
            lambda value: value["release_views"][1]["active"].append(
                "sql.table_function.system.main.unowned_extra"
            ),
            "active inventory omission/extra",
        )

    def test_whole_release_view_omission_fails_closed(self) -> None:
        self.require_rejected(
            lambda value: value["release_views"].pop(1),
            "release-view omission/extra",
        )

    def test_unknown_classification_fails_schema(self) -> None:
        self.require_rejected(
            lambda value: value["entries"][0]["revisions"][1].__setitem__("classification", "maybe_safe"),
            "unknown value",
        )

    def test_deprecation_misclassified_as_compatible_fails(self) -> None:
        self.require_rejected(
            lambda value: value["entries"][0]["revisions"][1].__setitem__(
                "classification", "compatible_addition"
            ),
            "must be 'deprecation'",
        )

    def test_removal_misclassified_as_incompatible_fails(self) -> None:
        self.require_rejected(
            lambda value: value["entries"][0]["revisions"][2].__setitem__(
                "classification", "incompatible_change"
            ),
            "must be 'removal'",
        )

    def test_shape_mutation_requires_new_identity(self) -> None:
        self.require_rejected(
            lambda value: value["shapes"][0]["arguments"][0].__setitem__("required", False),
            "shape digest does not match canonical content",
        )

    def test_recomputed_shape_digest_still_requires_new_content_address(self) -> None:
        candidate = copy.deepcopy(self.inventory)
        shape = candidate["shapes"][0]
        shape["behaviors"].append("synthetic_mutation")
        payload = {name: value for name, value in shape.items() if name not in {"id", "digest"}}
        shape["digest"] = hashlib.sha256(
            json.dumps(payload, sort_keys=True, separators=(",", ":")).encode("utf-8")
        ).hexdigest()
        with self.assertRaisesRegex(InventoryError, "shape id is not its canonical content address"):
            verify_inventory(candidate, self.schema)

    def test_optional_argument_addition_is_compatible(self) -> None:
        candidate = copy.deepcopy(self.inventory)
        old_shape = candidate["shapes"][1]
        new_shape = copy.deepcopy(old_shape)
        new_shape["id"] = "github-login-search-v2"
        new_shape["arguments"].append(
            {
                "name": "optional_filter",
                "duckdb_type": "VARCHAR",
                "required": False,
                "nullable": True,
                "origin": "relation",
            }
        )
        payload = {name: value for name, value in new_shape.items() if name not in {"id", "digest"}}
        new_shape["digest"] = hashlib.sha256(
            json.dumps(payload, sort_keys=True, separators=(",", ":")).encode("utf-8")
        ).hexdigest()
        new_shape["id"] = f"sha256.{new_shape['digest']}"
        candidate["shapes"].append(new_shape)
        candidate["entries"][1]["revisions"].append(
            {
                "release": "0.9.0",
                "state": "active",
                "classification": "compatible_change",
                "shape": new_shape["id"],
                "rfc": "RFC 0012",
                "rationale": "Synthetic classifier fixture.",
            }
        )
        verify_inventory(candidate, self.schema)

    def test_unknown_shape_reference_fails(self) -> None:
        self.require_rejected(
            lambda value: value["entries"][1]["revisions"][0].__setitem__("shape", "missing-v1"),
            "references unknown shape",
        )

    def test_duplicate_sql_name_fails_case_insensitively(self) -> None:
        self.require_rejected(
            lambda value: value["entries"][1].__setitem__("name", "duckdb_api_scan"),
            "duplicate SQL function name",
        )

    def test_removed_surface_is_not_current(self) -> None:
        dispatcher = "sql.table_function.system.main.duckdb_api_scan"
        self.assertIn(dispatcher, self.inventory["release_views"][0]["active"])
        self.assertNotIn(dispatcher, self.inventory["release_views"][1]["active"])
        self.assertIn(dispatcher, self.inventory["release_views"][1]["removed"])

    def test_coordinated_entry_omission_fails_query_contract(self) -> None:
        candidate = copy.deepcopy(self.inventory)
        identity = "sql.table_function.system.main.duckdb_api_load_connector"
        entry = next(item for item in candidate["entries"] if item["id"] == identity)
        shape = entry["revisions"][0]["shape"]
        candidate["entries"].remove(entry)
        candidate["shapes"] = [item for item in candidate["shapes"] if item["id"] != shape]
        for view in candidate["release_views"]:
            if identity in view["active"]:
                view["active"].remove(identity)
        verify_inventory(candidate, self.schema)
        with self.assertRaisesRegex(InventoryError, "Query contract entry omission/extra"):
            verify_query_contract(candidate, self.query_contract)

    def test_coordinated_entry_extra_fails_query_contract(self) -> None:
        candidate = copy.deepcopy(self.inventory)
        source = next(
            item
            for item in candidate["entries"]
            if item["id"] == "sql.table_function.system.main.github_duckdb_login_search_page"
        )
        extra = copy.deepcopy(source)
        extra["id"] = "sql.table_function.system.main.unapproved_extra"
        extra["name"] = "unapproved_extra"
        candidate["entries"].append(extra)
        for view in candidate["release_views"]:
            if view["release"] == "0.9.0":
                view["active"].append(extra["id"])
        verify_inventory(candidate, self.schema)
        with self.assertRaisesRegex(InventoryError, "Query contract entry omission/extra"):
            verify_query_contract(candidate, self.query_contract)

    def test_in_review_rfc_requires_explicit_review_mode(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            directory = pathlib.Path(temporary)
            (directory / "0009-baseline.md").write_text(
                '```yaml\nrfc: "0009"\nstatus: "Accepted"\n```\n', encoding="utf-8"
            )
            (directory / "0012-candidate.md").write_text(
                '```yaml\nrfc: "0012"\nstatus: "In review"\n```\n', encoding="utf-8"
            )
            with self.assertRaisesRegex(InventoryError, "not Accepted"):
                verify_rfc_decisions(self.inventory, directory)
            verify_rfc_decisions(self.inventory, directory, frozenset({"RFC 0012"}))

    def test_unknown_rfc_reference_fails_closed(self) -> None:
        candidate = copy.deepcopy(self.inventory)
        candidate["entries"][0]["revisions"][0]["rfc"] = "RFC 9999"
        with self.assertRaisesRegex(InventoryError, "does not resolve"):
            verify_rfc_decisions(candidate, self.rfc_directory, frozenset({"RFC 0012"}))


if __name__ == "__main__":
    unittest.main()

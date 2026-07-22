#!/usr/bin/env python3
"""Mutation tests for the fail-closed 1.0.0 contract freeze gate."""

from __future__ import annotations

import copy
import pathlib
import sys
import unittest

REPOSITORY_ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPOSITORY_ROOT / "scripts"))

from contract_freeze import FreezeError, verify_freeze  # noqa: E402
from public_surface.inventory import load_json  # noqa: E402


class ContractFreezeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.freeze = load_json(REPOSITORY_ROOT / "release" / "1.0.0" / "freeze.json")
        cls.inventory = load_json(REPOSITORY_ROOT / "release" / "public-surface" / "inventory.json")
        cls.schema = load_json(
            REPOSITORY_ROOT / "src" / "connector" / "package" / "assets" / "connector-package-v1.schema.json"
        )
        cls.rfc_directory = REPOSITORY_ROOT / "docs" / "rfcs"

    def verify(self, freeze, schema=None) -> None:
        verify_freeze(
            freeze,
            inventory=self.inventory,
            schema=self.schema if schema is None else schema,
            rfc_directory=self.rfc_directory,
        )

    def require_rejected(self, mutation, fragment: str) -> None:
        candidate = copy.deepcopy(self.freeze)
        mutation(candidate)
        with self.assertRaisesRegex(FreezeError, fragment):
            self.verify(candidate)

    def test_canonical_freeze_passes(self) -> None:
        self.verify(copy.deepcopy(self.freeze))

    def test_freeze_version_drift_fails(self) -> None:
        self.require_rejected(lambda value: value.__setitem__("freeze_version", "2.0.0"), "freeze_version")

    def test_connector_spec_identifier_drift_fails(self) -> None:
        self.require_rejected(
            lambda value: value["connector_spec"].__setitem__("identifier", "duckdb_api/v2"),
            "connector-spec identifier",
        )

    def test_declared_schema_authority_drift_fails(self) -> None:
        self.require_rejected(
            lambda value: value["connector_spec"].__setitem__(
                "schema_authority", "src/connector/package/assets/old.schema.json"
            ),
            "schema_authority",
        )

    def test_pagination_schema_authority_drift_fails(self) -> None:
        self.require_rejected(
            lambda value: value["pagination_strategies"].__setitem__(
                "schema_authority", "src/connector/package/assets/old.schema.json"
            ),
            "schema_authority",
        )

    def test_sql_active_surface_extra_fails(self) -> None:
        self.require_rejected(
            lambda value: value["sql_surface"]["active"].append(
                "sql.table_function.system.main.unfrozen_extra"
            ),
            "active surface disagrees",
        )

    def test_sql_active_surface_omission_fails(self) -> None:
        self.require_rejected(lambda value: value["sql_surface"]["active"].pop(), "active surface disagrees")

    def test_sql_removed_surface_drift_fails(self) -> None:
        self.require_rejected(lambda value: value["sql_surface"]["removed"].clear(), "removed surface disagrees")

    def test_rest_pagination_strategy_widening_fails(self) -> None:
        self.require_rejected(
            lambda value: value["pagination_strategies"]["rest"].append("body_url_next"),
            "REST pagination strategies disagree",
        )

    def test_graphql_pagination_strategy_widening_fails(self) -> None:
        self.require_rejected(
            lambda value: value["pagination_strategies"]["graphql"].append("offset"),
            "GraphQL pagination strategies disagree",
        )

    def test_rejected_diagnostic_drift_fails(self) -> None:
        self.require_rejected(
            lambda value: value["pagination_strategies"]["rejected_diagnostic"].__setitem__(
                "code", "DUCKDB_API_INVALID_TYPE"
            ),
            "rejected_diagnostic",
        )

    def test_version_domain_omission_fails(self) -> None:
        self.require_rejected(lambda value: value["version_domains"].pop(), "version domains")

    def test_version_domain_extra_fails(self) -> None:
        self.require_rejected(
            lambda value: value["version_domains"].append(
                {"domain": "internal_abi", "contract": "fabricated", "authority": "none"}
            ),
            "version domains",
        )

    def test_mandatory_exclusion_removed_fails(self) -> None:
        self.require_rejected(
            lambda value: value["exclusions"].remove(
                "pagination_body_url_offset_or_cursor_in_body_strategies"
            ),
            "mandatory exclusions",
        )

    def test_custom_protocol_abi_exclusion_removed_fails(self) -> None:
        self.require_rejected(
            lambda value: value["exclusions"].remove("custom_protocol_or_pagination_binary_abis"),
            "mandatory exclusions",
        )

    def test_fast_follow_removed_fails(self) -> None:
        self.require_rejected(lambda value: value["fast_follows"].clear(), "fixture execution fast-follow")

    def test_accepted_candidate_revisions_section_removed_fails(self) -> None:
        self.require_rejected(
            lambda value: value.pop("accepted_candidate_revisions"),
            "accepted_candidate_revisions",
        )

    def test_accepted_candidate_revisions_empty_list_passes(self) -> None:
        # `response_next` graduated into pagination_strategies.rest when its
        # 0.10.0 implementation landed; accepted_candidate_revisions is empty
        # today and the freeze must accept that. The framework remains in
        # place for any future accepted RFC that introduces a new candidate.
        candidate = copy.deepcopy(self.freeze)
        candidate["accepted_candidate_revisions"] = []
        self.verify(candidate)

    def test_accepted_candidate_revisions_synthetic_entry_passes(self) -> None:
        # Proves the structural verifier still accepts a well-formed entry
        # pointing at an Accepted RFC, even though no real candidate exists
        # today. Mutations of this synthetic entry are the structural checks.
        candidate = copy.deepcopy(self.freeze)
        candidate["accepted_candidate_revisions"] = [
            {
                "id": "synthetic_future_candidate",
                "scope": "Test-only synthetic candidate to exercise the verifier.",
                "status": "accepted_by_rfc_pending_implementation",
                "authority": "RFC 0016",
                "target_release": "0.99.0",
                "target_release_authority": "test-only",
                "not_yet_in_schema_closed_set": True,
                "schema_closed_set_today": ["disabled", "link_next", "response_next", "short_page"],
                "graduation_rule": "Test-only synthetic entry; graduation is not real.",
                "broader_category_remains_excluded": "pagination_body_url_offset_or_cursor_in_body_strategies",
                "broader_category_exclusion_reason": "Test-only synthetic entry.",
            }
        ]
        self.verify(candidate)

    def test_accepted_candidate_revisions_synthetic_entry_drift_fails(self) -> None:
        candidate = copy.deepcopy(self.freeze)
        candidate["accepted_candidate_revisions"] = [
            {
                "id": "synthetic_future_candidate",
                "scope": "Test-only synthetic candidate to exercise the verifier.",
                "status": "accepted_by_rfc_pending_implementation",
                "authority": "RFC 0016",
                "target_release": "0.99.0",
                "target_release_authority": "test-only",
                "not_yet_in_schema_closed_set": True,
                "schema_closed_set_today": ["disabled"],  # drift: missing link_next, response_next, short_page
                "graduation_rule": "Test-only synthetic entry; graduation is not real.",
                "broader_category_remains_excluded": "pagination_body_url_offset_or_cursor_in_body_strategies",
                "broader_category_exclusion_reason": "Test-only synthetic entry.",
            }
        ]
        with self.assertRaisesRegex(FreezeError, "schema_closed_set_today"):
            self.verify(candidate)

    def test_not_yet_frozen_spurious_fails(self) -> None:
        self.require_rejected(
            lambda value: value["not_yet_frozen"].append(
                {"item": "fabricated_deferral", "reason": "x", "authority": "x"}
            ),
            "not_yet_frozen",
        )

    def test_not_yet_frozen_missing_fails(self) -> None:
        self.require_rejected(lambda value: value["not_yet_frozen"].pop(), "not_yet_frozen")

    def test_matrix_marked_frozen_prematurely_fails(self) -> None:
        candidate = copy.deepcopy(self.freeze)
        candidate["not_yet_frozen"] = [
            entry for entry in candidate["not_yet_frozen"] if "matrix_rows" not in entry["item"]
        ]
        with self.assertRaisesRegex(FreezeError, "not_yet_frozen"):
            self.verify(candidate)

    def test_rfc_authority_set_omission_fails(self) -> None:
        self.require_rejected(
            lambda value: value["rfc_authorities"]["accepted"].pop(),
            "rfc_authorities.accepted",
        )

    def test_rfc_authority_set_extra_fails(self) -> None:
        self.require_rejected(
            lambda value: value["rfc_authorities"]["accepted"].append("RFC 9999"),
            "rfc_authorities.accepted",
        )

    def test_rfc_authority_not_accepted_status_fails(self) -> None:
        import shutil
        import tempfile

        with tempfile.TemporaryDirectory() as temporary:
            mirror = pathlib.Path(temporary) / "rfcs"
            shutil.copytree(self.rfc_directory, mirror)
            target = mirror / "0010-prove-conservative-relational-composition.md"
            text = target.read_text(encoding="utf-8").replace(
                'status: "Accepted"', 'status: "Withdrawn"', 1
            )
            target.write_text(text, encoding="utf-8")
            with self.assertRaisesRegex(FreezeError, "not Accepted"):
                verify_freeze(
                    copy.deepcopy(self.freeze),
                    inventory=self.inventory,
                    schema=self.schema,
                    rfc_directory=mirror,
                )

    def test_schema_third_rest_pagination_strategy_fails(self) -> None:
        schema = copy.deepcopy(self.schema)
        schema["$defs"]["bodyUrlPagination"] = {
            "type": "object",
            "additionalProperties": False,
            "required": ["strategy"],
            "properties": {"strategy": {"const": "body_url_next"}},
        }
        schema["$defs"]["restOperation"]["properties"]["pagination"]["oneOf"].append(
            {"$ref": "#/$defs/bodyUrlPagination"}
        )
        with self.assertRaisesRegex(FreezeError, "REST pagination strategies disagree"):
            self.verify(copy.deepcopy(self.freeze), schema=schema)

    def test_schema_rest_closed_set_is_read_from_oneof(self) -> None:
        one_of = self.schema["$defs"]["restOperation"]["properties"]["pagination"]["oneOf"]
        rest = {
            self.schema["$defs"][option["$ref"].rsplit("/", 1)[-1]]["properties"]["strategy"]["const"]
            for option in one_of
        }
        self.assertEqual(rest, {"disabled", "link_next", "response_next", "short_page"})

    def test_schema_graphql_closed_set_is_read_from_reference(self) -> None:
        ref = self.schema["$defs"]["graphqlRequest"]["properties"]["pagination"]["$ref"]
        name = ref.rsplit("/", 1)[-1]
        self.assertEqual(self.schema["$defs"][name]["properties"]["strategy"]["const"], "relay_forward")


if __name__ == "__main__":
    unittest.main()

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
            REPOSITORY_ROOT / "src" / "connector" / "package" / "assets" / "connector-package-v2.schema.json"
        )
        cls.v1_schema = load_json(
            REPOSITORY_ROOT / "src" / "connector" / "package" / "assets" / "connector-package-v1.schema.json"
        )
        cls.rfc_directory = REPOSITORY_ROOT / "docs" / "rfcs"

    def verify(self, freeze, schema=None, v1_schema=None) -> None:
        verify_freeze(
            freeze,
            inventory=self.inventory,
            schema=self.schema if schema is None else schema,
            rfc_directory=self.rfc_directory,
            repository_root=REPOSITORY_ROOT,
            v1_schema=self.v1_schema if v1_schema is None else v1_schema,
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
            lambda value: value["connector_spec"].__setitem__("latest_identifier", "duckdb_api/v1"),
            "connector-spec contract",
        )

    def test_v1_graphql_retry_widening_fails(self) -> None:
        v1_schema = copy.deepcopy(self.v1_schema)
        v1_schema["$defs"]["graphqlOperation"]["properties"]["retry"] = {
            "$ref": "#/$defs/retryRecommendation"
        }
        with self.assertRaisesRegex(FreezeError, "v1 connector GraphQL schema unexpectedly enables retry"):
            self.verify(copy.deepcopy(self.freeze), v1_schema=v1_schema)

    def test_declared_schema_authority_drift_fails(self) -> None:
        self.require_rejected(
            lambda value: value["connector_spec"]["schema_authorities"].__setitem__(
                "duckdb_api/v2", "src/connector/package/assets/old.schema.json"
            ),
            "connector-spec contract",
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

    def test_scalar_type_schema_authority_drift_fails(self) -> None:
        self.require_rejected(
            lambda value: value["scalar_types"].__setitem__(
                "schema_authority", "src/connector/package/assets/old.schema.json"
            ),
            "schema_authority",
        )

    def test_scalar_type_widening_fails(self) -> None:
        self.require_rejected(
            lambda value: value["scalar_types"]["authored"].append("DECIMAL"),
            "scalar types disagree",
        )

    def test_scalar_type_omission_fails(self) -> None:
        self.require_rejected(
            lambda value: value["scalar_types"]["authored"].pop(),
            "scalar types disagree",
        )

    def test_scalar_type_rejected_diagnostic_drift_fails(self) -> None:
        self.require_rejected(
            lambda value: value["scalar_types"]["rejected_diagnostic"].__setitem__(
                "code", "DUCKDB_API_UNSUPPORTED_DECLARATION"
            ),
            "scalar_types rejected_diagnostic",
        )

    def test_failure_taxonomy_section_removed_fails(self) -> None:
        self.require_rejected(lambda value: value.pop("failure_taxonomy"), "failure_taxonomy")

    def test_failure_taxonomy_authority_omission_fails(self) -> None:
        self.require_rejected(
            lambda value: value["failure_taxonomy"].pop("authority"),
            "failure_taxonomy authority",
        )

    def test_failure_taxonomy_fabricated_authority_fails(self) -> None:
        self.require_rejected(
            lambda value: value["failure_taxonomy"].__setitem__("authority", "RFC 9999"),
            "failure_taxonomy authority",
        )

    def test_failure_primary_class_removed_fails(self) -> None:
        self.require_rejected(
            lambda value: value["failure_taxonomy"]["primary_classes"].remove("rate_limit"),
            "primary classes disagree",
        )

    def test_failure_primary_class_renamed_fails(self) -> None:
        classes = self.freeze["failure_taxonomy"]["primary_classes"]
        self.require_rejected(
            lambda value: value["failure_taxonomy"]["primary_classes"].__setitem__(
                classes.index("resource_budget"), "resource_limit"
            ),
            "primary classes disagree",
        )

    def test_failure_primary_class_widening_fails(self) -> None:
        self.require_rejected(
            lambda value: value["failure_taxonomy"]["primary_classes"].append("network_error"),
            "primary classes disagree",
        )

    def test_replay_classification_removed_fails(self) -> None:
        self.require_rejected(
            lambda value: value["failure_taxonomy"]["replay_classifications"].remove("indeterminate"),
            "replay classifications disagree",
        )

    def test_replay_classification_widening_fails(self) -> None:
        self.require_rejected(
            lambda value: value["failure_taxonomy"]["replay_classifications"].append("always_replay"),
            "replay classifications disagree",
        )

    def test_indeterminate_non_replayable_invariant_removed_fails(self) -> None:
        self.require_rejected(
            lambda value: value["failure_taxonomy"].__setitem__("indeterminate_replay_is_non_replayable", False),
            "indeterminate replay is non-replayable",
        )

    def test_bounded_retry_attempt_ceiling_drift_fails(self) -> None:
        self.require_rejected(
            lambda value: value["bounded_retry"].__setitem__("max_attempts_per_step", 4),
            "bounded-retry contract",
        )

    def test_bounded_retry_partial_response_widening_fails(self) -> None:
        self.require_rejected(
            lambda value: value["bounded_retry"].__setitem__("partial_response", "retryable"),
            "bounded-retry contract",
        )

    def test_column_shape_omission_fails(self) -> None:
        self.require_rejected(
            lambda value: value["column_shapes"]["authored"].remove("ARRAY"),
            "column shapes disagree",
        )

    def test_array_element_type_widening_fails(self) -> None:
        self.require_rejected(
            lambda value: value["column_shapes"]["array_element_types"].append("DECIMAL"),
            "array element types disagree",
        )

    def test_array_scope_drift_fails(self) -> None:
        self.require_rejected(
            lambda value: value["column_shapes"].__setitem__("array_nesting", "recursive"),
            "ARRAY scope",
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

    def test_accepted_contract_revisions_section_removed_fails(self) -> None:
        self.require_rejected(
            lambda value: value.pop("accepted_contract_revisions"),
            "accepted_contract_revisions",
        )

    def test_graduated_contract_revision_cannot_return_pending(self) -> None:
        self.require_rejected(
            lambda value: value["accepted_contract_revisions"].append({"id": "durable_credential_providers"}),
            "missing required keys",
        )

    def test_credential_provider_closed_set_removed_fails(self) -> None:
        self.require_rejected(
            lambda value: value.pop("credential_providers"),
            "credential-provider closed set",
        )

    def test_credential_provider_provider_drift_fails(self) -> None:
        self.require_rejected(
            lambda value: value["credential_providers"]["providers"].remove("environment"),
            "credential-provider closed set",
        )

    def test_credential_provider_storage_drift_fails(self) -> None:
        self.require_rejected(
            lambda value: value["credential_providers"]["config_storages"].remove("duckdb_api"),
            "credential-provider closed set",
        )

    def test_credential_provider_retained_exclusion_loss_fails(self) -> None:
        self.require_rejected(
            lambda value: value["exclusions"].remove(
                "authenticators_beyond_anonymous_bearer_and_static_api_key"
            ),
            "mandatory exclusions",
        )

    def test_credential_provider_authority_drift_fails(self) -> None:
        self.require_rejected(
            lambda value: value["credential_providers"].__setitem__("authority", "conversation memory"),
            "credential-provider closed set",
        )

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
                    repository_root=REPOSITORY_ROOT,
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

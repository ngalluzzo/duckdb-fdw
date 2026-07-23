"""Cross-source verification for the frozen 1.0.0 public contract.

The freeze declaration at ``release/1.0.0/freeze.json`` is a machine-checkable
summary of the complete 1.0.0 public contract. It is not itself primary
authority: every claim it makes must agree with the authoritative source that
defines the behavior. This module performs that cross-check so coordinated
drift between the freeze and the authoritative sources fails closed.
"""

from __future__ import annotations

import pathlib
import re
from typing import Any

from public_surface.inventory import load_json


class FreezeError(Exception):
    """Raised when the freeze declaration disagrees with an authoritative source."""


EXPECTED_FREEZE_VERSION = "1.0.0-candidate"

EXPECTED_SCHEMA_AUTHORITY = "src/connector/package/assets/connector-package-v1.schema.json"

EXPECTED_REJECTED_DIAGNOSTIC = {
    "code": "DUCKDB_API_UNSUPPORTED_DECLARATION",
    "phase": "SCHEMA",
}

EXPECTED_SCALAR_TYPE_REJECTED_DIAGNOSTIC = {
    "code": "DUCKDB_API_INVALID_TYPE",
    "phase": "SCHEMA",
}

EXPECTED_VERSION_DOMAINS = (
    "project_extension",
    "connector_spec",
    "connector_package",
    "duckdb_compatibility",
)

EXPECTED_NOT_YET_FROZEN = frozenset(
    {
        "supported_duckdb_profile_platform_architecture_installation_matrix_rows",
        "release/1.0.0/pins.json and public_contract.json",
    }
)

EXPECTED_RFC_AUTHORITIES = frozenset(
    {
        "RFC 0009",
        "RFC 0010",
        "RFC 0011",
        "RFC 0012",
        "RFC 0013",
        "RFC 0014",
        "RFC 0021",
        "RFC 0022",
        "RFC 0023",
    }
)

# Required structure for each entry in the freeze's
# accepted_candidate_revisions section. Each entry records a contract change
# an accepted RFC has authorized for a named target release, but whose
# implementation has not yet graduated the change into the schema-closed set.
# The section is conceptually distinct from exclusions (permanent),
# fast_follows (discovered gaps), and not_yet_frozen (evidence-derived).
REQUIRED_CANDIDATE_REVISION_KEYS = (
    "id",
    "scope",
    "status",
    "authority",
    "target_release",
    "target_release_authority",
    "not_yet_in_schema_closed_set",
    "schema_closed_set_today",
    "graduation_rule",
    "broader_category_remains_excluded",
    "broader_category_exclusion_reason",
)

# Candidate revisions accepted by RFC after the 0.9.0 freeze that must be
# present in every candidate snapshot until they graduate into the schema-
# closed set proper. Removing an entry before its graduation must fail closed.
# Empty today: the `response_next` candidate (RFC 0016) graduated into the
# schema-closed set when its 0.10.0 implementation landed; subsequent
# accepted RFCs that introduce new candidate revisions should add their ids
# here and to release/1.0.0/freeze.json's accepted_candidate_revisions list.
EXPECTED_CANDIDATE_REVISION_IDS = frozenset()

# Generic accepted decisions whose implementation has not yet graduated into a
# live closed-set section. Pagination predates this representation and retains
# its schema-specific accepted_candidate_revisions gate above. New non-schema
# decisions use this form so acceptance cannot leave the candidate freeze green
# while the decided future is absent.
REQUIRED_CONTRACT_REVISION_KEYS = frozenset(
    {
        "id",
        "kind",
        "scope",
        "status",
        "authority",
        "target_release",
        "target_release_authority",
        "current_contract_authority",
        "current_contract",
        "target_contract",
        "graduation_rule",
        "retained_exclusions",
        "not_yet_current",
    }
)

EXPECTED_CONTRACT_REVISION_IDS = frozenset()

EXPECTED_CREDENTIAL_PROVIDER_CONTRACT = {
    "providers": ["config", "environment"],
    "config_storages": ["duckdb_api", "memory"],
    "environment_storages": ["duckdb_api", "memory"],
    "environment_resolution": "execution_time_exact_variable",
    "persistent_storage": "bounded_project_duckdb_api",
    "scan_snapshot_identity": "opaque_authority_and_revision",
    "authority": "RFC 0023; docs/ARCHITECTURE.md Execution and authorization; "
    "docs/RUNTIME_CONTRACTS.md Credential provider and authorization snapshot",
}

MANDATORY_EXCLUSIONS = frozenset(
    {
        "public_rust_native_plugin_wasm_or_columnar_binary_abi",
        "custom_protocol_or_pagination_binary_abis",
        "internal_types_and_traits_public",
        "central_connector_discovery_or_distribution_registry",
        "connector_package_signing_or_trust_infrastructure",
        "authenticators_beyond_anonymous_bearer_and_static_api_key",
        "automatic_retry_or_rate_limit_waiting",
        "author_configurable_cache_or_single_flight",
        "dynamic_schemas",
        "write_back_transactions_or_continuous_streams",
        "pagination_body_url_offset_or_cursor_in_body_strategies",
        "raw_graphql_documents",
    }
)

# Closed resilience vocabulary established by RFC 0021. Layered additively on
# the existing ErrorStage classification; existing rendered diagnostic strings
# are preserved verbatim. No retry/rate-limit-waiting/cache mechanism is
# enabled. The freeze binds these sets so silent removal or rename fails closed;
# the C++ enum authority (src/include/duckdb_api/execution.hpp) must match.
EXPECTED_FAILURE_PRIMARY_CLASSES = frozenset(
    {
        "configuration",
        "authorization",
        "credential_provider",
        "destination_policy",
        "transport",
        "timeout",
        "remote_status",
        "rate_limit",
        "protocol",
        "decode",
        "schema",
        "resource_budget",
        "cancellation",
        "internal",
    }
)

EXPECTED_FAILURE_TAXONOMY_AUTHORITY = "RFC 0021; docs/RUNTIME_CONTRACTS.md Error ownership and redaction"

EXPECTED_REPLAY_CLASSIFICATIONS = frozenset(
    {
        "never_replayable",
        "replayable_before_exposure",
        "atomic_traversal_step",
        "server_directed_delay",
        "indeterminate",
    }
)


def _rfc_status(reference: str, rfc_directory: pathlib.Path) -> str:
    match = re.fullmatch(r"RFC ([0-9]{4})", reference)
    if match is None:
        raise FreezeError(f"invalid RFC reference: {reference}")
    candidates = sorted(rfc_directory.glob(f"{match.group(1)}-*.md"))
    if len(candidates) != 1:
        raise FreezeError(f"{reference} does not resolve to exactly one repository decision")
    text = candidates[0].read_text(encoding="utf-8")
    declared_rfc = re.search(r'^rfc: "([0-9]{4})"$', text, flags=re.MULTILINE)
    declared_status = re.search(r'^status: "([^"]+)"$', text, flags=re.MULTILINE)
    if declared_rfc is None or declared_rfc.group(1) != match.group(1) or declared_status is None:
        raise FreezeError(f"{reference} has invalid decision metadata")
    return declared_status.group(1)


def _def_name(ref: Any) -> str:
    if not isinstance(ref, str):
        raise FreezeError(f"schema $ref must be a string, got {type(ref).__name__}")
    return ref.rsplit("/", 1)[-1]


def _schema_value(schema: dict[str, Any], path: tuple[Any, ...]) -> Any:
    node: Any = schema
    for key in path:
        try:
            node = node[key]
        except (KeyError, TypeError, IndexError) as error:
            raise FreezeError(f"connector schema lost its closed pagination authority at {path}: {error}") from error
    return node


def _schema_connector_identifier(schema: dict[str, Any]) -> str:
    return _schema_value(schema, ("$defs", "connector", "properties", "api_version", "const"))


def _schema_rest_pagination_strategies(schema: dict[str, Any]) -> set[str]:
    one_of = _schema_value(schema, ("$defs", "restOperation", "properties", "pagination", "oneOf"))
    if not isinstance(one_of, list) or not one_of:
        raise FreezeError("connector schema REST pagination oneOf is not a non-empty list")
    strategies: set[str] = set()
    for option in one_of:
        ref = _schema_value(option, ("$ref",))
        strategies.add(_schema_value(schema, ("$defs", _def_name(ref), "properties", "strategy", "const")))
    return strategies


def _schema_graphql_pagination_strategies(schema: dict[str, Any]) -> set[str]:
    ref = _schema_value(schema, ("$defs", "graphqlRequest", "properties", "pagination", "$ref"))
    return {_schema_value(schema, ("$defs", _def_name(ref), "properties", "strategy", "const"))}


def _schema_scalar_types(schema: dict[str, Any]) -> set[str]:
    # The column def is the authoritative closed set; package_relation_schema.py's
    # DecodeColumn/DecodeInput and package_predicate_schema.py's literal-type
    # check keep the input and predicate-literal defs in lockstep with it, so
    # cross-checking all three here would only restate the same authority.
    return set(_schema_value(schema, ("$defs", "scalarColumn", "properties", "type", "enum")))


def _schema_column_shapes(schema: dict[str, Any]) -> set[str]:
    options = _schema_value(schema, ("$defs", "column", "oneOf"))
    references = {_def_name(_schema_value(option, ("$ref",))) for option in options}
    if references != {"scalarColumn", "arrayColumn"}:
        raise FreezeError("connector schema column oneOf is not the closed scalar-or-array shape")
    return {"SCALAR", "ARRAY"}


def _schema_array_element_types(schema: dict[str, Any]) -> set[str]:
    return set(_schema_value(schema, ("$defs", "arrayColumn", "properties", "element_type", "enum")))


def _inventory_release_view(inventory: dict[str, Any], release: str) -> dict[str, Any]:
    for view in inventory["release_views"]:
        if view["release"] == release:
            return view
    raise FreezeError(f"inventory has no release view for {release}")


def _require_freeze_field(freeze: dict[str, Any], *path: Any) -> Any:
    node: Any = freeze
    for key in path:
        if not isinstance(node, dict) or key not in node:
            raise FreezeError(f"freeze lost required field {'/'.join(str(p) for p in path)}")
        node = node[key]
    return node


def _verify_accepted_candidate_revisions(
    freeze: dict[str, Any],
    *,
    schema: dict[str, Any],
    rfc_directory: pathlib.Path,
) -> None:
    """Assert the accepted_candidate_revisions section is structurally valid and
    consistent with the schema-closed set and the cited RFC authorities.

    The section records contract changes an accepted RFC has authorized for a
    named target release but whose implementation has not yet graduated the
    change into the schema-closed set. Removing an entry before graduation,
    widening the schema-closed set without a corresponding graduation, or
    citing an RFC that is not Accepted all fail closed.
    """

    revisions = freeze.get("accepted_candidate_revisions")
    if not isinstance(revisions, list):
        raise FreezeError("freeze is missing the accepted_candidate_revisions list")
    if not revisions:
        # An empty list is permitted only when every expected candidate has
        # graduated. The expected-IDs set is non-empty today, so an empty list
        # is a structural error until the response_next entry graduates.
        if EXPECTED_CANDIDATE_REVISION_IDS:
            raise FreezeError(
                "freeze accepted_candidate_revisions is empty but expected entries have not graduated"
            )
        return

    revision_ids: set[str] = set()
    schema_rest_set = _schema_rest_pagination_strategies(schema)
    for entry in revisions:
        if not isinstance(entry, dict):
            raise FreezeError("accepted_candidate_revisions entry is not a mapping")
        missing = [key for key in REQUIRED_CANDIDATE_REVISION_KEYS if key not in entry]
        if missing:
            raise FreezeError(
                f"accepted_candidate_revisions entry {entry.get('id', '<unknown>')!r} "
                f"missing required keys: {missing}"
            )
        revision_id = entry["id"]
        if revision_id in revision_ids:
            raise FreezeError(f"accepted_candidate_revisions has duplicate id {revision_id!r}")
        revision_ids.add(revision_id)

        # The cited authority must resolve to an Accepted RFC.
        authority = entry["authority"]
        rfc_match = re.search(r"RFC ([0-9]{4})", authority)
        if rfc_match is None:
            raise FreezeError(
                f"accepted_candidate_revisions entry {revision_id!r} authority {authority!r} "
                f"does not cite an RFC reference"
            )
        status = _rfc_status(rfc_match.group(0), rfc_directory)
        if status != "Accepted":
            raise FreezeError(
                f"accepted_candidate_revisions entry {revision_id!r} cites {rfc_match.group(0)} "
                f"which is {status!r}, not Accepted"
            )

        # Until the candidate graduates (not_yet_in_schema_closed_set == false),
        # the entry's declared schema_closed_set_today must equal the actual
        # schema's REST closed set. This is the invariant that distinguishes
        # "decided future" (this section) from "current contract" (the closed
        # set itself): if the schema has already been widened, the entry must
        # graduate rather than linger here.
        if entry["not_yet_in_schema_closed_set"]:
            declared_today = set(entry["schema_closed_set_today"])
            if declared_today != schema_rest_set:
                raise FreezeError(
                    f"accepted_candidate_revisions entry {revision_id!r} "
                    f"schema_closed_set_today {sorted(declared_today)} disagrees with the "
                    f"schema's actual REST closed set {sorted(schema_rest_set)}"
                )

        # The broader exclusion category the entry carves out from must
        # remain a mandatory exclusion of the 1.0.0 boundary until the entry
        # graduates. This prevents silent removal of an exclusion that still
        # covers other shapes (e.g. offset/cursor-in-body) outside the
        # candidate revision's scope.
        broader_category = entry["broader_category_remains_excluded"]
        if broader_category not in MANDATORY_EXCLUSIONS:
            raise FreezeError(
                f"accepted_candidate_revisions entry {revision_id!r} "
                f"broader_category_remains_excluded {broader_category!r} is not a recognized "
                f"mandatory exclusion"
            )
        if broader_category not in set(freeze.get("exclusions", [])):
            raise FreezeError(
                f"accepted_candidate_revisions entry {revision_id!r} "
                f"requires broader exclusion {broader_category!r} to remain in the exclusions list"
            )

    missing_expected = EXPECTED_CANDIDATE_REVISION_IDS - revision_ids
    if missing_expected:
        raise FreezeError(
            f"freeze lost expected accepted_candidate_revisions entries: {sorted(missing_expected)}"
        )


def _verify_credential_provider_contract(freeze: dict[str, Any]) -> None:
    if freeze.get("credential_providers") != EXPECTED_CREDENTIAL_PROVIDER_CONTRACT:
        raise FreezeError(
            "freeze credential-provider closed set disagrees with the graduated RFC 0023 contract"
        )


def _verify_accepted_contract_revisions(
    freeze: dict[str, Any], *, rfc_directory: pathlib.Path
) -> None:
    revisions = freeze.get("accepted_contract_revisions")
    if not isinstance(revisions, list):
        raise FreezeError("freeze is missing the accepted_contract_revisions list")

    revision_ids: set[str] = set()
    for entry in revisions:
        if not isinstance(entry, dict):
            raise FreezeError("accepted_contract_revisions entry is not a mapping")
        missing = REQUIRED_CONTRACT_REVISION_KEYS - set(entry)
        if missing:
            raise FreezeError(
                f"accepted_contract_revisions entry {entry.get('id', '<unknown>')!r} "
                f"missing required keys: {sorted(missing)}"
            )
        revision_id = entry["id"]
        if revision_id in revision_ids:
            raise FreezeError(f"accepted_contract_revisions has duplicate id {revision_id!r}")
        revision_ids.add(revision_id)

        authority = entry["authority"]
        rfc_match = re.fullmatch(r"RFC ([0-9]{4})", authority)
        if rfc_match is None:
            raise FreezeError(
                f"accepted_contract_revisions entry {revision_id!r} has invalid authority {authority!r}"
            )
        status = _rfc_status(authority, rfc_directory)
        if status != "Accepted":
            raise FreezeError(
                f"accepted_contract_revisions entry {revision_id!r} cites {authority} "
                f"which is {status!r}, not Accepted"
            )
        if entry["status"] != "accepted_by_rfc_pending_implementation" or entry["not_yet_current"] is not True:
            raise FreezeError(
                f"accepted_contract_revisions entry {revision_id!r} was prematurely graduated"
            )
        retained = entry["retained_exclusions"]
        if not isinstance(retained, list) or not retained:
            raise FreezeError(
                f"accepted_contract_revisions entry {revision_id!r} has no retained exclusions"
            )
        missing_exclusions = set(retained) - set(freeze.get("exclusions", []))
        if missing_exclusions:
            raise FreezeError(
                f"accepted_contract_revisions entry {revision_id!r} lost retained exclusions: "
                f"{sorted(missing_exclusions)}"
            )

        raise FreezeError(
            f"accepted_contract_revisions entry {revision_id!r} has unknown kind {entry['kind']!r}"
        )

    missing_expected = EXPECTED_CONTRACT_REVISION_IDS - revision_ids
    if missing_expected:
        raise FreezeError(
            f"freeze lost expected accepted_contract_revisions entries: {sorted(missing_expected)}"
        )
    unexpected = revision_ids - EXPECTED_CONTRACT_REVISION_IDS
    if unexpected:
        raise FreezeError(
            f"freeze has unexpected accepted_contract_revisions entries: {sorted(unexpected)}"
        )


def verify_freeze(
    freeze: dict[str, Any],
    *,
    inventory: dict[str, Any],
    schema: dict[str, Any],
    rfc_directory: pathlib.Path,
    repository_root: pathlib.Path,
) -> None:
    """Assert the freeze declaration agrees with every authoritative source."""

    if freeze.get("freeze_version") != EXPECTED_FREEZE_VERSION:
        raise FreezeError(f"freeze_version must be {EXPECTED_FREEZE_VERSION!r}")
    if freeze.get("produced_by_release") != "0.9.0":
        raise FreezeError("freeze is not attributed to the 0.9.0 release")
    if freeze.get("status") not in {"candidate", "frozen"}:
        raise FreezeError("freeze status must be candidate or frozen")

    declared_schema_authority = {
        _require_freeze_field(freeze, "connector_spec", "schema_authority"),
        _require_freeze_field(freeze, "pagination_strategies", "schema_authority"),
        _require_freeze_field(freeze, "scalar_types", "schema_authority"),
        _require_freeze_field(freeze, "column_shapes", "schema_authority"),
    }
    if declared_schema_authority != {EXPECTED_SCHEMA_AUTHORITY}:
        raise FreezeError(
            f"freeze schema_authority paths {sorted(declared_schema_authority)} disagree with the canonical "
            f"{EXPECTED_SCHEMA_AUTHORITY!r} the gate verifies against"
        )

    identifier = _require_freeze_field(freeze, "connector_spec", "identifier")
    schema_identifier = _schema_connector_identifier(schema)
    if identifier != schema_identifier:
        raise FreezeError(
            f"freeze connector-spec identifier {identifier!r} disagrees with schema const {schema_identifier!r}"
        )

    view = _inventory_release_view(inventory, freeze["sql_surface"]["release_view"])
    active = set(freeze["sql_surface"]["active"])
    removed = set(freeze["sql_surface"]["removed"])
    if active != set(view["active"]):
        raise FreezeError("freeze SQL active surface disagrees with the inventory release view")
    if removed != set(view["removed"]):
        raise FreezeError("freeze SQL removed surface disagrees with the inventory release view")

    rest_strategies = set(freeze["pagination_strategies"]["rest"])
    graphql_strategies = set(freeze["pagination_strategies"]["graphql"])
    if rest_strategies != _schema_rest_pagination_strategies(schema):
        raise FreezeError("freeze REST pagination strategies disagree with the schema's closed oneOf set")
    if graphql_strategies != _schema_graphql_pagination_strategies(schema):
        raise FreezeError("freeze GraphQL pagination strategies disagree with the schema's closed reference")

    rejected = freeze["pagination_strategies"].get("rejected_diagnostic")
    if rejected != EXPECTED_REJECTED_DIAGNOSTIC:
        raise FreezeError(
            f"freeze rejected_diagnostic {rejected!r} disagrees with {EXPECTED_REJECTED_DIAGNOSTIC!r}"
        )

    scalar_types = set(freeze["scalar_types"]["authored"])
    if scalar_types != _schema_scalar_types(schema):
        raise FreezeError("freeze scalar types disagree with the schema's closed column-type enum")
    scalar_rejected = freeze["scalar_types"].get("rejected_diagnostic")
    if scalar_rejected != EXPECTED_SCALAR_TYPE_REJECTED_DIAGNOSTIC:
        raise FreezeError(
            f"freeze scalar_types rejected_diagnostic {scalar_rejected!r} disagrees with "
            f"{EXPECTED_SCALAR_TYPE_REJECTED_DIAGNOSTIC!r}"
        )

    failure_taxonomy = freeze.get("failure_taxonomy")
    if not isinstance(failure_taxonomy, dict):
        raise FreezeError("freeze is missing the failure_taxonomy closed-vocabulary section")
    if failure_taxonomy.get("authority") != EXPECTED_FAILURE_TAXONOMY_AUTHORITY:
        raise FreezeError("freeze failure_taxonomy authority does not cite the accepted RFC 0021 contract")
    primary_classes = set(failure_taxonomy.get("primary_classes", []))
    if primary_classes != EXPECTED_FAILURE_PRIMARY_CLASSES:
        raise FreezeError(
            f"freeze failure_taxonomy primary classes disagree: "
            f"freeze has {sorted(primary_classes)}, expected {sorted(EXPECTED_FAILURE_PRIMARY_CLASSES)}"
        )
    replay = set(failure_taxonomy.get("replay_classifications", []))
    if replay != EXPECTED_REPLAY_CLASSIFICATIONS:
        raise FreezeError(
            f"freeze failure_taxonomy replay classifications disagree: "
            f"freeze has {sorted(replay)}, expected {sorted(EXPECTED_REPLAY_CLASSIFICATIONS)}"
        )
    if failure_taxonomy.get("indeterminate_replay_is_non_replayable") is not True:
        raise FreezeError("freeze failure_taxonomy must assert indeterminate replay is non-replayable")

    column_shapes = freeze["column_shapes"]
    if set(column_shapes.get("authored", [])) != _schema_column_shapes(schema):
        raise FreezeError("freeze column shapes disagree with the schema's closed column oneOf set")
    if set(column_shapes.get("array_element_types", [])) != _schema_array_element_types(schema):
        raise FreezeError("freeze array element types disagree with the schema's closed element-type enum")
    if column_shapes.get("array_nesting") != "flat_only" or column_shapes.get("array_usage") != "output_columns_only":
        raise FreezeError("freeze ARRAY scope is not the accepted flat output-only contract")

    declared_domains = {domain["domain"] for domain in freeze.get("version_domains", [])}
    if declared_domains != set(EXPECTED_VERSION_DOMAINS):
        raise FreezeError(
            f"freeze version domains {sorted(declared_domains)} disagree with {list(EXPECTED_VERSION_DOMAINS)}"
        )

    exclusions = set(freeze.get("exclusions", []))
    missing = MANDATORY_EXCLUSIONS - exclusions
    if missing:
        raise FreezeError(f"freeze lost mandatory exclusions: {sorted(missing)}")

    fast_follow_ids = {follow["id"] for follow in freeze.get("fast_follows", [])}
    if "end_to_end_fixture_execution" not in fast_follow_ids:
        raise FreezeError("freeze does not record the end-to-end fixture execution fast-follow")

    _verify_accepted_candidate_revisions(freeze, schema=schema, rfc_directory=rfc_directory)
    _verify_credential_provider_contract(freeze)
    _verify_accepted_contract_revisions(freeze, rfc_directory=rfc_directory)

    not_yet_frozen = {entry["item"] for entry in freeze.get("not_yet_frozen", [])}
    if not_yet_frozen != EXPECTED_NOT_YET_FROZEN:
        raise FreezeError(
            f"freeze not_yet_frozen {sorted(not_yet_frozen)} disagrees with {sorted(EXPECTED_NOT_YET_FROZEN)}"
        )

    authorities = freeze.get("rfc_authorities", {})
    accepted = authorities.get("accepted", [])
    if set(accepted) != EXPECTED_RFC_AUTHORITIES:
        raise FreezeError(
            f"freeze rfc_authorities.accepted {sorted(set(accepted))} disagrees with "
            f"{sorted(EXPECTED_RFC_AUTHORITIES)}"
        )
    for reference in accepted:
        status = _rfc_status(reference, rfc_directory)
        if status != "Accepted":
            raise FreezeError(f"{reference} is {status!r}, not Accepted")


class FreezePaths:
    def __init__(self, repository: pathlib.Path) -> None:
        self.freeze = repository / "release" / "1.0.0" / "freeze.json"
        self.inventory = repository / "release" / "public-surface" / "inventory.json"
        self.schema = repository / EXPECTED_SCHEMA_AUTHORITY
        self.rfc_directory = repository / "docs" / "rfcs"
        self.repository = repository


def verify_paths(paths: FreezePaths) -> None:
    try:
        verify_freeze(
            load_json(paths.freeze),
            inventory=load_json(paths.inventory),
            schema=load_json(paths.schema),
            rfc_directory=paths.rfc_directory,
            repository_root=paths.repository,
        )
    except FreezeError:
        raise
    except (KeyError, TypeError, ValueError, AttributeError) as error:
        raise FreezeError(f"freeze structure is malformed: {error}") from error

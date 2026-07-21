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
    }
)

MANDATORY_EXCLUSIONS = frozenset(
    {
        "public_rust_native_plugin_wasm_or_columnar_binary_abi",
        "custom_protocol_or_pagination_binary_abis",
        "internal_types_and_traits_public",
        "central_connector_discovery_or_distribution_registry",
        "connector_package_signing_or_trust_infrastructure",
        "automatic_retry_or_rate_limit_waiting",
        "author_configurable_cache_or_single_flight",
        "dynamic_schemas",
        "write_back_transactions_or_continuous_streams",
        "pagination_body_url_offset_or_cursor_in_body_strategies",
        "raw_graphql_documents",
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


def verify_freeze(
    freeze: dict[str, Any],
    *,
    inventory: dict[str, Any],
    schema: dict[str, Any],
    rfc_directory: pathlib.Path,
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


def verify_paths(paths: FreezePaths) -> None:
    try:
        verify_freeze(
            load_json(paths.freeze),
            inventory=load_json(paths.inventory),
            schema=load_json(paths.schema),
            rfc_directory=paths.rfc_directory,
        )
    except FreezeError:
        raise
    except (KeyError, TypeError, ValueError, AttributeError) as error:
        raise FreezeError(f"freeze structure is malformed: {error}") from error

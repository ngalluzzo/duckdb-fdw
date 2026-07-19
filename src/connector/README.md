# Connector metadata

This package defines the immutable catalog that describes the extension's
installed connectors and relations. Change it when a relation, schema,
pagination declaration, resource ceiling, catalog validation rule, or safe
catalog explanation changes.

Connector construction is deterministic and network-free. The resulting
`CompiledConnector` contains metadata only: no credentials, active requests,
DuckDB callback state, or runtime objects. Conditional predicate declarations
are immutable source facts; Semantics owns their relational interpretation and
Runtime receives only the resulting plan.

## Start here

| Change | Production code | Test |
| --- | --- | --- |
| Add or change an installed GitHub relation | `native_github_composition.cpp`, `duckdb_api/connector.hpp` | `connector_contract_tests.cpp` |
| Change catalog values, validation, lookup, or snapshots | `catalog_model.cpp`, `duckdb_api/connector_catalog.hpp` | `connector_catalog_contract_tests.cpp` |
| Change a pagination declaration | `pagination_declaration.cpp` and its internal header | `connector_pagination_contract_tests.cpp` |
| Change a predicate declaration | `predicate_declaration.cpp` and its internal header | `connector_predicate_contract_tests.cpp`; `connector_contract_tests.cpp` for installed values |
| Change resource ceilings | `resource_ceiling_declaration.cpp` and its internal header | `connector_pagination_contract_tests.cpp`; `connector_contract_tests.cpp` for installed values |
| Add a deterministic catalog fixture | `test/cpp/connector/support/connector_catalog_test_fixtures.*` | `connector_catalog_test_fixtures_tests.cpp` |

Production sources are inventoried in `sources.cmake`; provider targets are in
`targets.cmake`. Tests and their targets mirror those files under
`test/cpp/connector/`.

The supported consumer interfaces are
[`connector.hpp`](../include/duckdb_api/connector.hpp) and
[`connector_catalog.hpp`](../include/duckdb_api/connector_catalog.hpp).
Headers under `duckdb_api/internal/connector/` are implementation details.
Tests in other packages that need non-production catalogs use the
Connector-owned fixture service. Connector's own contract tests may use
`test/cpp/connector/support/catalog_test_access.hpp` to exercise private model
invariants; that construction access must not become a consumer API.

## Tests

`make test` runs both focused Connector executables:

- `duckdb_api_connector_tests` for catalog, schema, predicate, pagination, and
  resource contracts;
- `duckdb_api_connector_catalog_fixture_tests` for the bounded fixture API.

Run `make build` before invoking a focused binary from
`<build_root>/extension/duckdb_api/`, where `build_root` is printed by
`make paths`. Run `make verify` before handoff on the supported product cell.

If a change affects connector-package syntax or author-visible validation,
start with [the connector specification](../../docs/CONNECTOR_SPECIFICATIONS.md).
If it changes a consumer interface or public behavior, follow the routing in
[CONTRIBUTING.md](../../CONTRIBUTING.md) and the
[RFC process](../../docs/RFC_PROCESS.md). Maintainer accountability is recorded
in the [Connector Experience charter](../../docs/teams/CONNECTOR_EXPERIENCE.md).

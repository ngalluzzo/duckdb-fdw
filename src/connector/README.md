# Connector metadata

This package defines the immutable catalog that describes the extension's
installed connectors and relations. Change it when a relation, schema,
pagination declaration, resource ceiling, catalog validation rule, or safe
catalog explanation changes.

Connector construction is deterministic and network-free. The resulting
`CompiledConnector` contains metadata only: no credentials, active requests,
DuckDB callback state, or runtime objects. Conditional predicate declarations
are immutable source facts. A closed proof identity binds accuracy to one base
occurrence domain, occurrence-preservation guarantee, and operation-scoped
encoding envelope; changing one field cannot relabel another profile. Semantics
owns implication, three-valued equivalence, composition, classification, and
residual ownership. Runtime receives only the resulting plan.

## Start here

| Change | Production code | Test |
| --- | --- | --- |
| Add or change an installed GitHub relation | `native_github_composition.cpp`, `duckdb_api/connector.hpp` | `connector_contract_tests.cpp` |
| Change catalog values, relation/catalog validation, or lookup | `catalog_model.cpp`, `duckdb_api/connector_catalog.hpp` | `connector_catalog_contract_tests.cpp` |
| Change the REST/GraphQL operation sum, HTTP authority, canonical GraphQL document, typed result mapping, cursor, or safe protocol snapshot | `protocol_operation_declaration.cpp`, `graphql_operation_declaration.cpp`, `duckdb_api/compiled_protocol_operation.hpp` | `connector_graphql_contract_tests.cpp`; existing REST contract tests |
| Change protocol-neutral content digests | `content_digest.cpp`, `duckdb_api/content_digest.hpp` | GraphQL digest-vector and canonical-profile tests |
| Change operation-selector normalization or declaration validation | `operation_selector.cpp` and its internal header | `connector_catalog_contract_tests.cpp`; fixture tests for controlled selection services |
| Change safe catalog snapshot rendering | `catalog_snapshot.cpp` | catalog and fixture snapshot assertions |
| Change a pagination declaration | `pagination_declaration.cpp` and its internal header | `connector_pagination_contract_tests.cpp` |
| Change a predicate declaration | `predicate_declaration.cpp` and its internal header | `connector_predicate_contract_tests.cpp`; `connector_contract_tests.cpp` for installed values |
| Change an accepted predicate proof/domain profile | `predicate_proof_profile.cpp` and its internal header | `connector_predicate_proof_contract_tests.cpp`; fixture tests for the controlled exact service |
| Change resource ceilings | `resource_ceiling_declaration.cpp` and its internal header | `connector_pagination_contract_tests.cpp`; `connector_contract_tests.cpp` for installed values |
| Add a deterministic catalog fixture | `test/cpp/connector/support/connector_catalog_test_fixtures.*` | `connector_catalog_test_fixtures_tests.cpp` |

Production sources are inventoried in `sources.cmake`; provider targets are in
`targets.cmake`. Tests and their targets mirror those files under
`test/cpp/connector/`.

The supported consumer interfaces are
[`connector.hpp`](../include/duckdb_api/connector.hpp) and
[`connector_catalog.hpp`](../include/duckdb_api/connector_catalog.hpp). The
cohesive operation-level handoff lives in
[`compiled_protocol_operation.hpp`](../include/duckdb_api/compiled_protocol_operation.hpp):
it owns protocol alternatives, neutral HTTP authority, REST requests, GraphQL
document/variable/result/cursor declarations, and guarded access. Catalog
composition includes that header and retains relation schema, authentication,
resource, and predicate responsibilities. `content_digest.hpp` is a separate
stateless service so Runtime may recompute bytes without linking or acquiring
Connector metadata authority.
Headers under `duckdb_api/internal/connector/` are implementation details.
Tests in other packages that need non-production catalogs use the
Connector-owned fixture service. Connector's own contract tests may use
`test/cpp/connector/support/catalog_test_access.hpp` to exercise private model
invariants; that construction access must not become a consumer API.

The fixture service's distinct exact predicate catalog is non-installable. It
passes the same production constructors and proof-profile validation as native
metadata, then exposes only public const catalog access. Consumers must not
infer proof or encoding from relation names, extractors, paths, fixed request
fields, or snapshots. The installed catalog retains only the reviewed GitHub
`SUPERSET` mapping; the controlled exact identity creates no public relation,
request, package, or ABI promise.

The same non-installable fixture service exposes a closed set of deliberately
invalid GraphQL catalog candidates for defensive consumer tests. Each candidate
starts as the production-validated canonical fixture; Connector-private test
access then changes only its named document, variable, response, cursor, body,
or schema fact. These values are not installable metadata, perform no I/O,
contain no secret or live request/response state, and must be rejected before
planning or execution. Consumer tests import only the public fixture header,
never `catalog_test_access.hpp`; production constructors continue to reject the
same drift.

## Tests

`make test` runs both focused Connector executables:

- `duckdb_api_connector_tests` for catalog, REST/GraphQL protocol, schema,
  predicate, pagination, digest, and resource contracts;
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

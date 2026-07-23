# DuckDB integration

This package is the DuckDB-facing edge of the extension. It translates a SQL
call into a protocol-neutral request, assembles the installed services, manages
DuckDB secret and scan state, publishes package-generated table functions,
pulls typed runtime batches, and writes `DataChunk` output.

Connector compilation, relational classification, and remote execution stay
behind their public interfaces; do not reproduce those rules in the adapter.

## Lifecycle at a glance

1. The extension entry point builds the installed composition and registers
   the secret type and `duckdb_api_scan` table function.
2. Bind validates constant arguments, retains a credential-free baseline
   `ScanRequest`, and asks the planner for one immutable baseline `ScanPlan`.
   Bind performs no network I/O.
3. Pinned DuckDB 1.5.4 logical optimization may offer structured filters. Query
   translates exact typed column/constant equality plus exposed `AND`, `OR`,
   and `NOT` structure into Semantics' bounded protocol-neutral candidate. It
   leaves every expression in DuckDB and never matches Connector mappings or
   chooses a remote input. Unsupported structure remains opaque at its offered
   position; over-depth or over-node input collapses completely rather than
   selecting a partial branch. DuckDB may substitute a prepared parameter with
   a typed constant before this callback, so each bind-data copy owns an
   independent selected request and plan.
4. Pre-execution explanation renders typed selected-request and plan facts:
   candidate, remote predicate and accuracy, retained filter scope, full
   projection closure, relational owners and delegation, adapter capability
   fallback, and Semantics' structured category and reason. Explanation is not
   parsed and grants no Runtime authority. It performs no secret lookup, I/O,
   request construction, or expression-text reconstruction.
5. Global initialization freezes the selected plan and opens one Runtime
   stream with a call-scoped credential provider. Runtime completes admission
   before the provider resolves the explicitly named temporary, environment,
   or persistent credential exactly once for that scan.
6. Each scan pull validates complete row arity, planned scalar kinds, batch
   bounds, and planned nullability before changing a `DataChunk`. Runtime nulls
   become typed vector NULLs; zero, `false`, and empty strings stay valid.
7. Interruption cancels the stream. Exhaustion marks it complete. Destruction
   cancels unfinished work and closes the stream without throwing.
8. The adapter translates provider failures at the DuckDB exception boundary;
   providers retain ownership of their structured, redacted errors.

Package publication follows a separate catalog lifecycle. Lead composition
implements `QueryPackageStagingService` by composing Connector compilation and
Runtime staging. Query receives one immutable registration/planning/execution
generation, validates catalog ownership and collisions, and changes all
generated, management, and introspection functions in one `system.main`
catalog transaction. Catalog, bind, prepared-plan, and scan state retain the
opaque generation owner; Query never parses package source or inspects a
Runtime registry.

## Start here

| Change | Production code | Focused evidence |
| --- | --- | --- |
| `ScanRequest` values or DuckDB capability reporting | `scan_request.cpp`, `duckdb_api/scan_request.hpp` | `duckdb_api_scan_request_tests` |
| Package staging consumer port and retained generation ownership | `query_generation.cpp`, `duckdb_api/query_generation.hpp` | `duckdb_api_package_query_surface_tests` |
| Atomic load/reload publication, ownership/collision checks, and close admission | `duckdb/catalog_generation_coordinator.*`, `duckdb/package_lifecycle_sentry.*` | management and lifecycle files in `duckdb_api_package_query_surface_tests` |
| Generated package function signatures and bind/planning handoff | `duckdb/generated_relation_adapter.*`, `duckdb/package_catalog_snapshot.*` | `generated_relation_tests.cpp` in `duckdb_api_package_query_surface_tests` |
| Package management and catalog introspection functions | `duckdb/package_management_functions.*`, `duckdb/package_introspection_functions.*` | management and introspection files in `duckdb_api_package_query_surface_tests` |
| Shared dispatcher/generated stream lifecycle | `duckdb/relation_execution.*` | `duckdb_api_adapter_stream_contract_tests`, `duckdb_api_package_query_surface_tests` |
| Pinned structured-expression translation | `duckdb/complex_filter_adapter.*` | `predicate_candidate_translation_tests.cpp` and `complex_filter_adapter_tests.cpp` in `duckdb_api_adapter_tests` |
| Typed selected-plan explanation | `duckdb/scan_plan_explanation.*` | `complex_filter_adapter_tests.cpp` in `duckdb_api_adapter_tests` |
| Planned scalar/nullability enforcement and DuckDB vector writes | `duckdb/typed_value_adapter.*` | `duckdb_api_typed_value_adapter_tests` |
| Baseline request retention and copied plan selection | `duckdb/table_function_plan_state.*` | `table_function_plan_state_tests.cpp` in `duckdb_api_adapter_tests` |
| Installed connector/runtime assembly | `product_composition.cpp`, `duckdb_api/product_composition.hpp` | `test/python/source_demo_contract.py` through `make test`; `make demo` for the live path |
| Table-function registration, callback composition, explain, bind/init/scan, cancellation, or batch transfer | `duckdb/table_function_adapter.cpp` | `duckdb_api_adapter_tests`, `duckdb_api_adapter_stream_contract_tests` |
| Credential provider/storage registration, validation, exact-name resolution, or persistent storage | `duckdb/secret_integration.cpp`, `duckdb/credential_provider_adapter.*`, `duckdb/credential_secret.*`, `duckdb/credential_storage.*`, `duckdb_api/duckdb_secret.hpp`; bounded target `duckdb_api_query_credential_service` | `duckdb_api_duckdb_secret_tests` |
| Extension identity, load order, or initialization containment | `duckdb/extension_entrypoint.cpp`, `duckdb_api_extension.hpp` | `test/sql/duckdb_api.test`, `test/python/source_demo_contract.py` |
| Controlled end-to-end composition | `test/cpp/query/integration/` | `test/python/live_rest_product_contract.py`, `test/python/authenticated_relation_product_contract.py`, `test/python/repository_pagination_product_contract.py` |
| GraphQL bind, explanation, nullable rows, SQL composition, and protocol errors through provider APIs | unchanged generic adapter plus `duckdb/scan_plan_explanation.*` | `duckdb_api_graphql_query_contract_tests` |
| Actual-DuckDB GraphQL result, prepare/repeat, and retained-REST composition through named Runtime scenarios | unchanged generic registration and adapter | `duckdb_api_graphql_product_contract_tests` |

Production and test inventories are in `src/query/{sources,targets}.cmake` and
`test/cpp/query/{sources,targets}.cmake`. Shared test helpers live under
`test/cpp/query/support/`; controlled product composition belongs under
`test/cpp/query/integration/`. Package catalog tests and their bounded consumer
doubles live under `test/cpp/query/packages/`.

## Product evidence boundary

`test/python/repository_pagination_product_contract.py` executes identical SQL
through the production-installed `Superset` mapping and a mapping-absent
forced-local baseline. Its fixture views share one duplicate-preserving bag,
and the matrix covers projection, `AND`, `OR`, `NOT`, total ordering, local
limit/offset, and `TRUE`/`FALSE`/`NULL` outcomes in actual DuckDB.

`Exact`, `Ambiguous`, and operation-selection-invalid planner outcomes are not
executable table-function profiles: the exact controlled operation is not
installed in Runtime, while the latter outcomes cannot authorize a selected
operation. Their production-planner and actual-DuckDB relational-law evidence
lives in `test/cpp/semantics/predicate_composition_law_tests.cpp`. Query tests
consume only the resulting public plan or error facts and verify explanation
and failure behavior without constructing provider internals.

## Tests

Run the ordinary developer loop from the repository root:

```sh
make build
make test
make demo
```

After `make build`, focused binaries are under
`<build_root>/extension/duckdb_api/`, where `build_root` is printed by
`make paths`. `make test` runs the Query targets plus SQL, controlled
service, artifact, and direct-load oracles. Run `make verify` before handoff on
the supported product cell.

Read [ARCHITECTURE.md](../../docs/ARCHITECTURE.md) for query semantics and
[RUNTIME_CONTRACTS.md](../../docs/RUNTIME_CONTRACTS.md) for state, cancellation,
error, and execution contracts. Shared-interface changes follow
[CONTRIBUTING.md](../../CONTRIBUTING.md) and the
[RFC process](../../docs/RFC_PROCESS.md). Maintainer accountability is recorded
in the [Query Experience charter](../../docs/teams/QUERY_EXPERIENCE.md).

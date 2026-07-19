# DuckDB integration

This package is the DuckDB-facing edge of the extension. It translates a SQL
call into a protocol-neutral request, assembles the installed services, manages
DuckDB secret and scan state, pulls typed runtime batches, and writes
`DataChunk` output.

Connector compilation, relational classification, and remote execution stay
behind their public interfaces; do not reproduce those rules in the adapter.

## Lifecycle at a glance

1. The extension entry point builds the installed composition and registers
   the secret type and `duckdb_api_scan` table function.
2. Bind validates constant arguments, retains a credential-free baseline
   `ScanRequest`, and asks the planner for one immutable baseline `ScanPlan`.
   Bind performs no network I/O.
3. Pinned DuckDB 1.5.4 logical optimization may offer structured filters. Query
   recognizes only `visibility = 'private'`, leaves the expression in DuckDB,
   and asks Semantics for a complete replacement plan from the retained
   baseline request. DuckDB may substitute a bound prepared-statement parameter
   with a typed constant before this callback; Query classifies that structured
   value per execution and does not reconstruct or retain parameter provenance.
   Unbound, `NULL`, and other values do not select the remote predicate. Each
   bind-data copy owns its selected plan independently, so executions cannot
   leak selection state into one another.
4. Pre-execution explanation renders only the selected plan's relation, remote
   predicate, accuracy, residual predicate scope, and residual owner. A larger
   retained filter is opaque and never mislabeled as the selective conjunct.
   Explanation performs no secret lookup, I/O, request construction, or
   expression-text parsing.
5. Global initialization freezes the selected plan, resolves an explicitly
   named temporary secret, and opens one Runtime stream.
6. Each scan pull validates a bounded typed batch before writing a `DataChunk`.
7. Interruption cancels the stream. Exhaustion marks it complete. Destruction
   cancels unfinished work and closes the stream without throwing.
8. The adapter translates provider failures at the DuckDB exception boundary;
   providers retain ownership of their structured, redacted errors.

## Start here

| Change | Production code | Focused evidence |
| --- | --- | --- |
| `ScanRequest` values or DuckDB capability reporting | `scan_request.cpp`, `duckdb_api/scan_request.hpp` | `duckdb_api_scan_request_tests` |
| Pinned structured-expression recognition | `duckdb/complex_filter_adapter.*` | `complex_filter_adapter_tests.cpp` in `duckdb_api_adapter_tests` |
| Baseline request retention and copied plan selection | `duckdb/table_function_plan_state.*` | `table_function_plan_state_tests.cpp` in `duckdb_api_adapter_tests` |
| Installed connector/runtime assembly | `product_composition.cpp`, `duckdb_api/product_composition.hpp` | `test/python/source_demo_contract.py` through `make test`; `make demo` for the live path |
| Table-function registration, callback composition, explain, bind/init/scan, cancellation, or batch transfer | `duckdb/table_function_adapter.cpp` | `duckdb_api_adapter_tests`, `duckdb_api_adapter_stream_contract_tests` |
| Secret registration, validation, or exact-name resolution | `duckdb/secret_integration.cpp`, `duckdb_api/duckdb_secret.hpp` | `duckdb_api_duckdb_secret_tests` |
| Extension identity, load order, or initialization containment | `duckdb/extension_entrypoint.cpp`, `duckdb_api_extension.hpp` | `test/sql/duckdb_api.test`, `test/python/source_demo_contract.py` |
| Controlled end-to-end composition | `test/cpp/query/integration/` | `test/python/live_rest_product_contract.py`, `test/python/authenticated_relation_product_contract.py`, `test/python/repository_pagination_product_contract.py` |

Production and test inventories are in `src/query/{sources,targets}.cmake` and
`test/cpp/query/{sources,targets}.cmake`. Shared test helpers live under
`test/cpp/query/support/`; controlled product composition belongs under
`test/cpp/query/integration/`.

## Tests

Run the ordinary developer loop from the repository root:

```sh
make build
make test
make demo
```

After `make build`, focused binaries are under
`<build_root>/extension/duckdb_api/`, where `build_root` is printed by
`make paths`. `make test` runs all four Query targets plus SQL, controlled
service, artifact, and direct-load oracles. Run `make verify` before handoff on
the supported product cell.

Read [ARCHITECTURE.md](../../docs/ARCHITECTURE.md) for query semantics and
[RUNTIME_CONTRACTS.md](../../docs/RUNTIME_CONTRACTS.md) for state, cancellation,
error, and execution contracts. Shared-interface changes follow
[CONTRIBUTING.md](../../CONTRIBUTING.md) and the
[RFC process](../../docs/RFC_PROCESS.md). Maintainer accountability is recorded
in the [Query Experience charter](../../docs/teams/QUERY_EXPERIENCE.md).

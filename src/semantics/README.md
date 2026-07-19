# Relational planning

This package turns an immutable connector definition and protocol-neutral
`ScanRequest` into a complete, immutable, explainable `ScanPlan`. Planning is
deterministic and performs no network I/O.

The planner decides what may run remotely and what DuckDB must still evaluate.
Runtime consumes the finished plan; it must never need to repeat relational
classification.

The native RFC 0008 profile admits exactly one selective shape:
`visibility = 'private'` over the authenticated-repositories response field.
Semantics selects the Connector mapping only when the closed request,
structured-callback capability, DuckDB residual-retention capability, required
VARCHAR schema, operation, and reviewed mapping all agree. The plan records a
remote `SUPERSET`, leaves the complete predicate owned by DuckDB, and carries
the sole typed `VISIBILITY_PRIVATE` conditional input. Exact single-filter
requests name the visibility residual; larger or unsupported retained filters
use an opaque complete-filter marker rather than SQL text. Every absent or
mismatched fact retains complete traversal. No other predicate, projection,
ordering, limit, or offset is delegated.

## Start here

| Change | Production code | Focused test |
| --- | --- | --- |
| Closed protocol-neutral request predicate | `relational_predicate.cpp`, `duckdb_api/relational_predicate.hpp` | `duckdb_api_scan_planner_tests` |
| Visibility implication, mapping selection, and fallback | `predicate_classifier.cpp` | `duckdb_api_scan_planner_tests` |
| Plan values, guarded payloads, or resource predicates | `scan_plan.cpp`, `duckdb_api/scan_plan.hpp` | `duckdb_api_scan_plan_contract_tests` |
| Human-readable plan snapshots | `scan_plan_explain.cpp` | `duckdb_api_scan_plan_contract_tests`, `duckdb_api_scan_plan_fixture_tests` |
| Capability narrowing or conservative classification | `scan_planner.cpp`, `duckdb_api/scan_planner.hpp` | `duckdb_api_scan_planner_tests` |
| Connector, request, authorization, or pagination admission | `scan_planner_validation.cpp` | `duckdb_api_scan_planner_tests`, `duckdb_api_scan_plan_pagination_contract_tests` |
| Private normalization shared by construction and validation | `scan_planner_internal.hpp` | `duckdb_api_scan_planner_tests` |
| Reusable plan fixtures for Runtime consumers | `test/cpp/semantics/support/` | `duckdb_api_scan_plan_fixture_tests` |

`relational_predicate.hpp` is the smaller service below Query request
construction and Semantics classification. `scan_plan.hpp` is the value-only
Runtime consumer interface and intentionally carries no Connector, Query, or
planner-construction dependency. `scan_planner.hpp` is the separate planning
entry point used by Query. Keep those dependency directions intact.

Runtime must use only `ScanPlan::ConditionalInput()` for predicate-derived
request selection. `Operation().query_parameters`, Connector provenance, and
the safe snapshot do not duplicate or supersede that authority.

## Correctness checklist

- Remote predicate `R` is safe only when DuckDB predicate `D` implies `R`;
  exact pushdown also requires `R` to imply `D`.
- Every residual predicate has exactly one owner.
- Required filtering and ordering happen before limit or offset.
- Providers preserve base-row cardinality.
- Missing DuckDB capabilities produce conservative behavior, never SQL-text
  reconstruction.
- Every classification has an explainable reason and a counterexample or
  property oracle where appropriate.

The complete semantic contract is in
[ARCHITECTURE.md](../../docs/ARCHITECTURE.md) and
[RUNTIME_CONTRACTS.md](../../docs/RUNTIME_CONTRACTS.md). For operation
selection, declared capabilities, and connector-facing proof inputs, also read
[CONNECTOR_SPECIFICATIONS.md](../../docs/CONNECTOR_SPECIFICATIONS.md).

## Tests

`make test` runs the focused planner, plan-contract, pagination-contract, and
fixture executables declared in `test/cpp/semantics/targets.cmake`. Run
`make build` before invoking one directly from
`<build_root>/extension/duckdb_api/`, where `build_root` is printed by
`make paths`. Run `make verify` before handoff on the supported product cell.

Changes to a shared `ScanRequest`, `ScanPlan`, plan explanation, or relational
rule require affected producer and consumer review and the trigger decision in
the [RFC process](../../docs/RFC_PROCESS.md) before implementation crosses the
boundary. Repository workflow is in [CONTRIBUTING.md](../../CONTRIBUTING.md),
and maintainer accountability is recorded in the
[Relational Semantics charter](../../docs/teams/RELATIONAL_SEMANTICS.md).

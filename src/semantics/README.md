# Relational planning

This package turns an immutable connector definition and protocol-neutral
`ScanRequest` into a complete, immutable, explainable `ScanPlan`. Planning is
deterministic and performs no network I/O.

The planner decides what may run remotely and what DuckDB must still evaluate.
Runtime consumes the finished plan; it must never need to repeat relational
classification.

The shared candidate is an immutable protocol-neutral tree with typed equality
leaves, ordered conjunction/disjunction, unary negation, and opaque unsupported
positions. Construction enforces depth 16 and 64 total nodes. Query owns the
DuckDB-to-candidate translation; Semantics alone binds ordinals to the selected
relation and interprets Connector proof and encoding facts. The value contains
no SQL, DuckDB expression, request input, credential, or I/O authority.

The native profile can encode one positive `visibility = 'private'`
restriction. The permanent GitHub proof produces only `SUPERSET`; the
Connector-owned controlled duplicate-occurrence fixture has the distinct
three-valued and occurrence proof needed for `EXACT`. Identical repeated leaves
may share the one declared input. A safe member of a larger conjunction may
narrow remotely while DuckDB retains the complete filter, but that opaque
complete-filter scope is accepted only when the candidate tree contains an
unsupported position. Fully represented trees use the exact requested-predicate
scope so an unseen outer `OR` cannot be hidden. Unencodable
disjunction/negation, missing mappings/capabilities, and opaque structure use
`UNSUPPORTED`; conflicting safe inputs use `AMBIGUOUS`; malformed bindings or
contracts return `PlanningError` without a partial plan. Every successful
decision keeps filtering, final projection, ordering, limit, and offset owned
by DuckDB.

Operation selection precedes classification. Semantics derives an independent
binding set for every non-fallback operation from only that operation's
predicate mappings. Required, alternative, and forbidden input selectors decide
eligibility; the largest satisfied alternative determines specificity, and
priority breaks only a specificity tie. The sole fallback is evaluated only
when no non-fallback operation is eligible. An equal top rank or no eligible
operation fails with `OPERATION_SELECTION_FAILED`; neither is reinterpreted as
predicate ambiguity or resolved by declaration order. Only the selected
operation reaches full predicate classification and plan construction. Unknown
relation identity is instead an invalid request contract.

The planned operation is an exhaustive REST-or-GraphQL value. REST preserves
the existing typed request and Link-pagination facts. GraphQL planning admits
only Connector's canonical `GITHUB_VIEWER_REPOSITORY_METRICS_V1` profile:
exact bytes and SHA-256 digest, query-only kind, fixed variables and response
paths, duplicate-preserving viewer-repository occurrence domain, sequential
mutable cursor traversal, and page/scan resource ceilings must all agree.
Semantics does not parse or generate GraphQL, serialize a request, track a
cursor, or decode a response. Fixed `UPDATED_AT DESC` enumerates the cursor but
does not grant SQL ordering or snapshot authority; body and row ceilings do not
grant limit, truncation, or retry authority.

## Start here

| Change | Production code | Focused test |
| --- | --- | --- |
| Closed protocol-neutral request predicate | `relational_predicate.cpp`, `duckdb_api/relational_predicate.hpp` | `duckdb_api_scan_planner_tests` |
| Exact/Superset proof matching, composition, and fallback | `predicate_classifier.cpp` | `duckdb_api_scan_planner_tests` |
| Ordered REST bindings and structural response paths | `rest_operation_planner.cpp`, `scan_plan_builder.hpp` | `duckdb_api_scan_planner_tests`, `duckdb_api_scan_plan_fixture_tests` |
| Plan values, guarded payloads, or resource predicates | `scan_plan.cpp`, `duckdb_api/scan_plan.hpp` | `duckdb_api_scan_plan_contract_tests` |
| Exhaustive REST/GraphQL planned operation value | `planned_protocol_operation.cpp`, `duckdb_api/planned_protocol_operation.hpp` | `duckdb_api_scan_plan_contract_tests`, `duckdb_api_graphql_semantics_tests` |
| Canonical GraphQL admission and replay derivation | `graphql_operation_planner.cpp` | `duckdb_api_graphql_semantics_tests` |
| Human-readable plan snapshots | `scan_plan_explain.cpp` | `duckdb_api_scan_plan_contract_tests`, `duckdb_api_scan_plan_fixture_tests` |
| Capability narrowing or conservative classification | `scan_planner.cpp`, `duckdb_api/scan_planner.hpp` | `duckdb_api_scan_planner_tests` |
| Connector, request, authorization, or pagination admission | `scan_planner_validation.cpp` | `duckdb_api_scan_planner_tests`, `duckdb_api_scan_plan_pagination_contract_tests` |
| Private normalization shared by construction and validation | `scan_planner_internal.hpp` | `duckdb_api_scan_planner_tests` |
| Reusable plan fixtures for Runtime consumers | `test/cpp/semantics/support/scan_plan_test_fixtures.*`, `graphql_scan_plan_test_fixtures.*` | `duckdb_api_scan_plan_fixture_tests`, `duckdb_api_graphql_semantics_tests` |
| Real planner-produced permanent REST fixture | `test/cpp/semantics/support/permanent_rest_scan_plan_test_fixtures.*` | `duckdb_api_scan_planner_tests`; Runtime links `duckdb_api_semantics_materialized_fixture_service` |

`relational_predicate.hpp` is the smaller service below Query request
construction and Semantics classification. `scan_plan.hpp` is the value-only
Runtime consumer interface and intentionally carries no Connector, Query, or
planner-construction dependency. `scan_planner.hpp` is the separate planning
entry point used by Query. Keep those dependency directions intact.

Runtime must exhaustively inspect `ScanPlan::Operation().Protocol()` and then
use the guarded `Rest()` or `Graphql()` payload. For REST, the ordered typed
`query_bindings` are the complete package request fields after omission and
operation selection. `ScanPlan::ConditionalInput()` identifies whether one of
them came from a predicate mapping; the native encoded-only mirror,
Connector provenance, and safe explanation do not duplicate or supersede that
authority. For GraphQL, Runtime consumes the typed document, variable,
response, and cursor plan without parsing relation names, source snapshots, or
explanation to recover authority.

`PredicateCategory()` and `PredicateReason()` are the stable diagnostic
contract. `ClassificationReason()` and `Snapshot()` are safe prose and must
never be parsed for execution decisions. Package plan provenance excludes
author defaults and predicate literals; typed predicate explanation records
only kind and presence. Exact and Superset remain independent of residual
ownership: both retain the offered DuckDB filter in this profile.

## Correctness checklist

- Remote predicate `R` is safe only when DuckDB predicate `D` implies `R`;
  exact pushdown also requires `R` to imply `D`.
- Every residual predicate has exactly one owner.
- Required filtering and ordering happen before limit or offset.
- Providers preserve base-row cardinality.
- Missing DuckDB capabilities produce conservative behavior, never SQL-text
  reconstruction.
- Every classification has a structured reason and a counterexample or
  property oracle where appropriate.
- An Exact proof preserves the full three-valued vector and base-occurrence
  multiplicity; Superset preserves every DuckDB-true occurrence and its
  multiplicity.

The complete semantic contract is in
[ARCHITECTURE.md](../../docs/ARCHITECTURE.md) and
[RUNTIME_CONTRACTS.md](../../docs/RUNTIME_CONTRACTS.md). For operation
selection, declared capabilities, and connector-facing proof inputs, also read
[CONNECTOR_SPECIFICATIONS.md](../../docs/CONNECTOR_SPECIFICATIONS.md).

## Tests

`make test` runs the focused planner, plan-contract, pagination-contract,
GraphQL, and fixture executables declared in
`test/cpp/semantics/targets.cmake`. The planner
target includes a pinned-DuckDB law oracle over TRUE/FALSE/NULL values and
duplicate occurrence identifiers; it compares DuckDB-only with
remote-plus-retained-residual result bags. Run
`make build` before invoking one directly from
`<build_root>/extension/duckdb_api/`, where `build_root` is printed by
`make paths`. Run `make verify` before handoff on the supported product cell.

Changes to a shared `ScanRequest`, `ScanPlan`, plan explanation, or relational
rule require affected producer and consumer review and the trigger decision in
the [RFC process](../../docs/RFC_PROCESS.md) before implementation crosses the
boundary. Repository workflow is in [CONTRIBUTING.md](../../CONTRIBUTING.md),
and maintainer accountability is recorded in the
[Relational Semantics charter](../../docs/teams/RELATIONAL_SEMANTICS.md).

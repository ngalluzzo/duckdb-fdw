# Relational Semantics source package

This package is owned by the
[Relational Semantics team](../../docs/teams/RELATIONAL_SEMANTICS.md). It
implements the deterministic, network-free transformation from an immutable
Connector definition and Query request into an immutable, explainable scan
plan.

## Team APIs

The package consumes Connector Experience's validated, immutable
`CompiledConnector` and Query Experience's protocol-neutral `ScanRequest` and
capability profile. It provides the stable `duckdb_api/scan_plan.hpp` facade:
the immutable `ScanPlan`, its relational ownership and resource obligations,
its normalized logical-secret selector, and its explanation snapshot. Runtime
links the corresponding plan-value service without Connector, Query, or planner
construction dependencies. The separate `duckdb_api/scan_planner.hpp` facade
provides `BuildConservativeScanPlan` to Query and Semantics fixtures.

Dependencies point from Semantics to the bounded Connector and Query public
facades only inside planner construction. Query consumes the planner facade;
Remote Runtime consumes only the plan facade. Neither consumer constructs or
reinterprets planner internals.

## Implementation units

- `scan_plan.cpp` owns immutable plan value behavior, guarded optional payloads,
  resource-bound predicates, and accessors.
- `scan_plan_explain.cpp` owns the stable deterministic rendering of plan facts,
  ownership, obligations, budgets, and classification reasons.
- `scan_planner.cpp` owns capability and resource narrowing, conservative
  classification, and construction of a complete plan.
- `scan_planner_validation.cpp` owns exact provider selection and validation of
  Connector facts, Query requests, authorization, and pagination envelopes.
- `scan_planner_internal.hpp` is the private vocabulary-normalization seam shared
  by construction and validation; it is not a consumer-facing team API.

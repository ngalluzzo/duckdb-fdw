# Goal: Floating-point (DOUBLE) scalar type

## PM brief

### Outcome

For a connector author, enable declaring a column or relation input of a
floating-point numeric type, `DOUBLE`, so that packages targeting APIs whose
JSON fields hold non-integral numbers (price, rating, coordinates, exchange
rate, percentage, temperature) can be authored through the same v1 path,
without a lossy `BIGINT`-truncation workaround or an unparsable
`VARCHAR`-and-cast workaround, and can filter on that value with the same
equality pushdown `BOOLEAN`/`BIGINT`/`VARCHAR` already support.

### Why now

This is the third proactive capability-gap closure ahead of `ROADMAP.md`'s
1.0.0 gate requiring 10 connector providers (2 exist today: GitHub, Rick and
Morty). A fresh architecture-maturity re-assessment, run after `api_key` and
`short_page` pagination shipped, found this is the highest-leverage remaining
gap: `duckdb_api/v1`'s scalar type set is closed at exactly `{BOOLEAN,
BIGINT, VARCHAR}` with no floating-point type at all. Unlike dates (already
capturable as ISO-8601 `VARCHAR` and `CAST` in SQL), there is no workaround
for a JSON field holding a non-integral number — `BIGINT` decode explicitly
requires the value be integral and lossless, so the field simply cannot be
declared. This is judged to block a wider cross-section of real free/hobby
REST APIs than basic auth or the already-twice-deferred opaque-cursor
pagination strategy.

### Product guardrails

- Must: preserve every existing scalar-type invariant (closed schema/RFC-
  gated evolution, static author-declared typing, no dynamic schema
  inference, exact/canonical encoding discipline) for the new type too.
- Must not: introduce an arbitrary-precision `DECIMAL`/`NUMERIC(width,
  scale)` type in this first cut — that needs its own width/scale
  declaration model and its own RFC. This goal is exactly one new type,
  IEEE-754 double-precision floating point.
- Preserve: the closed-schema/RFC-gated evolution model — this is a new
  scalar `type` enum value, a durable public contract change needing its own
  RFC per `docs/RFC_PROCESS.md`.

### Success signals

- A connector author can declare `type: DOUBLE` for a column or relation
  input and have it validate, compile, plan, explain, and execute exactly
  like `BIGINT`/`VARCHAR`/`BOOLEAN` do today, decoding a non-integral JSON
  number without truncation or a compile-time rejection.
- A connector author can use a `DOUBLE`-typed conditional input in a
  predicate mapping, with the same single-positive-equality pushdown
  `BOOLEAN`/`BIGINT`/`VARCHAR` already support.
- Existing bearer/api_key-authenticated, link_next/response_next/
  short_page-paginated packages (GitHub, Rick and Morty) are unaffected — no
  compatibility break, no version bump for unrelated connectors.
- The redaction/diagnostic/digest invariants that already apply to
  `BOOLEAN`/`BIGINT`/`VARCHAR` values apply identically to `DOUBLE`.

### Reserved product decisions

- **Resolved (2026-07-22):** `DOUBLE` participates in predicate/
  conditional-input equality pushdown in this first cut (not deferred to a
  follow-on). The equality semantics for this pushdown (exact canonical-form
  comparison, no epsilon tolerance — resolved as a technical design decision
  consistent with how `BIGINT`/`VARCHAR` already compare, not a further
  product decision) are recorded in the RFC.

## Agent commitment

### Observable interpretation

A connector author writing a manifest for an API that exposes a non-integral
JSON number (e.g. `"price": 19.99`, `"latitude": 37.7749`) declares
`type: DOUBLE` for the corresponding column or relation input. The relation
compiles, plans, `EXPLAIN`s, and executes: the value decodes as a DuckDB
native `DOUBLE`, round-trips exactly, and — if declared as a conditional
input — can be used in a predicate mapping with the same single-positive-
equality pushdown the three existing scalar types already support. An author
who declares a non-finite value, an out-of-range value, or a malformed
number gets the same field-precise diagnostic quality the existing three
types already give.

### Acceptance evidence

- Demonstration: a new fixture relation (or an extension of an existing
  fixture) declares a `DOUBLE` column and, separately, a `DOUBLE` conditional
  input used in a predicate mapping, and returns/filters correctly through
  the existing `duckdb_api_load_connector` path.
- Automated oracle: schema/compiler fixture coverage mirroring the existing
  `BIGINT`/`VARCHAR` variant sets (valid value, boundary/round-trip values,
  malformed-number rejection, non-finite-value rejection if reachable,
  canonical-encoding mismatch rejection); a Relational Semantics property
  test proving `DOUBLE` participates correctly in the existing typed-
  equality predicate machinery; a Query-owned test proving the DuckDB vector
  conversion round-trips exactly for representative values including zero,
  a negative value, and a value requiring many significant digits.
- Quality gates: `make build`, `make test`, `make demo`; source-identity,
  public-surface-inventory, and contract-freeze scripts and their tests.
- Independent review: `$adversarial-review` with at least two perspectives
  (this changes a value representation used across encoding, decoding,
  predicate equality, and diagnostics — broader surface than a typical new
  output type, since every value-carrying struct in the pipeline stores one
  field per scalar kind side by side rather than a tagged union).

### Contract and invariant impact

- `docs/CONNECTOR_SPECIFICATIONS.md`'s scalar-type grammar and canonical-
  encoding rules; `docs/RUNTIME_CONTRACTS.md`'s decode/encode description.
- `CompiledScalarType`, `PlannedRestScalarKind`, `PlannedColumnScalarKind`,
  `ValueKind`, and every exhaustive switch or equality-comparison function
  over them across Connector, Relational Semantics, Remote Runtime, and
  Query Experience (a substantially broader site list than a typical scalar
  addition — see RFC 0020 for the full inventory).
- Two pre-existing silent-fallthrough hazards found during research, not
  caused by this change but directly relevant to it: `ScalarType()` (schema→
  compiled-type mapping) and `CompileConcreteScalar` (default-value
  compilation) both currently treat an unrecognized type string as `VARCHAR`
  by silent fallthrough rather than failing closed. `DOUBLE` must be
  recognized correctly by both before this goal can be considered complete,
  and fixing the silent-fallthrough behavior itself (fail closed for any
  truly unrecognized string) is in scope since it is the mechanism by which
  `DOUBLE` becomes recognized at all.
- `release/1.0.0/freeze.json`'s scalar-type-set authority (mirroring how
  `pagination_strategies`/credential kinds are tracked).

### Team and RFC routing

- Accountable stream: Connector Experience (a connector author declaring and
  loading a new scalar type).
- Supporting interactions: Relational Semantics (Collaboration — predicate-
  equality and typed-value classification correctness), Remote Runtime
  (Collaboration — JSON decode/encode of a non-integral number), Query
  Experience (X-as-a-Service — DuckDB `LogicalType::DOUBLE` vector
  conversion, already natively supported by DuckDB), Engineering Enablement
  (Facilitation — fixture-coverage completeness).
- RFC: required (new public connector-package contract, a shared
  `CompiledConnector`/`ScanPlan` interface change, and touches the predicate-
  equality machinery directly). Drafted as RFC 0020; status tracked there.

### Unknowns and first trial

- Unknown: the canonical string encoding for a `DOUBLE` value (significant
  digits / round-trip guarantee, exponent notation, `-0.0` normalization) —
  no existing convention in the repository to reuse; resolved as a design
  decision in RFC 0020 (round-trip-exact via a fixed-precision `%.17g`-style
  encoding, reusing the existing VARCHAR percent-encoding transform for the
  resulting text, with `-0.0` normalized to `0` at construction time to
  avoid an equality-comparison ambiguity).
- Unknown: whether DuckDB's own `complex_filter_adapter.cpp` (predicate
  pushdown from the DuckDB planner side) needs a `DOUBLE`-specific arm —
  flagged during research as not yet read in full; must be checked directly
  during implementation.
- No trial needed before the RFC: DuckDB's own `LogicalType::DOUBLE` and
  `Value::DOUBLE(...)` already exist natively, so the Query-layer vector-
  conversion feasibility is already established by direct source inspection.

### Delivery path

1. RFC 0020 accepted (encoding/equality design, full site inventory, and
   cross-team obligations decided).
2. Implement: schema (three duplicated inline enum sites), compiler (fixing
   the two silent-fallthrough hazards while adding DOUBLE), compiled IR
   (`CompiledScalarType::DOUBLE` plus a `double_value` field and updated
   inactive-payload validation across every parallel-fields struct),
   Relational Semantics (predicate/typed-equality machinery), Remote Runtime
   (JSON decode/encode), Query Experience (`LogicalType::DOUBLE` vector
   conversion), fixture coverage, freeze-artifact update.
3. Adversarial review, quality gates, commit.

## Governance

Follow docs/PRODUCT_DELIVERY.md. Pursue: a connector author can declare a
column or relation input of a floating-point numeric type, `DOUBLE`,
including predicate/conditional-input equality pushdown, so that packages
targeting APIs whose JSON fields hold non-integral numbers can be authored
through the same v1 path.
Completion requires: the acceptance evidence in this goal's Agent commitment
section and RFC 0020's Acceptance and verification section (schema/compiler
fixtures, a Relational Semantics typed-equality property test, a Query-owned
DuckDB vector round-trip test, a predicate-pushdown-proof test, and a real
`EXPLAIN` test), plus independent adversarial review given the breadth of
the parallel-fields struct surface this RFC touches.
Preserve: every existing scalar-type invariant (closed schema/RFC-gated
evolution, static author-declared typing, canonical/exact encoding
discipline); AGENTS.md and the relevant architecture/connector/runtime
contracts.
Governance: Accountable stream is Connector Experience. RFC 0020 accepted
2026-07-22 (`docs/rfcs/0020-add-double-scalar-type.md`) — five-team
topology-consult review complete, all five reviewers objected with
evidence-backed corrections (the highest correction rate of any RFC so far,
including two decision-critical fixes: the `strtod` overflow/underflow rule
and the equality-semantics design), all dispositioned by revising the RFC's
technical content; no reviewer objected to the underlying decision.

## Completion record

Delivered 2026-07-22 via delivery-loop, following RFC 0020's accepted design.

**Scope delivered:**
- Schema: `DOUBLE` added to all three inline `type` enum sites in
  `connector-package-v1.schema.json` (column, input, predicate literal defs),
  with the evidence copy, `.schema.inc` regeneration, and digest updated.
- Compiler: `CompiledScalarType::DOUBLE`, `CompiledScalarValue::Double()`,
  `CompiledModelBuilder::Double(...)` (`-0.0` normalized to `0.0` at
  construction), `IsCanonicalDouble` grammar-then-`strtod` validator (rejects
  only `HUGE_VAL`/`-HUGE_VAL`, accepts underflow), `EncodeCanonicalDouble`
  (`%.17g`, 17 significant digits) in the connector layer, and every
  exhaustive switch across `catalog_model.cpp`, `catalog_snapshot.cpp`,
  `package_compile_helpers.cpp`, `package_relation_schema.cpp`,
  `package_predicate_schema.cpp`, `package_fixture_index.cpp`,
  `package_predicate_compiler.cpp`, `package_compatibility.cpp`,
  `predicate_declaration.cpp`, `protocol_operation_declaration.cpp`,
  `package_fixture_index_validation.cpp`, `package_fixture_coverage.cpp`.
  The two silent-fallthrough hazards identified in the RFC
  (`ScalarType()`/`CompileConcreteScalar`) were confirmed unreachable in the
  live compile path (upstream schema-decode sites already fail closed before
  either function runs) and were left with their existing fallback behavior
  unchanged, per the RFC's implementation-time refinement.
- Relational Semantics: `PlannedRestScalarKind::DOUBLE`,
  `PlannedColumnScalarKind::DOUBLE`, `RequestedPredicateValueKind::DOUBLE`,
  `RequestedPredicateValue::Double(...)`, `PlannedEqualityPredicate::DoubleValue()`
  plus its inactive-payload invariant, direct `==` equality throughout
  (`SameTypedLiteral`, `SameTypedValue`, `TypedLiteralMatches`,
  `ColumnTypeMatches`), independent byte-identical `%.17g` encoders in
  `rest_operation_planner.cpp` and `planned_protocol_operation.cpp`, and
  `ResolvedRelationInput`/`ExplicitInput`'s own `double_value` field and
  `DoubleValue()` accessor.
- Remote Runtime: `ValueKind::DOUBLE`, `TypedValue::Double(...)`,
  `ParseDouble` in both the JSON and GraphQL response decoders (rejects only
  `HUGE_VAL`/`-HUGE_VAL`, matching the Connector-layer rule), a third
  independent byte-identical `%.17g` encoder in
  `rest_request_materialization.cpp`, and `RestConditionalBindingAuthority`'s
  `double_value` field (explicitly zero-initialized in its constructor — a
  real uninitialized-member bug caught and fixed before it could reach
  release).
- Query Experience: `LogicalType::DOUBLE`/`Value::DOUBLE(...)` conversion in
  `typed_value_adapter.cpp`, `DoubleValue::Get(...)` extraction in
  `generated_relation_adapter.cpp`, a `%.17g` default-value renderer in
  `package_introspection_functions.cpp`, and — the review-confirmed
  decision-critical site — `complex_filter_adapter.cpp`'s `RequestedType`/
  `RequestedLiteral` arms, without which a `DOUBLE` predicate would silently
  never reach the remote request.
- **Material decision made during implementation, not pre-specified in the
  RFC text:** GraphQL relations use a separate `CompiledGraphqlScalarKind`
  enum (`STRING`/`INT64`/`BOOLEAN`, no Float equivalent). Rather than build
  full GraphQL Float support or silently decode a `DOUBLE` GraphQL column as
  `STRING` (a real correctness bug caught by inspection), `type: DOUBLE` on a
  GraphQL relation column is rejected at compile time in
  `package_graphql_renderer.cpp` with a precise diagnostic, mirroring RFC
  0018's precedent of rejecting `api_key`+GraphQL rather than a silent
  runtime mismatch.
- Fixtures and tests, covering all five required layers: a Runtime
  fixture-variant harness (`DOUBLE_MINIMUM`/`DOUBLE_MAXIMUM`/
  `DOUBLE_MAGNITUDE_OVERFLOW_REJECTED`, exercised through a dedicated
  single-column plan+transcript fixture to avoid risking the widely shared
  3-column anonymous fixture); a schema/compiler round-trip test covering
  the largest finite magnitude, a 17-significant-digit value, and a
  subnormal underflow value, plus a magnitude-overflow rejection test; a
  Relational Semantics typed-equality property test (`double_predicates`
  fixture relation, direct `==`); a predicate-pushdown-proof test asserting
  the exact encoded query parameter reaches the constructed REST request;
  and a real end-to-end DuckDB test (`DESCRIBE` proving the literal
  `"DOUBLE"` type string, then a real `SELECT` proving the decoded value
  round-trips through `WriteTypedBatch` into an actual DuckDB result vector)
  — EXPLAIN's own safe-explanation map carries no per-column type fact,
  so DESCRIBE is the correct oracle here, not merely a convenient one.
- Freeze artifact: `release/1.0.0/freeze.json` gains a new `scalar_types`
  domain (`authored: [BOOLEAN, BIGINT, VARCHAR, DOUBLE]`), cross-checked in
  `scripts/contract_freeze.py` against the schema's closed column-type enum,
  with mutation-test coverage in `test/python/contract_freeze_tests.py`.
  Docs updated: `docs/CONNECTOR_SPECIFICATIONS.md` (scalar-type grammar,
  conversion rule, default-literal grammar), `docs/RUNTIME_CONTRACTS.md`
  (scalar-kind list, encoding rule), `ROADMAP.md` (new `0.13.0` section).
- Two pre-existing test fixtures were found to use the string `"DOUBLE"` as
  their own example of a schema-rejected/unsupported type
  (`scan_plan_contract_tests.cpp`'s `TestPlannedColumnScalarKindRejectsUnsupportedTypes`,
  and the `UNSUPPORTED_SCHEMA_TYPE` response-plan counterexample) — both
  updated to use `"DECIMAL"` instead, since `DOUBLE` is now a genuinely
  supported type.

**Verification:** `make build`, `make test`, and `make demo` all pass with
zero regressions in the full pre-existing suite (unrelated pin/format
churn from broad multi-team edits was resolved: `release/0.10.0/pins.json`'s
`native_product_sources` digest updated twice, once for the initial change
set and once after a repo-wide `clang-format` pass; `package_compatibility_contract_tests.cpp`
and the two `"DOUBLE"`-as-counterexample fixtures above were the only test
assertions that needed updating for the new type's existence). `scripts/verify-source-identities.py`,
`scripts/verify-public-surface-inventory.py`, and `scripts/contract_freeze.py`
plus their Python test suites all pass.

**Adversarial review:** three independent perspectives (Relational Semantics,
Transport/Remote Runtime encoding, Test Oracle) via parallel fresh-context
agents. Relational Semantics and Transport/Runtime reported zero findings
after directly verifying the equality/-0.0-normalization/`%.17g`-encoder-
byte-identity claims and the `strtod` overflow/underflow rule, respectively.
Test Oracle found two P1s and two P2s, all fixed before commit: (1) the
subnormal round-trip assertion checked `> 0.0` instead of the exact bit
pattern, now asserts `== 4.9e-324`; (2) no Runtime-layer fixture proved
underflow-acceptance end to end (only the schema/compiler layer had
coverage) — added `RuntimeFixtureColumnVariant::DOUBLE_SUBNORMAL`; (3) no
test proved differently-formatted JSON numbers (`1.5`, `1.50`, `1.5e0`,
`0.00015e4`, `15e-1`) decode identically — added
`TestDoubleFormatEquivalence` to `json_decoder_tests.cpp`; (4) the
`input_resolution_observation_service` test harness explicitly threw
"not yet support DOUBLE" instead of being extended, leaving relation-input
`DOUBLE` resolution (a genuinely-changed production code path) unobserved —
fully wired `ObservedScalarKind::DOUBLE` through the service and added a
new isolated single-relation, single-DOUBLE-input fixture
(`BuildDoubleRelationInputPackageGenerationFixture`) plus a "controlled
DOUBLE default" test, deliberately not touching any existing shared
relation-input fixture. All four fixes verified: `make build`, `make test`,
`make demo`, and the Python contract/inventory test suites all pass with
zero regressions after the fixes.

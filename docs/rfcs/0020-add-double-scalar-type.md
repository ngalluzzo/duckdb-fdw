# RFC 0020: Add a DOUBLE scalar type to duckdb_api/v1

```yaml
rfc: "0020"
title: "Add a DOUBLE scalar type to duckdb_api/v1"
status: "Accepted"
rfc_type: "Product"
sponsor_team: "Connector Experience"
technical_decision_owner: "Lead agent"
product_approver: "Nic Galluzzo"
authors:
  - "Lead agent"
required_reviewers:
  - "Connector Experience"
  - "Query Experience"
  - "Remote Runtime"
  - "Relational Semantics"
  - "Engineering Enablement"
affected_teams:
  - "Connector Experience"
  - "Query Experience"
  - "Remote Runtime"
  - "Relational Semantics"
  - "Engineering Enablement"
linked_outcome_or_objective: "docs/goals/double-scalar-type.md: the third proactive capability-gap closure ahead of ROADMAP.md's 1.0.0 ten-provider gate."
supersedes: "Not applicable"
```

## Summary

Adds `DOUBLE` (IEEE-754 double-precision floating point) as a fourth scalar
type to `duckdb_api/v1`'s closed set, alongside `BOOLEAN`, `BIGINT`, and
`VARCHAR`. `DOUBLE` may be declared for a column, a relation input, or a
predicate/conditional-input equality target, with the same field-precise
diagnostic quality and closed-set discipline the existing three types
already have. Unlike a typical closed-set addition, this touches a wider
surface than usual: every value-carrying struct in the pipeline stores one
field per scalar kind side by side (not a tagged union), so adding a fourth
kind means adding a `double_value` field and an updated inactive-payload
invariant to each of roughly seven independent struct definitions across
four teams, not just adding a switch arm in a handful of places.

## Sponsorship and context

- **RFC type:** Product. The decision changes `duckdb_api/v1`'s closed
  scalar-type contract, a public, author-facing surface used by columns,
  relation inputs, and predicate literals alike.
- **Sponsoring team:** Connector Experience, which owns
  `docs/CONNECTOR_SPECIFICATIONS.md` and the package schema/compiler this
  decision extends.
- **Linked outcome or objective:** `docs/goals/double-scalar-type.md` — the
  third proactive capability-gap closure ahead of `ROADMAP.md`'s 1.0.0 gate
  requiring 10 connector providers (2 exist today: GitHub, Rick and Morty).
  A fresh architecture-maturity re-assessment, run after RFC 0018's
  `api_key` and RFC 0019's `short_page` shipped, ranked this the
  highest-leverage remaining gap among the candidates considered (basic
  auth, non-JSON response bodies, opaque-cursor pagination): it blocks the
  widest cross-section of real free/hobby-tier REST APIs, since almost any
  API exposing a price, rating, coordinate, percentage, or measurement uses
  a non-integral JSON number today, with no workaround at all (unlike a
  date, which an author can already capture as `VARCHAR` and `CAST` in
  SQL).
- **Why now:** Deciding now — before a third or later connector provider is
  authored against an API exposing this shape — lets the project include it
  deliberately rather than discover the gap mid-authorship, the way RFC
  0016's `response_next` gap was discovered while authoring
  `connectors/rickandmorty`.

## Problem

`docs/CONNECTOR_SPECIFICATIONS.md`'s scalar-type grammar accepts exactly
three types today: `BOOLEAN`, `BIGINT` (a signed 64-bit integer, required to
decode from a JSON number "losslessly" — a non-integral or out-of-range
value is rejected as `DUCKDB_API_INVALID_TYPE`), and `VARCHAR` (validated
UTF-8 text). There is no floating-point or fixed-point numeric type. A
connector author targeting an API whose JSON response contains a field like
`"price": 19.99`, `"latitude": 37.7749`, or `"temperature_c": -3.5` cannot
declare that field as any scalar type that preserves its value: `BIGINT`
decode explicitly rejects the fractional part as a lossy-conversion failure,
and `VARCHAR` would capture a string like `"19.99"` only if the JSON author
happened to encode the number as a JSON string in the first place — for the
overwhelmingly common case of a bare JSON numeric literal (`19.99`, not
`"19.99"`), there is no accepted scalar type at all, and the relation cannot
be declared.

This is not a defect: RFC 0013 scoped the scalar-type set to what GitHub
actually needed (`BIGINT` ids, `VARCHAR` text, `BOOLEAN` flags), and no
prior RFC has proposed widening it. `release/1.0.0/freeze.json` does not
pre-name this gap as an exclusion the way it does for pagination
strategies and credential kinds, because it was never previously identified
as a live decision — this RFC raises it for the first time.

## Decision drivers and invariants

- **Must preserve:** every invariant `BIGINT`/`VARCHAR`/`BOOLEAN` currently
  enforce — static author-declared typing (no dynamic schema inference),
  closed-set discipline (an unrecognized type string fails closed at the
  `SCHEMA` phase), canonical/exact encoding (the connector's declared
  literal must byte-for-byte match what Runtime independently encodes from
  the same typed value), and the existing redaction/diagnostic/digest
  guarantees for scalar values.
- **Must enable:** representing a real, extremely common REST/JSON data
  shape (a non-integral numeric field) as both an output column and a
  predicate/conditional-input equality target, without inventing a new
  precision or tolerance model beyond what IEEE-754 double already defines.
- **Must not introduce:** an arbitrary-precision `DECIMAL`/`NUMERIC(width,
  scale)` type (a materially different declaration and storage model,
  reserved for a future RFC if a real connector author needs exact decimal
  arithmetic); a floating-point equality-tolerance/epsilon policy (this RFC
  uses exact canonical-form comparison, identical in spirit to how `BIGINT`
  and `VARCHAR` already compare, not a fuzzy-match policy); silent
  misclassification of an unrecognized type string as some other type (see
  Correctness analysis below — two pre-existing sites already do this and
  must be fixed as part of recognizing `DOUBLE` correctly).

## Proposed decision

### The key architectural fact this proposal rests on

Every value-carrying struct in the compiled/planned/runtime pipeline —
`CompiledScalarValue`, the JSON decoder's internal `ParsedSlot`,
`PlannedEqualityPredicate`, `PlannedRestQueryBinding`, `PlannedScalar`
(semantics-internal), `ExplicitInput`, `RequestedPredicateValue`, and
`TypedValue` — stores one field per scalar kind side by side (a `bool
boolean_value`, an `int64_t bigint_value`, a `std::string varchar_value`,
all present regardless of which kind is active), not a tagged
union/`std::variant`. Each struct's constructor cross-validates that every
*inactive* field holds its canonical empty/zero value. Adding `DOUBLE`
therefore means adding a `double_value` field and an updated inactive-
payload check to each of these roughly seven struct definitions
independently, in addition to adding an arm to every exhaustive switch over
the scalar-kind enums. This RFC does not propose converting these structs to
a tagged union — that is a larger, unrelated refactor with its own risk
profile — and instead follows the existing parallel-fields convention
exactly, so the new type is structurally indistinguishable in shape from
the existing three.

### Public behavior

Adds one new accepted value, `DOUBLE`, to the scalar `type` grammar
wherever `BOOLEAN`/`BIGINT`/`VARCHAR` are currently accepted: column
declarations, relation-input declarations, and predicate-literal
declarations (`docs/CONNECTOR_SPECIFICATIONS.md`). No existing accepted
package, including `connectors/github` and `connectors/rickandmorty` as
currently authored, changes behavior — this is an addition to the closed
set, not a change to the existing three types. `EXPLAIN` output gains
`double`/`DOUBLE` as a possible scalar-type fact value alongside the
existing three, wherever a type name is rendered.

A `DOUBLE` value decodes from any JSON numeric literal, including one with
a fractional part and/or exponent notation (`19.99`, `1.5e10`), with no
integrality requirement. A JSON `NaN`/`Infinity`/`-Infinity` bare literal is
not valid JSON and is already rejected by the decoder's tokenizer before
any per-column type dispatch is reached (confirmed by direct source
inspection: the tokenizer only recognizes tokens starting with `"`,
`-`/digit, `t`, `f`, `n`, `[`, `{`), so `DOUBLE` introduces no new non-finite-
value handling requirement beyond what every other type already gets for
free from the JSON grammar itself.

**Overflow vs. underflow (corrected during Remote Runtime review).** An
earlier draft of this RFC proposed treating `strtod`'s `ERANGE` uniformly as
a rejection, "consistent with `BIGINT`'s `ERANGE` rejection." Review found
this materially wrong: unlike `strtoll` (whose `ERANGE` means only "truly
out of range"), `strtod` sets `errno = ERANGE` on **both** true overflow
(magnitude too large to represent, e.g. a JSON `1e400`, result
`HUGE_VAL`/`-HUGE_VAL`) **and** benign underflow to a subnormal or exact
zero (e.g. `4.9e-324`, the smallest positive subnormal, or `1e-325`,
underflowing to exact `0.0`) — confirmed by direct empirical test on this
platform's libc. A blanket "`ERANGE` means reject" rule would incorrectly
reject a legitimate tiny measurement a real API might send (a small sensor
reading, a probability, a normalized coordinate), directly undermining this
RFC's own motivating examples. `ParseDouble` must therefore distinguish the
two cases: reject only true overflow (`result == HUGE_VAL || result ==
-HUGE_VAL`), and accept an underflowed result (subnormal or exact zero) as
the correctly-rounded, legitimate value `strtod` itself already computed —
the corresponding decoder-rejection fixture category is named
`magnitude_overflow_rejected` (a JSON-syntactically-valid number too large
to represent as any finite double), with no analogous
`underflow_rejected` category, since underflow to a representable
(if imprecise) value is not an error.

`strtod`'s decimal-point character is `LC_NUMERIC`-sensitive per the C
standard in principle; nothing in this codebase calls `setlocale`, and if a
host process's locale ever changed the decimal separator, the existing
trailing-garbage check (`*end` must be the token's exact end) already fails
closed on the resulting parse mismatch rather than silently misinterpreting
the value — a latent, low-severity assumption worth disclosing (see
Drawbacks) but not a correctness gap this RFC must close.

**Canonical encoding.** `DOUBLE`'s canonical *wire* string encoding — used
only where a scalar value must become HTTP query-parameter text
(`EncodeCompiledQueryScalar` and its Semantics-layer mirror,
`rest_operation_planner.cpp`'s `Encode`) — is a fixed-precision, round-trip-
exact decimal/exponential form (`%.17g`-style: 17 significant decimal digits
is the smallest fixed precision proven to round-trip any IEEE-754 double
bit-for-bit, per Steele & White). This reuses the existing `VARCHAR`
percent-encoding transform for the resulting text (the `%.17g` output can
contain `+`/`-`/`.`/`e` characters that already have defined percent-
encoding treatment) rather than inventing a second text-escaping routine.
`-0.0` is normalized to positive `0.0` at the point a `CompiledScalarValue`/
typed literal is constructed (not at encoding or comparison time) — this
gives every downstream consumer, including direct `==` comparison, a single
canonical in-memory value for zero, with no separate bit-pattern case to
track. This RFC deliberately does not adopt a shortest-round-trip encoding
(e.g. Grisu3/Ryu-style, as `std::to_chars` for floating-point would provide
in C++17) because the project's `CMAKE_CXX_STANDARD` is 11; `%.17g` via
`snprintf` is the portable, standard-library-only equivalent guarantee
(round-trip exactness), at the cost of a non-minimal digit count, which is
acceptable since the encoded form is a wire representation, not primarily
an author-facing aesthetic.

**Equality semantics (corrected during Relational Semantics review).** An
earlier draft of this section claimed the existing typed-equality/typed-
literal-comparison functions (`SameScalarValue`, `SameScalar` [two copies],
`SameTypedValue`, `SameTypedLiteral`) already compare `BIGINT`/`VARCHAR` via
a stored canonical `encoded_value` field. Review found this false by direct
inspection: none of these functions' operand structs
(`CompiledScalarValue`, `PlannedEqualityPredicate`) carry an `encoded_value`
field at all — every one of these functions already compares **raw decoded
values directly** (`Bigint() == Bigint()`, `Varchar() == Varchar()`), with
`encoded_value` existing only on the separate wire-encoding structs
(`CompiledQueryParameter`, `PlannedRestQueryBinding`), never consulted by
comparison. The correct, precedent-consistent design is therefore simpler
than originally proposed: `DOUBLE` equality in all five comparison functions
is a direct `==` on the normalized (`-0.0` → `0.0`) decoded `double` value —
exactly mirroring how `BIGINT`/`BOOLEAN` already compare, with **no string
encoding involved in comparison at all**. Since `-0.0` is normalized at
construction and non-finite values cannot reach construction (see Public
behavior above), this direct `==` comparison has no remaining surprise case
to guard against; it is not a new tolerance policy, it is the *absence* of
one, identical in spirit to how `BIGINT` already works. The `%.17g` encoder
remains necessary, but strictly for wire encoding, not for equality.

### Shared interfaces

- **Connector Experience:** `connector-package-v1.schema.json`'s scalar-type
  enum is currently duplicated inline three times (column, input, predicate
  literal — no shared `$def` exists today); each gains `"DOUBLE"`.
  `CompiledScalarType` (`src/include/duckdb_api/connector_catalog.hpp:31`)
  gains `DOUBLE`; `CompiledScalarValue` (same file, `:38-76`, implemented
  `src/connector/catalog_model.cpp:217-262`) gains a `double_value` field, a
  `Double()` accessor, and an updated inactive-payload constructor check.
  Two **pre-existing silent-fallthrough hazards**, found during RFC
  research and not caused by this change but directly relevant to it, must
  be fixed alongside `DOUBLE`'s addition: `ScalarType(const LocatedText&)`
  (declared `src/connector/package/package_model_compiler_internal.hpp:16`,
  implemented `src/connector/package/package_compile_helpers.cpp:65-73`)
  and `CompileConcreteScalar` (`src/connector/package/
  package_compile_helpers.cpp:103-124`) both currently treat *any*
  unrecognized type string as `VARCHAR` by silent fallthrough rather than
  failing closed with a diagnostic. **Corrected during Connector Experience
  review:** recognizing the new `"DOUBLE"` string only requires adding one
  more branch before the existing fallback — it does not, by itself,
  require changing what happens to a string that remains unrecognized
  afterward. Fixing that fallthrough to fail closed is therefore a
  **separate, explicitly disclosed decision bundled into this RFC**, not an
  automatic consequence of recognizing `DOUBLE` — see Compatibility and
  migration for its disclosed effect. Every other exhaustive switch over
  `CompiledScalarType` needs a `DOUBLE` arm: `ValidateScalarType`,
  `CompiledScalarTypeName`, the `Boolean()/Bigint()/Varchar()` accessor
  guards (`catalog_model.cpp`), `IsTypedScalar` (`package/
  package_fixture_index_validation.cpp`), `SameScalar` (`package/
  package_predicate_compiler.cpp` and `package_compatibility.cpp`, two
  independent copies), `EncodeCompiledQueryScalar`
  (`protocol_operation_declaration.cpp`), `SameScalarValue` and
  `AppendTypedLiteral` (`predicate_declaration.cpp`), and the required-
  fixture-coverage generator (`package/package_fixture_coverage.cpp`,
  which currently branches only on `BIGINT`/`VARCHAR` for required coverage
  variants and needs a `DOUBLE` branch — e.g. round-trip/boundary/
  malformed-number variants, mirroring `BIGINT`'s `{minimum, maximum,
  underflow_rejected, overflow_rejected, fraction_rejected}` set with
  `fraction_rejected` naturally dropped since `DOUBLE` accepts fractions).
- **Relational Semantics:** genuinely affected across **three** independent
  scalar-kind-shaped enums that must each gain a `DOUBLE`/matching arm
  (corrected during review from an earlier draft's "two independent
  enums," which undercounted): `PlannedRestScalarKind`
  (`src/include/duckdb_api/planned_protocol_operation.hpp:26`, used for
  REST query-binding/predicate-equality plumbing), `PlannedColumnScalarKind`
  (`src/include/duckdb_api/scan_plan.hpp:177`, deliberately protocol-neutral
  per its own doc comment, used only for column classification — verified
  by review to have exactly one consumer, confirming the separation is
  deliberate and correct, not a smell; keep both enums independent rather
  than consolidating), and `RequestedPredicateValueKind`
  (`src/include/duckdb_api/relational_predicate.hpp:16`, the DuckDB-facing
  predicate-value-kind enum `ColumnTypeMatches`/`TypedLiteralMatches`
  classify against and that Query Experience's `complex_filter_adapter.cpp`
  also populates directly — see that team's Shared interfaces entry). Every
  switch over any of the three needs an arm: `PlanScalarKind` (two
  independent copies, `src/semantics/predicate_classifier.cpp:34-45` and
  `src/semantics/rest_operation_planner.cpp:11-21`), `PlanTypedEquality`,
  `ColumnTypeMatches`, `TypedLiteralMatches`, `SameTypedLiteral`
  (`predicate_classifier.cpp`), the two `PlanScalar(...)` overloads and
  `Encode(...)` (`rest_operation_planner.cpp`), `PlannedColumn::ScalarKind()`-
  producing switch (`src/semantics/scan_plan.cpp:353-363`),
  `TypesAgree`/two unnamed helpers/accessor guards (`src/semantics/
  input_resolution.cpp`), `RestScalarKindName`
  (`src/semantics/scan_plan_explain.cpp:199-209`), and
  `PlannedEqualityPredicate`'s constructor (`src/semantics/scan_plan.cpp:
  27-70`), which structurally enforces "exactly one active field" per kind
  and needs the same extension the Connector-layer `CompiledScalarValue`
  constructor needs. `scan_planner_validation.cpp:28`'s plain boolean gate
  over accepted `logical_type` strings also needs `"DOUBLE"` added.
- **Remote Runtime:** `src/runtime/decoding/json_decoder.cpp` gains a
  `ParseDouble` function structurally parallel to the existing
  `ParseBigInt` (both already reuse the same `ParseNumberToken()` JSON-
  number tokenizer) but *without* `ParseBigInt`'s explicit `.`/`e`/`E`-
  rejection (line ~654) and using `strtod` instead of `strtoll` for the
  final conversion, with its own overflow handling (`strtod` signals
  overflow via `ERANGE`/`HUGE_VAL`, which this RFC treats as a rejection,
  not a silent clamp — consistent with `BIGINT`'s existing `ERANGE`
  rejection). The `ParsedSlot` struct (`json_decoder.cpp:135-144`) gains a
  `double_value` field; both kind-dispatch switches (`ParseColumnValue` and
  `ParseRecord`) gain a `DOUBLE` arm, the latter needing no VARCHAR-style
  memory-budget accounting since `DOUBLE` is fixed-size, matching
  `BOOLEAN`/`BIGINT`'s existing budget-free treatment. `ValueKind`
  (`src/include/duckdb_api/execution.hpp:68`) gains `DOUBLE`; `TypedValue`
  (same file, `:76-93`) gains a `double_value` field and a `Double()`
  factory. `TryColumnKind` (both overloads, `src/runtime/execution/
  rest_request_materialization.cpp`) and the `rest_relational_admission.cpp`
  ternary-chain field-building sites need `DOUBLE` arms/branches.
  `EncodeCompiledQueryScalar` and `rest_operation_planner.cpp`'s `Encode`
  must stay in lockstep (an existing compiled-package invariant already
  asserted at two call sites) — this RFC's canonical-encoding rule applies
  identically to both.
- **Query Experience:** `src/query/duckdb/typed_value_adapter.cpp`'s
  `LogicalTypeForKind` gains a `duckdb::LogicalType::DOUBLE` arm (DuckDB's
  own native double type, already fully supported by DuckDB — no new
  DuckDB-side capability needed) and `DuckdbValue` gains a
  `duckdb::Value::DOUBLE(value.double_value)` arm (DuckDB provides this
  constructor natively). `ValueKindForScalarKind`, `RegistrationLogicalType`
  and `ExplicitInputValue` (`src/query/duckdb/
  generated_relation_adapter.cpp`), and `RenderDefault`
  (`src/query/duckdb/package_introspection_functions.cpp`) each need a
  `DOUBLE` arm. **Corrected during Query Experience review (was flagged as
  an open item; now confirmed required):**
  `src/query/duckdb/complex_filter_adapter.cpp` — DuckDB-side predicate-
  pushdown constant extraction — has two functions that hardcode exactly
  the existing three kinds and **must** gain a fourth arm each:
  `RequestedType` (an if-chain testing a DuckDB `LogicalType` against
  `BIGINT`/`VARCHAR`/`BOOLEAN`, returning `false` — i.e., "not pushable" —
  for anything else) and `RequestedLiteral` (a `switch` over
  `RequestedPredicateValueKind`, the *third* scalar-kind-shaped enum this
  RFC's research had not separately named, declared
  `src/include/duckdb_api/relational_predicate.hpp:16` and populated by
  this same file). Without these two arms, a `DOUBLE` predicate would still
  produce a *correct* result (DuckDB falls back to evaluating it as a
  residual filter locally) but would **silently never push down** to the
  remote API — defeating the specific product-manager-selected scope of
  this RFC (predicate/conditional-input equality pushdown) without any
  visible failure. This is why Acceptance and verification below requires
  an explicit pushdown-proof test, not only a correctness test.

### Operational behavior

No new resource, cancellation, or backpressure model — a `DOUBLE` value is
fixed-size (8 bytes), matching `BOOLEAN`/`BIGINT`'s existing budget-free
decode treatment; no new per-value byte budget is needed the way `VARCHAR`
requires. No new credential, network-policy, or pagination authority is
introduced; this RFC is scoped entirely to scalar value representation.

## Topology impact

| Team | Role in this RFC | Interface or charter impact | Interaction mode | Exit condition |
| --- | --- | --- | --- | --- |
| Connector Experience | Sponsor and schema/compiler owner | New `DOUBLE` schema enum member (three sites), `CompiledScalarType::DOUBLE`, `CompiledScalarValue` extension, fix to two silent-fallthrough type-name sites, new required fixture-coverage variants | X-as-a-Service (existing) | Package authors can declare `DOUBLE` and get the same diagnostic quality as `BIGINT`/`VARCHAR` |
| Relational Semantics | Required implementation participant, not a passive reviewer | Three independent scalar-kind-shaped enums and roughly a dozen switches/accessors across predicate classification, typed equality, and column classification all require a `DOUBLE` arm; `PlannedEqualityPredicate`'s structural one-active-field constructor invariant extends; equality comparison is a direct normalized-`double ==`, not a string encoding (corrected during review) | Collaboration | A property test proves `DOUBLE` participates correctly in the existing typed-equality predicate machinery (construction, matching, and non-match) under the same fixture shape used for `BIGINT`/`VARCHAR` |
| Remote Runtime | Required implementation participant | New `ParseDouble` decoder path (reusing the existing JSON-number tokenizer) that distinguishes true overflow from benign underflow (corrected during review), `ParsedSlot`/`TypedValue`/`ValueKind` extensions, `TryColumnKind` and admission-site extensions; canonical-encoding parity with Connector Experience's encoder | Collaboration | The decode/encode round-trip fixture set (representative values including a value requiring the full 17 significant digits, a negative value, zero, and a subnormal-underflow value that must be accepted, not rejected) passes byte-exact |
| Query Experience | Required implementation participant | `LogicalTypeForKind`/`DuckdbValue`/`RegistrationLogicalType`/`ExplicitInputValue`/`RenderDefault` each require a `DOUBLE` arm; **`complex_filter_adapter.cpp`'s `RequestedType`/`RequestedLiteral` confirmed required (not merely "checked"), or `DOUBLE` predicates silently never push down** (corrected during review) | Collaboration (confirmed required, not X-as-a-Service-only) | A test proves a `DOUBLE` predicate actually pushes down to the remote request (not merely evaluates correctly via DuckDB's residual-filter fallback), plus a DuckDB-level round-trip test for zero, a negative value, and a 17-significant-digit value |
| Engineering Enablement | Facilitator | Helps establish the fixture-coverage variant set for the new type, distinguishing it from `BIGINT`'s integrality-focused set | Facilitation | Connector Experience, Remote Runtime, Relational Semantics, and Query Experience maintain the corrected oracle independently |

Cognitive load is genuinely distributed across four teams, each extending
several sites it already owns by direct analogy to how `BIGINT`/`VARCHAR`
already work — not a new pattern for any of them, but a materially larger
number of sites than a typical single-strategy addition, because of the
parallel-fields-not-tagged-union structural fact this RFC documents.

## Correctness, security, and lifecycle analysis

- **Relational semantics and conservative fallback:** Affected.
  `PlannedEqualityPredicate`'s constructor enforces "exactly one active
  scalar field" per kind — this structural invariant extends to `DOUBLE`
  exactly as it already does for the other three kinds, not a new kind of
  invariant. Predicate-mapping/typed-equality classification
  (`ColumnTypeMatches`, `TypedLiteralMatches`, `SameTypedLiteral`) must
  correctly recognize a `DOUBLE` column/literal pairing the same way it
  recognizes the other three; no change to accuracy classification
  (`EXACT`/`SUPERSET`) or occurrence-preservation semantics is needed, since
  those are orthogonal to which scalar kind is being compared.
- **Authentication, credentials, network policy, and privacy:** Not
  affected. No credential or network-authority change.
- **Resource budgets, backpressure, and cancellation:** Not affected beyond
  what's stated in Operational behavior — `DOUBLE` is fixed-size, no new
  budget category.
- **Replay, retries, caching, and duplicate prevention:** Not affected.
- **Concurrency, immutability, and state ownership:** Not affected; scalar
  values remain immutable, copy-only data, identical to the existing three
  kinds.
- **FFI, initialization, reload, shutdown, and failure containment:** Not
  affected.
- **Diagnostics, redaction, metrics, and progress reporting:** The two
  pre-existing silent-fallthrough hazards (`ScalarType()`,
  `CompileConcreteScalar`) currently mean an author who mistypes a scalar
  type string (e.g. `type: Boolean` with wrong case, or any future typo)
  gets silently miscompiled as `VARCHAR` rather than a diagnostic — this is
  a real, if narrow, existing correctness gap for the other three types
  that fixing the fallthrough (required to recognize `DOUBLE` correctly)
  also closes as a side effect. No new redaction concern: `DOUBLE` values
  are not credentials and follow the same diagnostic/EXPLAIN/digest
  treatment as `BIGINT`.

## Compatibility and migration

Additive only for every *currently valid* package: no existing accepted
package (`connectors/github`, `connectors/rickandmorty`) changes behavior or
requires a version bump, since neither declares an unrecognized scalar-type
string today. **Disclosed separately (per Connector Experience review):**
the bundled fix to `ScalarType`/`CompileConcreteScalar`'s silent-fallthrough
behavior is a real, if narrow, diagnostic-behavior change for a
*hypothetical* package that already has a mistyped or unrecognized `type:`
value — such a package was previously silently miscompiled as `VARCHAR`
with no diagnostic, and will now correctly fail closed with a `SCHEMA`-phase
diagnostic instead. No currently-accepted package is affected by this,
since none has such a value; a future author who previously relied on
(undocumented, unintended) silent-VARCHAR behavior would see a new
compile-time failure, which this RFC judges to be a correctness improvement
disclosed here, not a compatibility break of any documented behavior.

Whether either existing package could benefit from a `DOUBLE` column (for
example, if either API happens to expose a numeric field currently
represented some other way) is a separate, later decision for that
package's own version bump under RFC 0013's compatibility table — not
decided here.

**Implementation-time refinement:** direct inspection at implementation time
found that `ScalarType`/`CompileConcreteScalar`'s fallthrough is already
unreachable for a genuinely unrecognized type string in the live compile
path — `package_relation_schema.cpp`'s `DecodeColumn`/`DecodeInput`,
`package_predicate_schema.cpp`'s `DecodePredicateSchema`, and
`package_fixture_index.cpp`'s fixture-predicate-literal decoder each already
fail closed with an `INVALID_TYPE` diagnostic *before* `ScalarType`/
`CompileConcreteScalar` are ever reached, and the compiler's diagnostics-
revision check discards any result built after a diagnostic fires. Making
`ScalarType`/`CompileConcreteScalar` themselves throw would be inconsistent
with this codebase's established diagnostic-accumulation pattern (collect a
diagnostic, continue with a placeholder value, discard the final result) and
risks crashing mid-compile instead of gracefully continuing to collect
further diagnostics. This RFC's implementation therefore only adds the
`DOUBLE`-recognizing branch to both functions and to the five upstream
fail-closed validation sites that gate them; it does not additionally change
`ScalarType`/`CompileConcreteScalar`'s own fallback behavior, since doing so
would not close any live gap and could introduce a new one.

**Implementation-time refinement (GraphQL isolation):** GraphQL relations
render and decode through a completely separate `CompiledGraphqlScalarKind`
enum (`STRING`, `INT64`, `BOOLEAN`) with no Float equivalent — a fact not
surfaced by the original RFC draft or its review, since neither `connectors/
github`'s nor `connectors/rickandmorty`'s GraphQL operations declare a
numeric-adjacent column that would have exposed it. Left unaddressed, a
`type: DOUBLE` GraphQL column would have silently fallen through
`package_graphql_renderer.cpp`'s existing default-to-`STRING` mapping and
decoded as a string with no diagnostic — a genuine correctness bug, not a
documented limitation. Building full GraphQL Float support is a separate,
larger undertaking outside this RFC's scope (its own document/variable/
response-path plumbing, mirrored across two protocol renderers instead of
one). Implementation instead added an explicit compile-time rejection of
`type: DOUBLE` on a GraphQL relation column, with a precise diagnostic at
the column's declared type mark, mirroring RFC 0018's established
precedent of rejecting `api_key`+GraphQL rather than a silent runtime
mismatch. `DOUBLE` therefore remains REST-only in this first cut; GraphQL
Float support, if ever needed, is a distinct follow-on RFC.

## Evidence and bounded trials

| Question or claim | Evidence required | Method or fixture | Result and limitations |
| --- | --- | --- | --- |
| DuckDB natively supports a `DOUBLE`/`Value::DOUBLE(...)` vector type, so Query Experience's implementation is a pure additive arm, not new DuckDB-side capability | Direct code inspection | Read `duckdb/src/include/duckdb/common/types.hpp` (or equivalent) for `LogicalTypeId::DOUBLE` and `Value::DOUBLE` | Confirmed: DuckDB's own type system already has a native double-precision type; `src/query/duckdb/typed_value_adapter.cpp`'s existing switches over `ValueKind`/`PlannedColumnScalarKind` are the only sites needing a new arm. |
| The JSON tokenizer already rejects non-finite bare literals (`NaN`/`Infinity`) before any per-column type dispatch, so `DOUBLE` needs no special non-finite handling | Direct code inspection | Read `src/runtime/decoding/json_decoder.cpp`'s `SkipValue`/tokenizer dispatch | Confirmed: the tokenizer only recognizes tokens starting with `"`, `-`/digit, `t`, `f`, `n`, `[`, `{` — a bare `NaN`/`Infinity` literal (not valid JSON) fails as `MalformedJson()` at the tokenizer level, independent of column type. |
| No existing struct in the pipeline uses a fixed-size array, bitfield, or other structural representation that a 4th scalar kind would overflow or corrupt | Direct code inspection | Grep the whole `src/` tree for array/bitfield patterns sized to 3 | Confirmed: every value-carrying struct uses parallel named fields (`boolean_value`/`bigint_value`/`varchar_value`), not an array or bit-packed representation; no structural ceiling exists, only a larger-than-usual number of independent sites needing an added field. |
| `%.17g`-style fixed-precision encoding round-trips any IEEE-754 double exactly | Well-established numerical result (Steele & White, 1990), not requiring a new bounded trial | N/A — cited numerical analysis result, portable via `snprintf`/C++11 standard library | Accepted as a standard, well-known guarantee; implementation must still add a fixture proving round-trip exactness for representative boundary values (largest/smallest normal, a value needing all 17 digits, zero, negative zero). |
| `strtod`'s `ERANGE` signals both true overflow and benign underflow, so a blanket "reject on `ERANGE`" rule (mirroring `BIGINT`) would be wrong for `DOUBLE` | Direct empirical test of this platform's libc | Compiled and ran a standalone `strtod` probe across overflow/underflow/subnormal/exact-zero inputs | **Confirmed during Remote Runtime review**: `strtod` sets `errno=ERANGE` for true overflow (`1e400` → `HUGE_VAL`) and equally for benign underflow (`4.9e-324`, the smallest subnormal, and `1e-325`, which underflows to exact `0.0`). `ParseDouble` must reject only `result == HUGE_VAL \|\| result == -HUGE_VAL`, not `errno == ERANGE` alone — corrected from the original draft's blanket rule. |
| DuckDB's `complex_filter_adapter.cpp` (predicate-pushdown constant extraction) does or does not need a `DOUBLE`-specific arm | Was not yet resolved by this RFC's original research | Direct code inspection during review | **Resolved during Query Experience review, decision-critical, now confirmed required**: `RequestedType` and `RequestedLiteral` both hardcode exactly the existing three kinds and must each gain a `DOUBLE` arm, or `DOUBLE` predicates silently never push down (they still evaluate correctly via DuckDB's residual-filter fallback, so no incorrect result occurs — but the specific product-manager-selected scope of this RFC, pushdown, would be silently unmet). Moved from "open, non-decision-critical" to required scope. |

## Alternatives considered

1. **Add `DOUBLE`, scoped to output-column/relation-input only, deferring
   predicate equality (originally recommended by the lead agent).**
   Benefit: narrower surface, avoids touching the predicate/typed-equality
   machinery at all in this first cut. Drawback: the product manager
   explicitly chose to include predicate/conditional-input equality now
   rather than defer it (2026-07-22) — see the linked goal's Reserved
   product decisions. Not selected.
2. **Add `DOUBLE` with predicate equality, using exact canonical-form
   comparison (proposed).** Benefit: covers the full success-signal scope
   the product manager selected, with an equality rule that is a direct,
   unsurprising analogy to how `BIGINT`/`VARCHAR` already compare — no new
   tolerance concept invented. Drawback: touches roughly a dozen sites
   across four teams, the largest single-type addition attempted so far in
   this project's history.
3. **Add `DOUBLE` with epsilon-tolerant equality.** Benefit: might match
   author intuition better in some cases (e.g., two independently-computed
   floating-point values that "should" be equal but differ in the last
   bit). Drawback: requires inventing and justifying a specific tolerance
   value with no principled default, and no real connector today needs
   this; explicitly rejected as unnecessary invented policy per the goal's
   guardrails.
4. **Add a general `DECIMAL(width, scale)` fixed-point type instead of
   `DOUBLE`.** Benefit: exact decimal arithmetic, no floating-point
   surprises at all. Drawback: a materially different declaration model
   (author must declare width/scale per column), a different wire encoding
   question (JSON has no native decimal type either — every real API
   already emits IEEE-754-representable numbers), and no evidence any real
   connector needs exact decimal semantics rather than double precision.
   Explicitly out of scope per the goal's guardrails; a candidate follow-on
   if real evidence emerges.
5. **Retain current behavior (no new scalar type).** Rejected: leaves a
   widely-blocking, now-evidenced gap undocumented rather than deciding it.

## Drawbacks and failure modes

- This is the largest single-type addition attempted in this project's
  history: roughly a dozen independent switch/accessor sites across four
  teams, plus a `double_value` field and inactive-payload-invariant update
  to seven separate parallel-fields struct definitions (`CompiledScalarValue`,
  `ParsedSlot`, `PlannedEqualityPredicate`, `PlannedRestQueryBinding`,
  `PlannedScalar`, `ExplicitInput`, `RequestedPredicateValue`, `TypedValue`
  — eight, not seven, once precisely counted). Omitting any one struct's
  field/invariant update, or any one switch's arm, fails closed today
  (every switch found during research already throws/returns false on an
  unhandled value) rather than silently misbehaving — but the sheer count
  of sites is itself the primary implementation risk this RFC surfaces.
- The two pre-existing silent-fallthrough hazards
  (`ScalarType`/`CompileConcreteScalar`) are a real, if narrow, defect this
  RFC's implementation must fix as a precondition for `DOUBLE` working
  correctly at all — an author who already has a typo in an existing
  `type:` field today gets silently miscompiled as `VARCHAR`; fixing this
  is in scope, not a bundled unrelated change, but should be called out
  explicitly in the implementation commit as behavior-affecting for the
  existing three types too (a mistyped type string that used to silently
  become `VARCHAR` will now correctly fail closed).
- `%.17g`-style encoding is not the shortest possible round-trip
  representation (a shortest-round-trip algorithm like Ryu would produce a
  more human-readable canonical form, e.g. `1.1` instead of
  `1.1000000000000001`) — acceptable since the canonical form is a
  wire/comparison representation, not primarily an author-facing display
  value, but worth disclosing as a legibility tradeoff in `EXPLAIN` output
  and diagnostics.
- **Resolved during review, recorded here as a confirmed required site,
  not a residual drawback:** `complex_filter_adapter.cpp`'s `RequestedType`/
  `RequestedLiteral` must each gain a `DOUBLE` arm (see Evidence table) —
  omitting either would not cause an incorrect result (DuckDB's residual-
  filter fallback keeps results correct) but would silently defeat this
  RFC's specific predicate-pushdown scope with no visible failure, which is
  why Acceptance and verification requires an explicit pushdown-proof test
  rather than trusting correctness-only evidence.

## Acceptance and verification

- **End-to-end demonstration:** A new fixture relation (or an extension of
  an existing one) declares a `DOUBLE` column and a `DOUBLE` conditional
  input used in a predicate mapping, and both a plain scan and a predicate-
  filtered scan return correct, exactly round-tripped values through the
  existing `duckdb_api_load_connector` path.
- **Automated oracle:** schema/compiler fixture-coverage variants mirroring
  `BIGINT`'s existing `{minimum, maximum, underflow_rejected,
  overflow_rejected, fraction_rejected}` set, adapted for `DOUBLE`'s
  different failure shape (corrected during Engineering Enablement
  review): `fraction_rejected` is dropped (`DOUBLE` accepts fractions);
  `underflow_rejected` is **not** added (underflow to a subnormal or exact
  zero is a legitimate, accepted result per the Remote Runtime-corrected
  decode rule, not an error); `overflow_rejected`'s analog is renamed
  `magnitude_overflow_rejected` (a JSON-syntactically-valid number, e.g.
  `1e400`, too large to represent as any finite double); round-trip-
  boundary variants are added (zero, negative zero normalized to zero, a
  value requiring all 17 significant digits, a subnormal-underflow value
  that must be *accepted*). A Relational Semantics property test proves
  `DOUBLE` participates correctly in `PlannedEqualityPredicate`
  construction, matching, and non-match under the same fixture shape used
  for `BIGINT`/`VARCHAR`, using direct normalized-`double` equality (not a
  string encoding). A Query-owned test proves DuckDB vector round-trip
  exactness for the same boundary-value set through real `duckdb_api_scan`
  execution, not merely an internal function call. **Two additional
  required layers, corrected during review:** a real predicate-pushdown
  test proving a `DOUBLE` conditional-input equality predicate actually
  reaches the remote request (not merely evaluates correctly via DuckDB's
  residual-filter fallback — the specific gap `complex_filter_adapter.cpp`'s
  missing arms would otherwise leave silently unmet); and a concrete test
  asserting the literal `"DOUBLE"` string in real DuckDB `EXPLAIN` output
  (mirroring the four-layer coverage discipline — end-to-end, schema/
  compiler fixtures, property test, real-`EXPLAIN` test — RFC 0019's Query
  Experience review established, after that RFC found an identical claimed-
  but-untested `EXPLAIN` fact for `response_next`).
- **Quality gates:** `make build`, `make test`, `make demo`; the existing
  package/fixture/coverage gates extended to the new type;
  `scripts/verify-source-identities.py`, `scripts/verify-public-surface-inventory.py`,
  `scripts/verify-contract-freeze.py` and their tests.
- **Independent review:** `$topology-consult` review from all five
  required reviewers, recorded below; `$adversarial-review` during
  implementation with at least two perspectives, given the breadth of the
  parallel-fields struct surface this RFC touches, per `AGENTS.md`.
- **Interaction exit:** every team's extended sites pass the round-trip
  and typed-equality fixture set without any other team needing
  provider-internal knowledge of how `DOUBLE` is represented.

## Contract propagation

| Source of truth or artifact | Impact | Required update | Completion evidence |
| --- | --- | --- | --- |
| `docs/ARCHITECTURE.md` | Not affected | No architectural-invariant change | Not applicable |
| `docs/CONNECTOR_SPECIFICATIONS.md` | Affected | Add `DOUBLE` to the scalar-type grammar, its decode/encode rule, and its predicate-equality eligibility | Pending implementation |
| `docs/RUNTIME_CONTRACTS.md` | Affected | Document `ParseDouble`'s decode rule and the canonical `%.17g`-style encoding/equality rule | Pending implementation |
| `docs/TEAM_TOPOLOGY.md` and active charters | Not affected | No interface or accountability boundary moves | Not applicable |
| `docs/PRODUCT_DELIVERY.md`, `AGENTS.md`, and skills | Not affected | No change | Not applicable |
| `ROADMAP.md` | Affected | Record the new scalar type under the appropriate upcoming minor version | Pending implementation |
| `release/1.0.0/freeze.json` and `release/1.0.0/freeze.md` | Affected | **Confirmed during Engineering Enablement review:** `freeze.json` tracks no scalar-type-set today (only `pagination_strategies` exists as this kind of tracked closed set); this RFC introduces a new tracked set for the first time, rather than extending an existing one. Also confirmed: no `exclusions`/`MANDATORY_EXCLUSIONS` entry pre-names a numeric/decimal/floating-point gap, unlike pagination and credential kinds — this is a genuinely new decision, not a pre-scoped one | Pending implementation, with mutation coverage in `test/python/contract_freeze_tests.py` for the newly introduced tracked set |
| Examples, diagnostics, fixtures, and tests | Affected | New fixture-coverage variant set, typed-equality property test, DuckDB vector round-trip test | Pending implementation |

## Unresolved questions

- Non-blocking: whether `complex_filter_adapter.cpp` needs a `DOUBLE`-
  specific arm — resolved during implementation, not decision-critical to
  this RFC's acceptance (see Evidence and bounded trials).
- Non-blocking: exact required-fixture-coverage variant names for
  `DOUBLE` — cosmetic naming, resolved at implementation time following
  Engineering Enablement's review, per RFC 0016's precedent for this class
  of detail.
- Non-blocking: whether `release/1.0.0/freeze.json` already tracks a
  scalar-type-set the way it tracks `pagination_strategies`, or whether
  this RFC introduces that tracking for the first time — resolved during
  implementation; either way, `DOUBLE` must be reflected in whatever
  freeze authority governs the closed scalar-type set.

## Review record

| Required reviewer | Team | Result | Evidence or objection | Disposition by decision owner |
| --- | --- | --- | --- | --- |
| Connector Experience perspective | Connector Experience | Objected (Needs evidence) | Confirmed the three-way inline schema enum duplication and all claimed switch/accessor sites. Central finding: the RFC's original citation for `ScalarType`'s implementation was wrong (it cited the header declaration, `package_model_compiler_internal.hpp:65-73`, not the real implementation at `package_compile_helpers.cpp:65-73`). More substantively: the RFC's original framing — that fixing the silent-VARCHAR-fallthrough behavior is purely a consequence of "recognizing DOUBLE" — doesn't hold; recognizing a new string only needs one added branch, not a change to the fallback path, so the fail-closed fix is a separate, real diagnostic-behavior change deserving its own disclosure, not an automatic side effect. | Accepted; RFC's Shared interfaces section corrected with the right citation, and the fail-closed fix reframed as an explicitly disclosed, separately-decided change, with its compatibility effect stated precisely in Compatibility and migration (no currently-accepted package affected, since none has an unrecognized type string today). |
| Query Experience perspective | Query Experience | Objected (Needs evidence) | Confirmed DuckDB's native `LogicalTypeId::DOUBLE`/`Value::DOUBLE(...)` exist and are first-class, widely-used types, and confirmed all five claimed `typed_value_adapter.cpp`/`generated_relation_adapter.cpp`/`package_introspection_functions.cpp` sites. Central finding: directly investigated `src/query/duckdb/complex_filter_adapter.cpp` (an item the RFC had left as "unconfirmed") and found decision-critical evidence — `RequestedType` and `RequestedLiteral` both hardcode exactly the existing three kinds; without a `DOUBLE` arm in each, a `DOUBLE` predicate would still produce a correct result via DuckDB's residual-filter fallback but would silently never push down to the remote request, defeating this RFC's specific product-manager-selected scope with no visible failure. Also corrected the RFC's "two independent planned enums" framing: a third scalar-kind-shaped enum, `RequestedPredicateValueKind`, is what `ColumnTypeMatches`/`TypedLiteralMatches` and `complex_filter_adapter.cpp` actually operate on. | Accepted; RFC's Shared interfaces and Topology impact sections revised to move `complex_filter_adapter.cpp`'s two arms into confirmed required scope (Collaboration, not X-as-a-Service-only), the "two enums" framing corrected to three, and Acceptance and verification revised to require an explicit pushdown-proof test, not only a correctness test. |
| Remote Runtime perspective | Remote Runtime | Objected (Needs evidence) | Confirmed every cited `json_decoder.cpp` line number and the tokenizer's non-finite-literal rejection claim. Central finding, empirically verified by compiling and running a standalone `strtod` probe on this platform's libc: `strtod` sets `errno=ERANGE` for true overflow (`1e400` → `HUGE_VAL`) **and equally** for benign underflow to a subnormal or exact zero (`4.9e-324`, `1e-325`) — the RFC's original blanket "`ERANGE` means reject, consistent with `BIGINT`" rule would incorrectly reject a legitimate tiny measurement, directly undermining the RFC's own motivating examples. Also flagged `strtod`'s `LC_NUMERIC` decimal-point sensitivity as a low-severity, fail-safe-by-construction assumption worth disclosing. | Accepted; RFC's Public behavior section revised with a corrected `ParseDouble` rule (reject only `result == HUGE_VAL \|\| result == -HUGE_VAL`; accept underflow as a legitimate result), and the locale assumption disclosed in Drawbacks. This was the most consequential correction — the original rule would have silently broken a real, evidenced use case (small measurements) this RFC exists to unblock. |
| Relational Semantics perspective | Relational Semantics | Objected (Unsound) | Confirmed `PlannedEqualityPredicate`'s one-active-field constructor invariant and every claimed switch site. Confirmed the two "planned" enums (`PlannedRestScalarKind`, `PlannedColumnScalarKind`) are a deliberate, correct separation (verified each has genuinely distinct, non-overlapping consumers) — not a design smell, keep independent. Central finding: the RFC's "Equality semantics" design was factually wrong — `SameScalarValue`/`SameScalar`/`SameTypedValue`/`SameTypedLiteral` do **not** compare via a stored canonical `encoded_value` today (no such field exists on `CompiledScalarValue`/`PlannedEqualityPredicate`); every one already compares raw decoded values directly via `==`. Proposed the correct, simpler, precedent-consistent fix: since `-0.0` is normalized at construction and non-finite values can't reach construction, `DOUBLE` equality should be a direct normalized-`double ==` comparison in all five functions — no string encoding involved in comparison at all. | Accepted; this was the second most consequential correction. RFC's Public behavior "Equality semantics" subsection rewritten to specify direct `==` comparison (the `%.17g` encoder remains, but strictly for wire encoding, never for comparison), correcting the false precedent claim and simplifying the design. |
| Engineering Enablement perspective | Engineering Enablement | Objected (Needs evidence) | Confirmed `BIGINT`/`VARCHAR`'s exact existing fixture-coverage sets, confirmed `release/1.0.0/freeze.json` tracks no scalar-type-set today (this RFC introduces new tracking, not an extension), and confirmed no exclusion pre-names this gap (unlike pagination/credentials). Identified two gaps: (1) a real, reachable `DOUBLE`-specific overflow category (a JSON number too large for even a double, e.g. `1e400`) was missing a named fixture variant, while the RFC's proposed vague "non-finite" category was actually vacuous (already proven unreachable by the RFC's own tokenizer evidence) — and conversely warned against inventing an `underflow_rejected` analog, since underflow is legitimate, not an error; (2) the RFC's Acceptance and verification section omitted a concrete real-`EXPLAIN` test for the literal `"DOUBLE"` string, repeating the exact gap RFC 0019's own Query Experience review found and fixed for `response_next`. | Accepted; RFC's Acceptance and verification section revised to rename the overflow category `magnitude_overflow_rejected`, explicitly omit an underflow-rejected category with rationale, and add a required real-`EXPLAIN` test as a fourth coverage layer, mirroring RFC 0019's precedent. |

All five required reviewers returned an objection, each identifying a real,
independently-verified technical gap or error rather than a style
preference — the highest correction rate of any RFC in this project's
history, consistent with this being the largest single-type addition
attempted. Two corrections were decision-critical to the design's actual
soundness (the `strtod` overflow/underflow rule, which would have silently
broken the RFC's own motivating use case, and the false equality-semantics
precedent claim, which pointed at a design that didn't match how the
codebase actually works). No reviewer objected to the underlying decision
(add `DOUBLE`, include predicate-equality pushdown per the product
manager's locked-in scope) as unsafe or contract-violating — every
objection was about the technical design's completeness or correctness,
each directly and fully corrected in this revision.

## Decision and rationale

- **Technical decision owner:** Lead agent.
- **Product approval:** Received — Nic Galluzzo, 2026-07-22. The one
  reserved product decision this RFC depends on (predicate/conditional-
  input equality scope: included now, not deferred, the broader of the two
  options the lead agent presented) was recorded in
  `docs/goals/double-scalar-type.md`'s Reserved product decisions section.
- **Decision:** **Accepted.** `DOUBLE` is added to `duckdb_api/v1`'s closed
  scalar-type set, with the corrected design from the review record above:
  direct normalized-`double` equality comparison (not a string encoding) in
  every typed-equality function; a `ParseDouble` decode rule that rejects
  only true overflow, not benign underflow; `complex_filter_adapter.cpp`'s
  two pushdown arms as confirmed required scope with an explicit pushdown-
  proof test; the fail-closed fallthrough fix as an explicitly disclosed,
  separately-reasoned decision; and a corrected fixture-coverage set
  (`magnitude_overflow_rejected` in, no invented `underflow_rejected`, plus
  a required real-`EXPLAIN` test).
- **Rationale:** The core decision — add one new IEEE-754 double-precision
  scalar type, following the existing parallel-fields-per-struct
  convention exactly, with full predicate-equality pushdown per the
  product manager's explicit choice — is sound and unobjected-to by any
  reviewer. What five-for-five objections corrected was the technical
  execution of that decision: two corrections (the `strtod` overflow rule
  and the equality-semantics precedent) were significant enough that, left
  unfixed, they would have shipped a `DOUBLE` implementation that either
  silently broke on legitimate small values or rested on a materially
  incorrect understanding of how the existing codebase already compares
  typed values. This mirrors RFC 0016's, RFC 0018's, and RFC 0019's
  pattern: substantive independent review sharpens and corrects a sound
  decision rather than blocking it, and the size of this RFC's surface
  (the largest scalar-type addition attempted) is exactly why it drew the
  highest correction rate of the four RFCs decided so far.
- **Material objections:** All five objections (Connector Experience,
  Query Experience, Remote Runtime, Relational Semantics, Engineering
  Enablement) were dispositioned by directly revising the RFC's technical
  content — see the Review record table above for each disposition. None
  was rejected; none required returning to Draft, since every objection's
  evidence was immediately actionable within this RFC's own already-decided
  scope rather than requiring a new, separate decision.
- **Superseded by:** Not applicable.

## Follow-on goals

| Outcome or objective | Accountable team | Supporting teams and interaction modes | Activation condition |
| --- | --- | --- | --- |
| Implement `DOUBLE` (schema, compiler, compiled IR, Relational Semantics typed-equality machinery, Remote Runtime decode/encode, Query Experience vector conversion, corrected fixture-coverage set) | Connector Experience | Remote Runtime (Collaboration), Relational Semantics (Collaboration), Query Experience (X-as-a-Service/Collaboration for the `complex_filter_adapter.cpp` check), Engineering Enablement (Facilitation) | This RFC accepted; `docs/goals/double-scalar-type.md` activated |
| Add a `DECIMAL(width, scale)` fixed-point type | Connector Experience | Remote Runtime, Relational Semantics, Query Experience | Not activated by this RFC — explicitly deferred pending a real connector author's need |
| Add a declared floating-point equality-tolerance policy | Connector Experience | Relational Semantics | Not activated by this RFC — explicitly deferred pending a real connector author's need; exact canonical-form comparison is the shipped design |

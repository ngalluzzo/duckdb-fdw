# Relational Semantics plan: predicate-selective repository scans

## Outcome interpretation and authority

Status: **Delivered; provider interactions and semantic gates satisfied**.

Relational Semantics supports Query Experience's product outcome by turning one
closed structured predicate on `github.authenticated_repositories` into a
complete, immutable, explainable `ScanPlan` that may narrow remote work without
changing DuckDB's row bag. The supported slice is exactly the required VARCHAR
column predicate `visibility = 'private'`. The same REST repository
`visibility` field supplies the output value, and the mapping binds only that
predicate to the fixed `visibility=private` request input.

The broader `private = TRUE` mappings to either `visibility=private` or
`type=private` remain rejected. GitHub treats internal as a distinct visibility
value while its broader private boolean may include internal repositories.
Projection, ordering, limit, offset, other predicate mappings, Runtime residual
evaluation, retries, caching, GraphQL, package loading, and connector authoring
remain outside this workstream.

The existing full-schema, unrestricted, DuckDB-owned plan is the safe baseline.
The optimized path must preserve authentication, exact origin, strict schema,
sequential mutable pagination, resource ceilings, cancellation, and error
behavior. Planning remains deterministic and performs no DuckDB callback,
secret lookup, environment access, filesystem access, or network I/O.

This plan allocates charter-owned work and evidence; RFC 0008 remains the
decision record. Production shared interfaces and behavior wait until that RFC
is Accepted.

## Closed predicate handoff

This goal does not need or authorize a general predicate algebra. The
protocol-neutral request handoff needs only two semantic states:

- unrestricted `TRUE` for the complete base domain; and
- the closed candidate `visibility = 'private'`.

Query constructs the candidate only from a bound equality whose depth-zero
column reference belongs to the current `LogicalGet`, resolves to the exact
required `visibility` VARCHAR column, and has the non-null VARCHAR literal
`private` on the other side. Either operand order is acceptable. Query does not
pass DuckDB expression objects, SQL text, request parameter names, or a partially
normalized predicate tree to Semantics.

If DuckDB presents that equality as an independent conjunct, Query may submit
the closed candidate while leaving every original filter in DuckDB. Query does
not descend through `OR` or `NOT`, erase `NULL`, fold unsupported branches, or
rewrite another comparison to discover the candidate. Different columns,
values, types, collations, casts, unresolved parameter expressions, ambiguous
bindings, and unsupported expression shapes produce no candidate and retain
the unrestricted plan.

The pinned DuckDB optimizer may substitute a bound parameter value before the
complex-filter callback, so `visibility = $1` can arrive as the same non-null
VARCHAR constant `private` as a literal predicate. Semantics may accept that
indistinguishable closed candidate: at that execution `D` is still exactly
`visibility = 'private'`, the same-field proof still establishes `D => R`, and
DuckDB still owns the retained predicate. Parameter provenance is not a third
request state. Query must rebuild refinement from the unrestricted request on
every prepared execution and rebind, so a prior `private` selection cannot
survive a later non-private, `NULL`, differently typed, or unsupported value.

The small request value must remain below Query request construction and
Semantics planning so the dependency graph has no cycle. Semantics owns its
meaning and snapshot; Query owns conversion from the pinned DuckDB surface.

## Implication and accuracy proof

Let `B` be the complete authenticated-repository base-domain bag under one
authorization and execution state, `D` be DuckDB's exact
`visibility = 'private'` predicate, and `R` be the bag returned by the same
operation with `visibility=private` on every page.

The pinned REST contract establishes the required `D => R` relationship:

1. the response repository field names visibility values `public`, `private`,
   and `internal`;
2. the endpoint input limits the same authenticated collection to the specified
   visibility and accepts `private`; and
3. therefore every valid base row for which the required decoded visibility is
   exactly `private` belongs in `R`. An internal row has the distinct value
   `internal`, does not make `D` true, and need not appear in `R`.

This goal still records `Superset`, not `Exact`, and retains the DuckDB
predicate. It does not rely on `R => D`, remove local evaluation, or promote an
observed exact result into a stronger contract. Required VARCHAR decoding stays
strict: a missing, null, or wrongly typed field is an error rather than a
coercion to a visibility value.

Semantics must reject any Connector declaration that maps the broader BOOLEAN
`private` column, another literal, another request input, or another operation.
Those forms are counterexamples, not additional classifier branches.

## Residual ownership and conservative fallback

This goal adds no Runtime predicate evaluator. DuckDB's complex-filter callback
does not erase the accepted expression, generic `filter_pushdown` remains
disabled, and DuckDB therefore evaluates the original predicate after the scan.
The plan records `visibility = 'private'` as the residual when that is the
complete retained filter. For a supported conjunct or unsupported structured
filter it records the opaque `complete_duckdb_filter` scope instead, so explain
never mistakes one candidate for the whole residual. `DUCKDB` remains the sole
owner. Runtime receives no expression or authority to re-evaluate, simplify,
or drop it.
Ordering remains in DuckDB, and no remote or Runtime limit or offset is added.

Fallback is an ordinary successful plan:

- the remote predicate is `TRUE` for the complete repository base domain;
- no predicate-derived `visibility=private` input enters the operation;
- DuckDB owns every unseen, unsupported, ambiguous, or unproven filter;
- the existing request, Link transitions, page and scan ceilings, and relation
  behavior remain unchanged; and
- explanation records a stable capability or mapping reason without SQL text,
  credentials, or response-derived values.

Missing adapter capability, absence of the closed candidate, or absence of the
mapping selects fallback. Contradictory column, type, operation, input,
authorization, or pagination metadata is rejected deterministically before I/O
rather than guessed into either path. If Query cannot prove that DuckDB retains
the predicate, Semantics also falls back.

## Plan authority and provider APIs

Semantics provides one complete `ScanRequest -> ScanPlan` construction path.
The immutable plan exposes:

- unrestricted or `visibility = 'private'` remote classification;
- `Unsupported` or `Superset` accuracy with a stable reason;
- the exact or opaque-complete DuckDB residual and its sole owner;
- one optional closed typed conditional input representing
  `visibility=private`; and
- the existing operation, pagination, authorization, network, and budget facts.

The typed conditional input is the only authority for predicate-derived request
selection. A raw encoded operation query field, explanation string, Connector
mapping, or DuckDB expression is not a second authority. Runtime admits the base
operation, conditional input, and pagination target into one immutable request
profile before authorization or I/O. Initial requests, continuation requests,
and Link validation consume that same profile without reclassifying relational
meaning.

The bounded service direction is:

```text
closed remote candidate + retained scope -> Query ScanRequest -> Semantics planner

Connector mapping facts ------------------------------^ |
                                                       | v
                                      immutable ScanPlan -> Runtime
```

Runtime must link only the plan-value service. It must not import
`ScanRequest`, Connector metadata, DuckDB filter types, planner-private headers,
or explanation parsing.

## Source and oracle ownership

Names are provisional until RFC acceptance, but responsibilities are fixed:

| Artifact | Relational Semantics responsibility |
| --- | --- |
| Closed predicate handoff | An unrestricted or `visibility = 'private'` remote candidate plus a separate unrestricted/exact/opaque-complete DuckDB residual scope, with typed identity and safe snapshots |
| Focused predicate classifier | Validate the one Connector mapping and Query candidate; emit Superset or fallback with stable reasons |
| `src/include/duckdb_api/scan_plan.hpp` | Immutable accuracy, remote restriction, residual owner, and typed conditional-input handoff |
| `src/semantics/scan_planner.cpp` | Compose validated Connector facts, one request, the closed classification, and the complete plan |
| `src/semantics/scan_planner_validation.cpp` | Reject contradictory column, type, mapping, operation, authorization, and pagination envelopes without inference from raw request strings |
| `src/semantics/scan_plan_explain.cpp` | Explain restriction, Superset accuracy, residual owner, typed input source, and fallback reason safely |
| `test/cpp/semantics/support/` | Valid visibility-Superset and unrestricted plan fixtures plus closed counterexamples for consumers |

Relational Semantics owns these focused oracle families:

- a closed classifier table covering the supported equality, wrong column,
  value, type, mapping, operation, capability, duplicate, conflict, and missing
  cases without Connector, Runtime, or adapter implementation sources;
- a row-bag property over public, private, and internal visibility values proving
  remote-plus-DuckDB-residual evaluation equals DuckDB-only evaluation;
- negative cases for `NULL`, missing and wrongly typed visibility, the broader
  private BOOLEAN, conjunction presentation, `OR`, `NOT`, unresolved
  parameters, and ambiguous bindings without implementing those forms in
  Semantics;
- prepared-parameter lifecycle cases that alternate `private`, public,
  internal, `NULL`, differently typed, and unsupported values across repeated
  and concurrent executions, proving per-execution rebind starts from the
  unrestricted request, admits only the exact bound VARCHAR `private`, retains
  DuckDB's residual, and never reuses another execution's selected plan;
- `CompiledConnector + ScanRequest -> ScanPlan` snapshots proving Superset,
  DuckDB ownership, typed `visibility=private`, fallback, immutable copies, and
  absence of SQL text or credentials; and
- plan-only consumer fixtures proving Runtime can consume Superset and fallback
  values without Connector, Query, planner-private, or test-access dependencies.

Query owns DuckDB-expression recognition and product SQL equivalence. Connector
owns the additive column and mapping declaration validation. Runtime owns request
construction, Link policy, pagination, and lifecycle execution.

## Dependencies and interaction exit

Current state: **Satisfied; X-as-a-Service**. The final implementation keeps
the two-state remote candidate separate from the exact-or-opaque retained
DuckDB scope. Focused implication, row-bag, mapping-absence, capability,
counterexample, snapshot, plan-fixture, actual bind-copy, and Runtime admission
oracles pass. Query supplies structure without assigning accuracy or ownership,
and Runtime consumes the immutable typed plan without importing classifier or
Connector internals.

- **RFC gate:** only decision evidence and this plan may precede acceptance.
- **Connector dependency:** Semantics consumes one immutable declaration for
  the required `visibility` VARCHAR column, equality to `private`, the
  `visibility=private` operation input, `Superset` accuracy, and rationale.
- **Query dependency:** Semantics consumes only the closed candidate, exact
  capability state, and proof that DuckDB retains the expression.
- **Runtime dependency:** Runtime consumes only the complete plan and its one
  typed conditional input; Semantics does not prescribe URL assembly or Link
  parsing mechanics.

The initial Collaboration exits to X-as-a-Service when:

- Connector's declaration compiles into the visibility Superset/fallback oracle
  and broader private-boolean mappings are rejected;
- every supported Query capability profile and structural counterexample yields
  the proven optimized or fallback plan without Query assigning accuracy or
  ownership;
- Runtime sends the admitted `visibility=private` input on every page and never
  imports or reinterprets predicate or Connector internals; and
- Semantics independently maintains the closed classifier, row-bag property,
  counterexample corpus, plan fixtures, and focused gates.

The final source and target dependencies prove those conditions independently
of the passing end-to-end query.

## Documentation and verification

After RFC acceptance, the coherent delivery must propagate the same-field
visibility proof, Superset classification, DuckDB ownership, typed input, and
fallback through `docs/ARCHITECTURE.md`, `docs/CONNECTOR_SPECIFICATIONS.md`,
`docs/RUNTIME_CONTRACTS.md`, adjacent source documentation, diagnostics,
examples, fixtures, release notes, and roadmap evidence. No package/YAML syntax
or broader predicate support is activated.

Required verification includes:

1. the closed classifier table and public/private/internal row-bag property;
2. forced-local equivalence, `NULL`, error, ambiguity, capability-absence, and
   broader-private-boolean counterexamples;
3. prepared-statement rebind oracles alternating exact `private`, non-private,
   `NULL`, differently typed, and unsupported parameter values, including
   concurrent copies and failed refinement without selected-plan leakage;
4. planner, plan snapshot, pagination, plan-only fixture boundary, and existing
   relation regression targets;
5. controlled multi-page equivalence and reduced-request evidence with
   `visibility=private` on every page;
6. `make build`, `make test`, `make demo`, and `make verify`;
7. source and native-dependency identity gates plus a fresh native product cell;
8. agent-asset validation and staged and unstaged diff checks; and
9. independent Relational Semantics and adversarial review of implication,
   ownership, fallback, typed authority, and the final dependency graph.

Completion requires the forced-local baseline, every conservative fallback,
unchanged existing named-column queries, the intentional trailing visibility
column behavior, and satisfied interaction exits.

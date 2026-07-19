# RFC 0008: Negotiate selective remote predicates

```yaml
rfc: "0008"
title: "Negotiate selective remote predicates"
status: "Accepted"
rfc_type: "Product"
sponsor_team: "Query Experience"
technical_decision_owner: "Lead agent"
product_approver: "ngalluzzo"
authors:
  - "Lead agent"
required_reviewers:
  - "predicate_query_review"
  - "predicate_connector_review"
  - "predicate_semantics_review"
  - "predicate_runtime_review"
affected_teams:
  - "Query Experience"
  - "Connector Experience"
  - "Relational Semantics"
  - "Remote Runtime"
linked_outcome_or_objective: "Predicate-selective repository scans"
supersedes: "none"
```

## Summary

Let the pinned DuckDB optimizer offer structured filter expressions to the
adapter through its complex-filter callback. The authenticated-repositories
relation exposes GitHub's `visibility` field, and Query may translate only the
closed `visibility = 'private'` shape into a protocol-neutral request. Connector
binds that shape to GitHub's fixed `visibility=private` input; Semantics records
a conservative superset classification while retaining the original predicate
in DuckDB as the sole residual owner. Runtime applies the typed input to every
bounded page without interpreting DuckDB expressions. All other predicates
preserve the existing complete traversal and local evaluation.

## Sponsorship and context

- **RFC type:** Product.
- **Sponsoring team:** Query Experience.
- **Linked outcome or objective:** Predicate-selective repository scans.
- **Why now:** The installed product can traverse the authenticated repository
  collection safely, but every query still requests the complete collection.
  The approved outcome asks whether ordinary DuckDB relational intent can
  reduce remote work without changing the row bag. This is the first bounded
  proof of the product's relational-adapter value after live authenticated
  pagination.

The affected DuckDB user asks for repositories of one exact GitHub visibility
through the existing relation and secret surface. GitHub documents
`GET /user/repos` as the authenticated user's explicitly accessible repository
collection, its response repository `visibility`, and a `visibility` input that
limits results to the specified visibility. Public, private, and internal are
distinct visibility categories.

The original `private = TRUE` candidate was unsafe to bind to
`visibility=private` because GitHub's boolean is also true for internal
repositories. The follow-up `type=private` candidate also lacked an explicit
REST completeness guarantee for internal rows. Product approval therefore
changes the SQL predicate, not the invariant: expose the upstream `visibility`
field and bind only `visibility = 'private'` to `visibility=private`. Internal
rows do not satisfy that SQL predicate, so they are correctly absent. The plan
still classifies the mapping as `Superset` and retains the DuckDB residual; the
delivery does not depend on removing local evaluation.

Primary sources:

- [List repositories for the authenticated user](https://docs.github.com/en/rest/repos/repos?apiVersion=2022-11-28#list-repositories-for-the-authenticated-user)
- [About repository visibility](https://docs.github.com/en/enterprise-cloud@latest/repositories/creating-and-managing-repositories/about-repositories#about-repository-visibility)
- [GraphQL Repository fields](https://docs.github.com/en/enterprise-cloud@latest/graphql/reference/repos)
- [Pinned GitHub REST OpenAPI description](https://github.com/github/rest-api-description/blob/03ca9c1cac754ec9b8369dc75de8a8c753c6e087/descriptions/api.github.com/api.github.com.2022-11-28.yaml)
- pinned DuckDB 1.5.4 `table_function.hpp`, `pushdown_get.cpp`, and
  `physical_table_scan.cpp` under the source identity in `extension_config.cmake`

## Problem

The native adapter advertises neither generic filter pushdown nor a selective
complex-filter callback. Bind therefore builds a request containing only the
string `TRUE`, Semantics always plans `TRUE` remotely and residually, and
Runtime always requests:

```text
/user/repos?per_page=100&page=N
```

DuckDB correctly filters the resulting rows, but a selective query still
traverses unrelated pages. Merely setting `TableFunction::filter_pushdown`
would be incorrect: DuckDB would remove every generated table filter from the
operator tree and require the table function to evaluate forms the product has
not proved. Reading filters only in global initialization would also be too
late for deterministic planning and truthful `EXPLAIN`.

The pinned DuckDB optimizer exposes a different, selective boundary.
`pushdown_complex_filter` receives structured bound expressions and a mutable
list. A table function may inspect that list during optimization; expressions
left in the list remain in a DuckDB `LogicalFilter`. This lets the adapter add
a safe remote restriction without claiming generic filter execution or
removing the residual.

Observed facts are:

- `TableFunctionBindInput` does not contain scan filters;
- `TableFunctionInitInput` contains generated `TableFilterSet` values only
  after physical planning;
- `pushdown_complex_filter` runs from optimizer filter pushdown and receives
  `vector<unique_ptr<Expression>> &filters` plus the logical scan and bind data;
- filters left after that callback are regenerated above the scan when generic
  `filter_pushdown` remains disabled;
- physical scan explanation invokes table-function `to_string` against the
  post-optimization bind data;
- the current `ScanRequest` represents its predicate as an unrestricted
  string even though only `TRUE` is accepted;
- the current pagination target and Link validator admit only `per_page` and
  `page`; and
- the current fixed Runtime request builder would drop any additional planned
  query input on later pages.

The pinned DuckDB callback and Runtime paths are feasible. The original boolean
mappings are rejected by the internal-repository counterexample and missing
REST completeness evidence. The revised same-field visibility mapping avoids
that ambiguity; every affected team approved it.

## Decision drivers and invariants

- **Must preserve:** the complete DuckDB row result; deterministic network-free
  bind, optimization, explain, and prepare; immutable connector facts and
  execution plans; one residual owner; local ordering and bounds after
  filtering; exact-name secret handling; bounded sequential pagination;
  cancellation; strict conversion; redacted diagnostics; and all existing
  relation behavior.
- **Must enable:** the approved `visibility = 'private'` SQL predicate to produce a narrower remote
  page sequence through a declared, explainable mapping.
- **Must require:** for DuckDB predicate `D` and remote restriction `R`,
  `D => R`; this decision deliberately retains `D` as DuckDB's residual and
  therefore does not rely on `R => D` or remove local evaluation.
- **Must fail closed:** an unrecognized expression class, operator, column,
  table binding, constant type, `NULL`, unresolved parameter, disjunction, negation,
  conflicting binding, missing adapter capability, or missing connector
  mapping cannot alter the remote request.
- **Must not introduce:** generic table-filter ownership; SQL-text parsing;
  expression evaluation in Runtime; another predicate mapping; projection,
  ordering, limit, or offset pushdown; new operation selection; caller query
  inputs; new credential or destination authority; retries; caching; parallel
  pagination; package loading; YAML compilation; GraphQL; or a public native
  ABI.

## Proposed decision

Query registers DuckDB's selective `pushdown_complex_filter` callback while
leaving `filter_pushdown` and `filter_prune` disabled. The callback considers
only a bound equality whose depth-zero column reference belongs to this
`LogicalGet`, resolves to the exact `visibility` VARCHAR column, and whose other
operand is the non-null VARCHAR constant `private`. Equality may present the
column on either side. Query converts that shape into one closed
protocol-neutral remote-candidate value. Separately, it records whether the
complete DuckDB-retained predicate is unrestricted, exactly that candidate, or
an opaque larger DuckDB filter; it never uses `Expression::ToString` as a
parser input. A supported top-level conjunct may therefore narrow remote work
without pretending that it is the whole residual.

DuckDB 1.5.4 may substitute a bound parameter before this callback, making
`visibility = $1` structurally indistinguishable from the literal equality for
that execution. Query accepts the candidate only when the resulting structured
value is the exact non-null VARCHAR `private`. DuckDB rebinds parameters for
each prepared execution, and Query must refine from the retained unrestricted
request each time; parameter provenance is neither reconstructed nor another
request state.

The callback does not erase any expression. DuckDB therefore remains the one
residual owner and evaluates the complete original filter after the
table-function scan. Query asks Semantics for a replacement immutable plan
using the original credential-free request snapshot plus the structured
remote candidate and retained-predicate scope. Bind data
replaces one immutable plan value during deterministic logical optimization;
execution never mutates a plan. A copied prepared or explain bind state owns
its own request and plan selection.

Connector adds one closed mapping to the immutable native
`authenticated_repositories` relation:

```text
column: visibility
operator: equals
literal: private
remote input: visibility=private
accuracy: superset
operation: github_authenticated_repositories
```

The mapping is a distinct immutable `CompiledPredicateMapping` exposed through
`CompiledRelation::PredicateMappings() const`; it is not a conditional use of
`CompiledQueryParameter`. Cross-field validation ties its column, typed
literal, operation, and input to existing declarations and rejects conflicts
with fixed query and pagination fields. Connector-owned decoy fixtures prove
that consumers use this provider interface rather than names or request
strings. The mapping contains structural facts only. It does not contain a
DuckDB expression, SQL text, mutable request, credential, or Runtime object.
The relation's base operation remains its fallback. Its schema adds the required
VARCHAR `visibility` column sourced from the same repository response object.

Semantics validates that the request shape, retained-predicate scope, relation mapping, output column,
operation, and capability all agree. It produces a plan with the same base
domain and operation plus the fixed remote predicate and input. The plan says
that `visibility = 'private'` is remote with superset accuracy. Its residual is
the exact visibility predicate when that is the complete filter, or the opaque
`complete_duckdb_filter` marker when DuckDB retained a larger expression. The
classification is conservative even when the upstream filter is observed to
be exact; this goal does not remove local predicate evaluation.

`ScanPlan` carries one closed typed conditional input value through a const
accessor; raw encoded operation fields are not a second authority for that
selection. Runtime admission consumes that value once and builds one immutable
`AdmittedRepositoryRequestProfile` containing the canonical fixed field set.
Unknown type/value variants, duplicate or conflicting fields, and any mismatch
with the base operation or pagination target fail before authorization or I/O.
The first-page builder, every later-page builder, and Link validator consume
that same admitted profile. An accepted Link target must contain exactly its
fixed values plus the expected page-size and next-page fields. A response
cannot add, remove, duplicate, or alter the selected field, nor can it widen
any existing authority. Runtime does not inspect the planned predicate or
choose the field.

### Public behavior

The existing SQL surface gains one additive column and one optimization:

```sql
SELECT id, full_name, private, visibility, fork, archived
FROM duckdb_api_scan(
    connector := 'github',
    relation := 'authenticated_repositories',
    secret := 'github_default'
)
WHERE visibility = 'private';
```

Supported execution requests
`/user/repos?per_page=100&page=N&visibility=private` using the canonical order
recorded in the accepted plan. DuckDB still evaluates
`visibility = 'private'`.

`EXPLAIN` reports at least the relation, remote predicate, remote accuracy, and
residual owner from safe bind data. The ordinary DuckDB filter remains visible
above the scan. Explain performs no secret resolution or network I/O.

All other filters use the existing full request and remain local. The order of
returned rows, local `ORDER BY`, local `LIMIT` and `OFFSET`, existing column
meanings, secret syntax, and errors remain compatible. `SELECT *` gains the
new trailing `visibility` column as an intentional pre-`1.0` additive change.

### Shared interfaces

- **Connector Experience provides:** a closed immutable native predicate
  mapping with column, operator, literal, operation/input binding, and accuracy;
  public const access and safe snapshots; validation that the mapping refers to
  one declared column and operation.
- **Query Experience provides:** a closed structured `RequestedPredicate` in
  `ScanRequest`, constructed only from accepted DuckDB expression structure;
  base request retention needed for deterministic optimizer refinement; and a
  safe table-function explanation.
- **Relational Semantics provides:** planned predicate and accuracy values,
  one authoritative closed typed conditional input, residual ownership,
  classification reason, and the only `ScanRequest -> ScanPlan` construction
  path.
- **Remote Runtime consumes:** the typed conditional input, base operation, and
  pagination target to construct one admitted request profile before
  authorization or I/O. It does not include `ScanRequest`, Connector mapping,
  DuckDB expression, or predicate-classification dependencies.
- **`BatchStream`:** not affected. Its pull, batch, cancellation, close, and
  error contracts remain unchanged.
- **Fixture execution:** Semantics owns safe plan fixtures containing the new
  classification; Runtime consumes those fixtures through the existing
  plan-only boundary.

These are private pre-`1.0` team APIs. They do not establish connector-package
syntax or a stable native ABI.

### Operational behavior

Authentication, headers, HTTPS destination, post-DNS policy, redirects,
private-address policy, concurrency, page and scan ceilings, timeouts,
cancellation, backpressure, retry, and cache remain unchanged. The only request
delta is one fixed non-sensitive query field selected before execution.

Each page remains a distinct one-attempt replay unit. The same planned
visibility field is reconstructed on every page. A Link value that omits or
changes the field fails as pagination policy rather than silently widening the
scan. No received URL becomes transport authority.

## Topology impact

| Team | Role in this RFC | Interface or charter impact | Interaction mode | Exit condition |
| --- | --- | --- | --- | --- |
| Query Experience | Sponsoring team and DuckDB adapter | Converts one structured expression, retains DuckDB residual, refines bind plan, and explains the result | Collaboration, then X-as-a-Service | SQL, offline lifecycle, fallback, and explain oracles pass without Connector or Runtime internals in Query |
| Connector Experience | Metadata provider | Declares and validates one immutable native mapping | Collaboration, then X-as-a-Service | Consumers use const mapping access and do not infer it from request strings |
| Relational Semantics | Correctness and plan provider | Proves implication, accuracy, input binding, residual ownership, and conservative fallback | Collaboration, then X-as-a-Service | Property/counterexample oracles pass and consumers do not reclassify the plan |
| Remote Runtime | Execution provider | Applies typed fixed query fields through bounded sequential pagination | Collaboration, then X-as-a-Service | DuckDB-free request, Link-policy, security, lifecycle, and plan-admission evidence passes |

No accountability boundary moves. Cognitive load is kept at the existing
interfaces: Query understands DuckDB expression classes, Connector understands
declared mappings, Semantics understands relational implication, and Runtime
understands executable request and pagination state.

## Correctness, security, and lifecycle analysis

- **Relational semantics and fallback:** The remote restriction is accepted
  only after `D => R` evidence. Because the original DuckDB expression remains
  above the scan, DuckDB is the sole residual owner and three-valued semantics
  are unchanged. Unsupported structures leave the request and plan unchanged.
- **Authentication, credentials, network policy, and privacy:** No credential
  field or authority changes. The non-sensitive visibility field narrows the
  server response. Diagnostics expose the field name and fixed enum value but
  never secret or row data.
- **Resources, backpressure, and cancellation:** Existing page and scan budgets
  remain hard ceilings. A narrower relation may finish in fewer pages but does
  not acquire a larger budget. Pull, cancellation, and close are unchanged.
- **Replay, retry, caching, and duplicates:** No retries or cache are added.
  Page identities and duplicate-preserving row semantics remain unchanged.
- **Concurrency, immutability, and state:** Optimization may replace a bind
  state's immutable plan before physical execution. Concurrent executions
  consume independent copied bind states and immutable plans. Runtime remains
  single-request and sequential.
- **FFI, initialization, reload, shutdown, and containment:** The implementation
  remains C++ and uses pinned DuckDB C++ extension APIs. No new cross-language
  FFI or initialization surface appears. Existing failure containment remains.
- **Diagnostics and progress:** Safe explain gains relational classification.
  Progress and public telemetry remain unsupported. Request and plan errors
  remain structured and redacted.

## Compatibility and migration

Existing named-column queries require no migration. Unfiltered queries and
filters other than the approved shape keep the complete traversal. The SQL call
is unchanged, and the relation adds one trailing required VARCHAR `visibility`
column. Queries using `SELECT *` intentionally observe that additive pre-`1.0`
schema change. The optimized query returns the same result as full traversal
because DuckDB retains the original predicate.

Prepared statements keep deterministic copied bind state. A prepared statement
containing the supported constant predicate plans the same restriction on each
execution while resolving the named secret at execution time as before.
An unresolved, `NULL`, non-private, differently typed, volatile, correlated,
ambiguous, or unsupported expression remains local. A bound parameter that
DuckDB has substituted with the exact supported constant may select the same
restriction for that execution; alternating prepared executions must not share
selected plan state.

Rollback consists of disabling the complex-filter callback and removing the
new mapping and plan fields; because the residual is never removed from
DuckDB, rollback returns to full traversal without a data migration or result
change. The plan and connector changes remain private pre-`1.0` interfaces.

## Evidence and bounded trials

| Question or claim | Evidence required | Method or fixture | Result and limitations |
| --- | --- | --- | --- |
| GitHub can narrow the authenticated collection without caller URL authority | Official fixed input and domain | GitHub REST documentation for `GET /user/repos` | Established: the closed `visibility` input limits the endpoint collection to the specified visibility and accepts `private`; default is `all`. Upstream availability remains live compatibility evidence, not the correctness oracle. |
| `visibility = 'private'` safely implies the remote restriction | Same response field and filter domain | Pinned REST endpoint/OpenAPI response schema and query-parameter contract | Established: the SQL value is extracted from repository `visibility`, while the fixed input limits the same collection to that specified visibility. Internal is a distinct value and therefore is neither a DuckDB-true row nor required in the restricted bag. |
| Binding `private = TRUE` to a visibility or legacy type input is unsafe or unproved | Internal/private counterexample and missing REST completeness contract | GitHub visibility documentation, Repository privacy contract, and affected-team reviews | Established: the visibility input omits internal rows satisfying the broader boolean; the legacy type input does not explicitly guarantee their inclusion. Both boolean mappings are rejected. |
| The authorized live account can supply the missing internal counterexample | Privacy-safe aggregate observation only | 2026-07-19 paginated `GET /user/repos` observation using the approved local credential | Inconclusive: all 432 observed rows were `visibility=public, private=false`; no repository identity or value was recorded. The observation cannot prove a universal mapping. |
| DuckDB can offer one filter selectively without transferring every filter | Pinned structured optimizer API and residual behavior | Source audit of DuckDB 1.5.4 `table_function.hpp` and `pushdown_get.cpp` | Established: the complex callback receives bound expressions; expressions left in its vector are regenerated as DuckDB filters. Generic `filter_pushdown` stays disabled. |
| Explain can reflect the selected plan without executing | Pinned post-optimization bind-data explanation path | Source audit of DuckDB 1.5.4 `physical_table_scan.cpp`; controlled `EXPLAIN` oracle required during delivery | Established in source; exact rendering remains implementation evidence. |
| Every page preserves the narrowing and rejects response widening | Typed reconstruction and counterexamples | Runtime fixed-field pagination fixtures and controlled multi-page service | Pending implementation evidence; required for goal completion, not RFC feasibility. |

No disposable product trial can strengthen an upstream contract. The accepted
decision uses the endpoint's same-field visibility contract rather than an
inference about the broader private boolean. Deterministic
private/internal/public fixtures prove that the product preserves the declared
contract; live observations remain compatibility evidence only.

## Alternatives considered

### Enable generic `filter_pushdown`

DuckDB would provide convenient `TableFilterSet` values during initialization,
but it would also remove filters it expects the table function to evaluate.
Supporting that contract would require a complete local filter evaluator or a
much wider mapping set and would move DuckDB semantic load into Query. Rejected.

### Remove the exact predicate from DuckDB

The official contract may support exact classification, and removing the
expression would save one local boolean comparison. It would make upstream
field correspondence part of the correctness boundary and add little user
value relative to reduced requests. Retaining the residual is safer,
explainable, and still proves the product mechanism. Rejected for this goal.

### Parse DuckDB expression text

Text is easy to log or pattern-match but loses structural identity, type,
binding, collation, parameter, and `NULL` facts. It violates the project's
explicit no-reconstruction invariant. Rejected.

### Read `TableFilterSet` in global initialization

This preserves structured metadata but arrives after logical planning, makes
the accepted plan unclear during `EXPLAIN`, and creates pressure to mutate a
plan during execution. Rejected.

### Add a `visibility` SQL argument or new relation

An explicit argument would narrow the request but bypass the product's core
relational proposition and create another public configuration surface. A new
relation would fragment one base domain. Both increase Query and user cognitive
load. Rejected.

### Bind `private = TRUE` to `visibility=private`

This is the most obvious spelling but it is unsafe: GitHub distinguishes the
internal visibility category while treating internal repositories as private
for repository privacy. The restriction may therefore be a subset of the
DuckDB-true rows. Rejected after Connector review.

### Bind `private = TRUE` to `type=private`

The legacy type input is real, but the pinned REST contract does not explicitly
guarantee that it includes every internal repository whose broader private
boolean is true. Connector and Relational Semantics therefore rejected the
required implication. Rejected.

### Keep complete traversal

This is already correct and remains the fallback. It does not demonstrate
whether relational intent can reduce remote work, so it cannot satisfy the
approved outcome. Retained only for unsupported profiles.

## Drawbacks and failure modes

- The remote restriction and local residual duplicate work. Query owns the
  negligible local boolean cost; Semantics owns the conservative accuracy
  explanation.
- Bind data must retain enough credential-free request state to deterministically
  obtain a refined immutable plan. Query owns copy isolation and documentation.
- The DuckDB complex-filter callback is a C++ compatibility surface. Query and
  Engineering Enablement must keep the pinned source identity and compatibility
  oracle aligned; no broader DuckDB compatibility is implied.
- Pagination must now preserve a fixed non-page query field. Runtime owns every
  counterexample where a response omits, changes, duplicates, or adds a field.
- GitHub may change its API contract. Connector owns the declaration and live
  compatibility observation; correctness remains fail-closed and retains the
  local residual, but a false-negative upstream contract break cannot be
  repaired locally. A future incompatibility disables this mapping rather than
  guessing.
- Supporting only one expression shape is intentionally narrow. Query must
  explain the accepted shape and leave every other shape visibly local rather
  than silently normalizing more SQL.

## Acceptance and verification

- **End-to-end demonstration:** Against a controlled mixed-visibility service,
  the approved SQL returns the same ordered rows as a forced-local baseline,
  issues the fixed visibility field on every page, and completes in fewer
  requests. An unsupported filter uses the original full sequence. `EXPLAIN`
  shows remote superset and DuckDB residual without a request or secret lookup.
- **Automated oracle:** Connector mapping snapshots and invalid declarations;
  Query bound-expression shape and copy isolation; Semantics implication,
  classification, ambiguity, missing-capability, input-binding, and plan
  snapshots; Runtime fixed-field request reconstruction, Link counterexamples,
  plan admission, pagination, budgets, cancellation, close, and redaction;
  controlled SQL equivalence and regressions; and a prepared-statement sequence
  binding `private`, `public`, `NULL`, and `private` again with independent
  per-execution plans and retained residuals.
- **Quality gates:** focused responsibility targets, `make build`, `make test`,
  `make demo`, source/dependency identity checks, a new-root native product
  gate on the supported cell, agent-asset validation, and staged and unstaged
  diff checks.
- **Independent review:** Query/DuckDB lifecycle and FFI; Connector declaration;
  Relational implication and residual ownership; Runtime request, security,
  pagination, and lifecycle; test-oracle; and at least two fresh adversarial
  perspectives.
- **Interaction exit:** every consumer builds only against its provider's
  public team API; Runtime fixtures consume `ScanPlan` without Connector,
  Query, or planner construction dependencies; the final source and test graph
  satisfies every exit in the topology table.

## Contract propagation

| Source of truth or artifact | Impact | Required update | Completion evidence |
| --- | --- | --- | --- |
| `docs/ARCHITECTURE.md` | Affected | Replace the native no-filter profile with selective remote narrowing, local residual, request, and explain behavior | Pending delivery |
| `docs/CONNECTOR_SPECIFICATIONS.md` | Affected only for native-preview mapping | Record the fixed native mapping without activating package/YAML syntax | Pending delivery |
| `docs/RUNTIME_CONTRACTS.md` | Affected | Update lifecycle timing, structured request, plan classification, pagination fixed inputs, fallback, and explanation | Pending delivery |
| `docs/TEAM_TOPOLOGY.md` and active charters | Not affected | Existing accountabilities and interactions already place each responsibility | Charter-backed plans and final exit audit |
| `docs/PRODUCT_DELIVERY.md`, `AGENTS.md`, and skills | Not affected | Existing goal, RFC, contract-change, topology, and review workflow governs the change | Agent-asset validation |
| Examples, diagnostics, fixtures, tests, changelog, and roadmap | Affected | Add approved SQL, safe explain, deterministic equivalence/request/fallback evidence, and unreleased product note | Pending delivery |

## Unresolved questions

None. Additional predicate shapes, exact residual removal, projection,
ordering, and bounds remain separate product decisions rather than questions
inside this RFC.

## Review record

| Required reviewer | Team | Result | Evidence or objection | Disposition by decision owner |
| --- | --- | --- | --- | --- |
| `predicate_query_review` | Query Experience | Approved | Pinned DuckDB 1.5.4 supplies the structured callback, leaves untouched expressions as DuckDB filters, substitutes bound parameters before the callback, rebinds them per execution, and explains post-optimization bind data | Accepted. Query retains the baseline request and filter vector, recognizes only the resulting exact VARCHAR equality, performs no REST selection, deep-copies plan state, and keeps bind, prepare, and explain offline. |
| `predicate_connector_review` | Connector Experience | Approved | The same-field contract establishes `D => R`; the immutable mapping, strict trailing VARCHAR extraction, cross-field validation, conservative classification, and retained residual preserve the Connector boundary | Accepted. Implementation and interaction-exit evidence remain delivery gates. |
| `predicate_semantics_review` | Relational Semantics | Approved | The endpoint's specified-visibility restriction contains every row satisfying `visibility = 'private'`; internal is a distinct nonmatching value; a bound parameter substituted with that exact typed constant has the same execution predicate; DuckDB remains sole residual owner | Accepted. Unsupported or unresolved shapes retain the complete base-domain plan; prepared executions must refine independently from the unrestricted request. |
| `predicate_runtime_review` | Remote Runtime | Approved | One closed typed input is admitted with the operation, six-column schema, and pagination target into the immutable profile consumed by first-page, continuation, Link, authorization, and transport paths | Accepted. Runtime never interprets predicates or decoded visibility values; implementation and interaction-exit evidence remain delivery gates. |

## Decision and rationale

- **Technical decision owner:** Lead agent.
- **Product approval:** ngalluzzo approved the revised linked outcome, the
  additive repository `visibility` column, the
  `visibility = 'private'` SQL surface, conservative fallback, and deferral of
  other relational operations on 2026-07-19. Retaining the DuckDB residual
  narrows risk without changing that outcome.
- **Rationale:** Accepted. Same-field visibility semantics remove the false-negative gap
  in both earlier broader-boolean candidates while preserving ordinary SQL,
  conservative local residual evaluation, and a single typed execution input.
  All affected teams approved the decision.
- **Material objections:** Connector and Semantics' broader-boolean objections
  are accepted by changing the predicate to the same REST visibility field.
  Runtime's competing-authority objection is accepted in the revised proposal.
  Query confirmed the pinned optimizer and lifecycle boundary. All reviewers
  approved the integrated decision; no material objection remains.
- **Superseded by:** Not applicable.

## Follow-on goals

| Outcome or objective | Accountable team | Supporting teams and interaction modes | Activation condition |
| --- | --- | --- | --- |
| Predicate-selective repository scans | Query Experience | Connector Experience, Relational Semantics, and Remote Runtime in Collaboration, then X-as-a-Service | RFC 0008 Accepted and the revised product goal active through `docs/PRODUCT_DELIVERY.md` |

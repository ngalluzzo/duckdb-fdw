# RFC 0010: Prove conservative relational composition

```yaml
rfc: "0010"
title: "Prove conservative relational composition"
status: "Accepted"
rfc_type: "Product"
sponsor_team: "Query Experience"
technical_decision_owner: "Lead agent"
product_approver: "Product manager"
authors:
  - "Lead agent"
required_reviewers:
  - "semantic_trust_query_review"
  - "semantic_trust_connector_review"
  - "semantic_trust_semantics_review"
  - "semantic_trust_runtime_review"
affected_teams:
  - "Query Experience"
  - "Connector Experience"
  - "Relational Semantics"
  - "Remote Runtime"
linked_outcome_or_objective: "Trustworthy composed remote queries — complete the 0.6.0 semantic-trust outcome"
supersedes: "none"
```

## Summary

Define one protocol-neutral semantic decision contract for exact, superset,
unsupported, ambiguous, and invalid predicate cases, including conservative
composition and capability fallback. In the native DuckDB profile, Query
erases none of the expressions offered by DuckDB and DuckDB remains the
semantic owner of filtering, projection, ordering, limit, and offset; the
current `visibility = 'private'` restriction remains the only public remote
predicate mapping. A Semantics-owned executable law oracle and an end-to-end
DuckDB differential prove that optimization changes remote work only, never
the result.

## Sponsorship and context

- **RFC type:** Product.
- **Sponsoring team:** Query Experience.
- **Linked outcome:** Trustworthy composed remote queries, completing the
  `0.6.0` semantic-trust outcome in `ROADMAP.md`.
- **Why now:** The current product proves one safe selective predicate and its
  fallbacks. The approved roadmap requires general semantic trust before a
  second protocol path can rely on the same planner and Query boundaries.

The DuckDB user composes ordinary projection, filtering, ordering, and bounds
over `github.authenticated_repositories`. The product must return the same row
bag as complete remote traversal followed by DuckDB evaluation even when one
supported conjunct narrows the remote traversal. It must return the same
sequence when SQL supplies a total `ORDER BY`; unordered queries retain no
ordering promise. Product approval on 2026-07-19 selects that outcome and
explicitly excludes new public predicate mappings and remote projection,
ordering, or bound pushdown from this goal.

## Problem

RFC 0008 deliberately proves one closed path. Query recognizes
`visibility = 'private'`, Connector supplies one reviewed superset mapping,
Semantics selects that typed input, and Runtime applies it to every page while
DuckDB retains the complete filter. The production representation consequently
has only `Unsupported` and `Superset` plan accuracy, and the classifier embeds
the one GitHub mapping profile.

That implementation is correct for the delivered slice but is not yet the
general semantic contract promised by the architecture and roadmap:

- the production decision function cannot independently demonstrate exact
  classification even though Connector metadata already distinguishes exact
  and superset mappings;
- Query currently counts recognized top-level candidates before Semantics,
  while the implication and composition laws are not one reusable Semantics
  decision over the structure DuckDB exposed;
- unsupported query structure, ambiguous candidate selection, unavailable
  adapter capabilities, and invalid declarations all avoid unsafe work, but
  their different meanings are not represented and tested as one matrix;
- the full-projection, empty-ordering, and unset-bound fallback is validated as
  one native request shape rather than proved against composed SQL results; and
- explain output can describe the current selected or fallback plan without a
  single executable completeness oracle tying classification, residual scope,
  relational ownership, and fallback reason together.

For example, consider:

```sql
SELECT full_name
FROM duckdb_api_scan(
    connector := 'github',
    relation := 'authenticated_repositories',
    secret := 'github_default'
)
WHERE visibility = 'private'
  AND archived = false
ORDER BY id DESC
LIMIT 2;
```

It is safe to use `visibility=private` as a remote superset restriction because
the complete conjunction implies that restriction. It is not safe to apply the
limit before DuckDB evaluates `archived = false` and orders the surviving rows.
For `visibility = 'private' OR archived = false`, using the same restriction
would lose DuckDB-true archived rows, so the remote predicate must be `TRUE`.
The product needs one decision contract and oracle that proves both cases.

## Decision drivers and invariants

- **Must preserve:** For DuckDB predicate `D` and remote restriction `R`, a
  remote restriction is safe only when every DuckDB-true row is remote-true.
  Exactness additionally requires matching `TRUE`, `FALSE`, and `NULL`
  outcomes over the declared domain; the two true-set implications are
  necessary but are not sufficient for negation or executable encoding.
- **Must preserve:** A mapped operation/input is an occurrence-preserving
  restriction of the same duplicate-preserving base-domain bag. Every
  DuckDB-true base occurrence remains available with the same multiplicity;
  exactness additionally excludes extra occurrences. Predicate truth alone
  cannot prove this property.
- **Must preserve:** Every residual has exactly one owner. In the native
  profile Query erases none of the filter expressions DuckDB offers to the
  callback, and DuckDB remains their semantic owner, including when a remote
  mapping is exact. DuckDB may simplify predicates before the callback or
  eliminate an operator or scan independently.
- **Must preserve:** Required filtering and ordering occur before limit or
  offset. The native profile delegates none of those operations.
- **Must preserve:** Missing or ambiguous DuckDB metadata changes optimization
  only. The adapter never reconstructs unavailable structure from SQL text.
- **Must preserve:** Bind, optimization, prepare, and explain are deterministic
  and perform no network I/O or secret resolution.
- **Must preserve:** Plans and connector snapshots are immutable; execution is
  bounded, cancelable, strictly converting, credential-safe, and sequentially
  paginated.
- **Must enable:** One production semantic decision function and executable law
  oracle for exact, superset, unsupported, ambiguous, and invalid cases;
  conjunction, disjunction, negation, and `NULL`; projection closure; ordering
  and bound prerequisites; capability fallback; and safe explanation.
- **Must not introduce:** Another public predicate mapping, generic runtime
  filter evaluation, remote projection/order/limit/offset pushdown, SQL-text
  parsing, a protocol addition, connector-package syntax, retry, cache, or new
  credential/network authority.

## Proposed decision

### Public behavior

The existing SQL function, relation names, arguments, columns, and
`visibility = 'private'` remote restriction remain unchanged. No additional
predicate acquires remote authority.

A DuckDB user may compose projections, supported and unsupported predicates,
ordering, limit, and offset. On the accepted native profile:

- the table function supplies the complete declared column closure;
- Query erases none of the filter expressions DuckDB offers to the callback;
  DuckDB remains their semantic owner and may independently simplify them or
  eliminate the scan;
- DuckDB performs final projection, ordering, limit, and offset;
- a safe supported conjunct may narrow remote traversal;
- unsafe, unsupported, ambiguous, or unavailable shapes select the unrestricted
  base plan when DuckDB executes the scan; a DuckDB-pruned scan performs no
  Runtime entry or traversal; and
- an invalid connector or plan contract fails before network activity with a
  deterministic safe diagnostic rather than being relabeled as ordinary
  fallback.

Query-visible explanation reports the remote predicate and accuracy, the scope
of the filter offered and left untouched plus its DuckDB owner, projection
closure, owners of ordering and bounds, adapter capability fallback, and one
stable classification category and safe reason. Explanation is evidence, never
Runtime authority or a serialization format.

### Shared interfaces

Connector Experience remains the producer of validated immutable predicate
mapping facts. A mapping carries its closed predicate shape, selected operation
and input, encoded value, proof identity, `Exact` or `Superset` accuracy,
base-domain identity, occurrence-preservation proof, and the operation-scoped
encoding/composability capability. The permanent native catalog continues to
contain only the accepted GitHub visibility superset mapping and one positive
conditional-input encoding. Invalid, conflicting, cross-domain, multiplicity-
changing, or unencodable declarations are rejected during catalog validation.

Query Experience converts only structured DuckDB expressions exposed by the
active adapter into a bounded protocol-neutral candidate algebra. Query
preserves column binding and typed literal identity but does not match
Connector mappings, prove implication, choose a remote input, or classify
composition. The algebra contains only:

- `TRUE` for no offered restriction;
- a typed comparison leaf over a bound output column and typed constant;
- `AND`, `OR`, and `NOT` structure over translated children; and
- an opaque unsupported leaf for structure that cannot be translated safely.

The algebra is not a DuckDB expression evaluator or residual serialization. It
contains no SQL text, function implementation, collation inference, or
authority for Runtime. Its unsupported leaf retains only the position needed
for Semantics to prevent an unsafe partial `OR` or `NOT`; it cannot be parsed or
executed.

`ScanRequest` keeps these facts separate:

1. the protocol-neutral candidate algebra offered for remote selection;
2. the scope of the filter DuckDB offered and Query left untouched, with DuckDB
   as semantic owner;
3. the complete column closure required when projection metadata is
   unavailable;
4. empty ordering and unset bounds when those capabilities are unavailable;
   and
5. explicit adapter capabilities proving whether candidate inspection and
   residual retention are safe.

The request contains no SQL text, DuckDB object, secret value, remote request
field, or I/O authority. Query may report that DuckDB supplied no scan because
it simplified or pruned the query; that state does not construct a request or
enter Runtime.

Relational Semantics owns one decision function consumed by production
planning and by its independent law oracle. For each candidate it produces:

- `Exact` only when the mapped DuckDB and remote predicates have equivalent
  `TRUE`, `FALSE`, and `NULL` outcomes over the declared domain and the mapped
  operation preserves exactly the selected occurrences and multiplicities;
- `Superset` when `D => R` is established, the mapped operation preserves every
  DuckDB-true base occurrence with its multiplicity, and equivalence is not;
- `Unsupported` with remote `TRUE` when no safe restriction is available;
- an explicit ambiguous fallback when, after exactly one base operation has
  been selected, predicate mapping or request encoding cannot identify one
  safe remote restriction; or
- a deterministic planning failure when validated invariants, mapping
  identities, ownership facts, capability combinations, or operation
  eligibility/ranking are internally inconsistent or ambiguous and safe
  fallback would hide a broken contract. Equal-ranked base operations never
  fall back by choosing one arbitrarily.

The native profile treats both exact and superset remote predicates as
advisory. Query removes none of DuckDB's offered expressions in either case, so
exactness does not by itself transfer residual ownership or permit an early
bound. An exact semantic path is exercised through the production decision API
using a Connector-owned fixture that passes production validation under a
distinct deterministic proof identity. The fixture cannot relabel the installed
GitHub mapping or bypass validation through Semantics-private construction. Its
controlled operation must preserve duplicate occurrences and bind its proof to
the same base domain.

Composition follows these laws:

- Semantics, not Query, matches leaves, selects candidates, and composes their
  logical approximations, but it emits only an operation/input encoding that
  Connector explicitly declares executable;
- implication, three-valued equivalence, and occurrence preservation are
  necessary but not sufficient to emit a compound remote restriction;
- for `AND`, an unsupported child contributes remote `TRUE`; the current native
  profile may select at most one deterministically unambiguous safe conjunct
  because it has only one positive conditional-input encoding. Multiple
  otherwise-safe candidates without a declared compatible encoding use the
  explicit ambiguous fallback;
- `OR` requires a safe approximation for every branch and an operation-scoped
  encoding that preserves the union with multiplicity; the current native
  profile declares no such encoding and therefore uses remote `TRUE`;
- `NOT` requires equivalent `TRUE`, `FALSE`, and `NULL` outcomes, or an
  explicitly total two-valued domain with complement-preserving equivalence,
  plus a declared complement encoding. The current native profile declares no
  complement encoding and therefore uses remote `TRUE`;
- `NULL`, casts, functions, unresolved parameters, unsupported operators, or
  three-valued behavior without an explicit proof remain local; and
- no explanation or snapshot string is parsed to recover predicate structure.

`ScanPlan` remains a complete immutable handoff. It records remote accuracy,
the offered residual scope, DuckDB as residual/projection/order/bound owner, no
remote or Runtime ordering/bounds, the sole typed conditional input if one was
selected, and a structured classification category plus safe reason.

Remote Runtime consumes the resulting typed operation and conditional input.
It may validate executable support but cannot reinterpret accuracy, recompute
composition, remove residual work, or derive ordering or bound authority.

### Operational behavior

No new operational capability is enabled. The selected plan uses the existing
fixed HTTPS origin, capability-scoped bearer behavior, sequential pagination,
resource ceilings, one-attempt replay profile, cancellation, close, redaction,
and failure containment.

Planning failure, unrestricted fallback, selected optimization, and DuckDB
scan pruning are distinguishable. The first three are decided before Runtime
opens a transport; pruning performs no Runtime entry. A failed semantic
decision cannot partially mutate retained bind state; copied prepared and
explain state owns independent immutable requests and plans.

## Topology impact

| Team | Role in this RFC | Interface or charter impact | Interaction mode | Exit condition |
| --- | --- | --- | --- | --- |
| Query Experience | Sponsor accountable for the composed DuckDB result | Translate offered DuckDB structure into the bounded candidate algebra without erasing expressions, expose truthful explanation, and prove end-to-end equivalence including scan pruning | Collaboration with Semantics, then X-as-a-Service consumer | Composed SQL, executed fallback, and pruned execution pass without Query reimplementing mapping proof or Runtime behavior |
| Connector Experience | Predicate-capability provider | Supply validated immutable exact/superset facts without DuckDB or planner logic | X-as-a-Service; bounded collaboration only for the generic fact boundary | Semantics consumes the public read-only declaration API and Connector validates it independently |
| Relational Semantics | Semantic decision provider | Own classification, composition, residual and prerequisite laws, reasons, and the executable oracle | Collaboration, then X-as-a-Service provider | The production decision function passes the law matrix and consumers do not reclassify meaning |
| Remote Runtime | Plan-execution consumer and platform provider | Admit and execute typed plans without treating accuracy or explanation as authority | X-as-a-Service; bounded collaboration for plan compatibility | Runtime tests consume public plan fixtures and execute without Connector, Query, or planner internals |

No accountability or charter boundary moves. Relational proof is removed from
the GitHub-specific classifier path and made independently consumable; protocol
and DuckDB lifecycle knowledge remain outside Relational Semantics.

## Correctness, security, and lifecycle analysis

- **Relational semantics and conservative fallback:** This is the decision's
  primary effect. Query supplies structure but no semantic classification.
  Semantics owns implication, composition, three-valued exactness, residual
  ownership, projection closure, and bound prerequisites as executable
  properties of the production decision function.
- **Authentication, credentials, network policy, and privacy:** No authority
  changes. Requests and plans remain credential-free until execution
  initialization, and the same host and placement restrictions apply.
- **Resource budgets, backpressure, and cancellation:** No ceilings or stream
  behavior change. Remote narrowing may reduce work; fallback retains the
  existing bounded complete traversal. DuckDB may close after satisfying a
  local limit, but Runtime cannot claim or apply that limit.
- **Replay units, retries, caching, and duplicate prevention:** No change. One
  attempt per accepted page remains, retry and cache remain disabled, and
  duplicate-preserving bag semantics remain explicit.
- **Concurrency, immutability, and state ownership:** Requests, decisions, and
  plans are copied immutable values. Prepared executions refine independently;
  failure or fallback cannot mutate another execution's plan.
- **FFI, initialization, reload, shutdown, and failure containment:** Query's
  structured callback remains advisory and retains DuckDB expressions. No new
  callback, thread, runtime, or reload behavior is introduced. Existing
  exception containment and shutdown evidence remains required.
- **Diagnostics, redaction, metrics, and progress:** Explanation adds semantic
  completeness but no secret, raw SQL, URL authority, or credential fields.
  Metrics and progress remain unchanged.

## Compatibility and migration

No SQL, relation-schema, secret, connector-package, network, or installation
migration is required. Existing unfiltered, selective, unsupported, prepared,
DuckDB-pruned, and paginated behavior remains compatible.

Private pre-`1.0` team APIs and plan snapshots may gain an exact accuracy state,
structured classification category, and generalized semantic facts. Every
consumer changes coherently under this RFC. There is no public C++ ABI or
connector-author syntax migration.

Unsupported or missing capabilities always select conservative values. An old
or incomplete consumer must fail plan admission rather than assume a new enum
value means executable authority. Rollback before publication restores the
prior private interfaces and full traversal; it cannot reinterpret already
published connector packages because package authoring is not active.

## Evidence and bounded trials

| Question or claim | Evidence required | Method or fixture | Result and limitations |
| --- | --- | --- | --- |
| Does the pinned adapter retain every offered SQL filter? | Structured callback and plan/explain evidence with no expression erasure | RFC 0008 evidence and current complex-filter adapter tests | Established for DuckDB 1.5.4; broader compatibility remains release-matrix evidence |
| Can one supported conjunct safely narrow a larger conjunction? | `D => R` proof plus differential rows and request trace | Existing mixed-visibility/archived controlled fixture and expanded composed-query oracle | Established for the current visibility conjunct; the full projection/order/bound matrix remains delivery evidence |
| Must partial disjunction or non-exact negation fall back? | Counterexamples containing DuckDB-true rows excluded by the unsafe restriction, including child `D=FALSE`, `R=NULL` under negation | Query-reachable candidate algebra through the production decision function plus actual DuckDB evaluation over the same `TRUE`/`FALSE`/`NULL` rows | The contract follows from implication and three-valued semantics; executable exhaustive bounded-domain evidence is required during delivery |
| Can exact classification be proven without inventing a public mapping? | The production Semantics decision function consumes a Connector-validated exact fact with a distinct deterministic proof identity and proves three-valued equivalence plus occurrence-preserving equality of the base-domain bag while DuckDB retains ownership | Connector-owned controlled operation and duplicate-sensitive fixture through production validation and the same public decision API used by production | The metadata can represent exactness, but current validation correctly rejects the only installed evidence profile when relabeled; the distinct validated fixture is required delivery evidence, not a product claim |
| Does a logically safe Boolean approximation have an executable request encoding? | Operation-scoped encoding capability and negative composition cases | Production decision oracle with single positive input, incompatible safe conjuncts, union, and complement counterexamples | The native profile can encode only one positive conditional input. Unencodable `OR` and `NOT`, and ambiguous multi-input `AND`, must fall back |
| Are projection, ordering, limit, and offset available for delegation in the native profile? | Capability and request/plan source inspection | Current `ScanRequest`, adapter profile, planner validation, and accepted runtime contract | No. Full projection closure, empty ordering, unset bounds, and DuckDB ownership are the required conservative behavior |
| Can invalid contracts be distinguished from normal fallback before I/O? | Catalog/planner rejection and zero-runtime-entry probes | Existing validation and lifecycle fixtures, extended for the semantic matrix | Established for current validation paths; classification-specific failure coverage remains delivery evidence |

No decision-critical feasibility trial remains. The RFC chooses semantic laws
already required by the authoritative contracts and scopes their production
realization to the accepted native profile.

## Alternatives considered

### Add remote projection, ordering, and bound pushdown now

This could reduce transfer or early-close work. The native adapter does not
expose the required metadata with proven lifecycle semantics, and safe bounds
depend on exact filtering and ordering prerequisites. It would combine
independently valuable optimizations with semantic proof and contradict the
approved product scope.

### Add another public exact predicate mapping

This would provide an end-to-end exact request example. No product outcome or
upstream proof selected such a mapping, and choosing one would add public
behavior solely to exercise an internal state. The selected decision proves
exactness through the production Semantics API while retaining the existing
public mapping boundary.

### Keep GitHub-specific classification and add more tests around it

This is smaller, but it cannot prove a reusable protocol-neutral law or give a
future protocol consumer a bounded Semantics service. Query or Runtime would
continue accumulating meaning that belongs to Relational Semantics.

### Delegate residual evaluation to Runtime

This could support future profiles where DuckDB removes filters. It requires a
DuckDB-equivalent expression evaluator, changes limit and pagination behavior,
and materially expands Runtime authority. The accepted native profile retains
the filter in DuckDB, so this is unnecessary and excluded.

## Drawbacks and failure modes

- The decision adds semantic structure before a second public mapping consumes
  every state. Relational Semantics owns keeping the model bounded and proving
  that fixtures exercise the production decision function rather than a test
  reimplementation.
- Exact accuracy can be mistaken for residual-removal or limit authority.
  `ScanPlan` therefore records accuracy separately from ownership, and Runtime
  rejects unsupported ownership/delegation combinations.
- Logical composition can be mistaken for request composability. Connector's
  operation-scoped encoding capability is a separate prerequisite; Semantics
  fails closed rather than inventing a conjunction, union, or complement from
  one positive input.
- Row-wise predicate equivalence can hide a multiplicity-changing remote
  operation. Mapping proof and differential evidence therefore bind the
  restriction to the same duplicate-preserving base bag and compare duplicate
  occurrences, not only distinct values.
- A generic predicate tree can leak DuckDB or protocol details. The accepted
  algebra is deliberately bounded to typed comparison facts, Boolean structure,
  and opaque unsupported leaves. Query translates structure only; Connector
  mappings expose semantic declarations rather than REST construction
  internals; Semantics alone classifies and composes.
- Conservative full-column and local-order/bound execution may transfer more
  data. That is an explicit performance limitation, not a correctness defect or
  permission to infer unavailable metadata.
- Explain text can become a second interface. Structured plan facts own the
  behavior; rendered prose is safe, deterministic evidence and is never parsed.

## Acceptance and verification

- **End-to-end demonstration:** The approved composed query and a matrix of
  projection, supported/unsupported predicate, `NULL`, order, limit, and offset
  variants preserve the forced-local duplicate-sensitive bag. A total explicit
  `ORDER BY` must produce the same sequence; a non-total ordering must preserve
  ordered key groups while treating ties as bags. Unordered queries make no
  sequence promise, and `LIMIT` without total ordering is checked for ownership
  and absence of premature remote bounds rather than exact row identity. The
  supported conjunction narrows the controlled request trace. An executed
  fallback uses the unrestricted request shape and no remote bound; one
  exhausting fallback proves the complete trace, while a local limit may close
  the unrestricted scan early. DuckDB-pruned execution is proved with Runtime-
  open counters, and planning failures perform no Runtime work.
- **Automated oracle:** Query-reachable candidate algebra passes through the
  production Semantics decision function and is compared with actual DuckDB
  evaluation over identical rows containing `TRUE`, `FALSE`, `NULL`, and
  duplicate occurrences. It covers exact, superset, unsupported, ambiguous,
  and invalid classifications; `AND`, `OR`, `NOT`, and `NULL`; operation-scoped
  encoding/composability and negative unencodable cases; occurrence-preserving
  base-domain bags; the distinction between predicate/encoding fallback and
  operation-selection failure; residual ownership;
  projection closure; filter/order/bound prerequisites; adapter capabilities;
  deterministic reasons; and immutable copies. Connector validates the
  distinct exact fixture through its production rules. Query differential,
  Runtime plan admission, strict conversion, lifecycle, and regression suites
  cover their respective boundaries.
- **Quality gates:** Focused responsibility targets, `make build`, `make test`,
  `make demo`, `scripts/verify-source-identities.py`,
  `python3 -I -B scripts/test-native-dependencies.py`, a fresh
  `scripts/run-native-product-tests.sh` cell, agent-asset validation, and staged
  and unstaged whitespace checks.
- **Independent review:** Query/DuckDB lifecycle and explanation, Connector
  declaration integrity, Relational implication/composition, Runtime plan
  admission and lifecycle, test-oracle quality, and at least two fresh
  adversarial reviewers with relational and boundary/oracle perspectives.
- **Interaction exit:** Provider-focused targets expose bounded public test
  fixtures; consumers compile against provider APIs without listing provider
  production sources or importing private construction; the complete product
  oracle composes those services only in the named integration target.

## Contract propagation

| Source of truth or artifact | Impact | Required update | Completion evidence |
| --- | --- | --- | --- |
| `docs/ARCHITECTURE.md` | Affected during delivery | Record the delivered native composition matrix, exact capability without residual transfer, conservative ownership, and current limitations | Completed: native composition, operation selection, ownership, and installed-versus-controlled evidence boundaries are recorded |
| `docs/CONNECTOR_SPECIFICATIONS.md` | Not affected for public syntax; native mapping note affected during delivery | Keep package syntax inactive and the one public native mapping unchanged; record any generalized private mapping boundary without claiming author syntax | Completed: package syntax remains inactive; the private proof, encoding, selector, validation, and fixture boundary is recorded |
| `docs/RUNTIME_CONTRACTS.md` | Affected during delivery | Align the native mapping with exact/superset/unsupported decisions, structured fallback/failure, capability profiles, ownership, and explanation | Completed: typed candidate, decision, selector, ownership, explanation, and fail-closed admission contracts agree with production evidence |
| `docs/TEAM_TOPOLOGY.md` and active charters | Not affected | Existing producer/consumer responsibilities and interaction modes govern the change | Completed without charter edits: four charter-owned plans and the final source/target dependency audit support X-as-a-Service exits |
| `docs/PRODUCT_DELIVERY.md`, `AGENTS.md`, and skills | Not affected | Existing goal, RFC, contract-change, topology, delivery, and review practices apply | Completed without process edits; `validate-agent-assets.rb` passes |
| Examples, diagnostics, fixtures, tests, changelog, and goal record | Affected during delivery | Add the composed SQL narrative, differential and law matrices, safe explanation, release note, workstream plans, and completion evidence | Completed: implementation commit `9f0cb82`, independent re-review, interaction exits, and the successful commit-bound fresh native product cell are recorded in the completed goal |

The RFC records rationale; the authoritative contracts and executable evidence
must agree before the product goal can close.

## Unresolved questions

None. Additional public predicate mappings, residual removal, delegated
filtering, remote projection/order/bounds, and new adapter profiles remain
separate product or compatibility decisions.

## Review record

| Required reviewer | Team | Result | Evidence or objection | Disposition by decision owner |
| --- | --- | --- | --- | --- |
| `semantic_trust_query_review` | Query Experience | Approved | Final review confirmed duplicate-sensitive bag equivalence, sequence only under total ordering, tie-bag treatment for non-total ordering, and distinct unrestricted-shape, exhausting-trace, early-close, and zero-Runtime-pruning oracles | Initial syntactic-retention and mandatory-trace objections resolved; Query's delivery exit remains open until the final differential, explanation, request-shape, prepared-copy, and lifecycle matrix passes |
| `semantic_trust_connector_review` | Connector Experience | Approved | Final review confirmed Connector owns base-domain identity, occurrence preservation, proof identity, and operation-scoped encoding/composability; exactness uses a distinct validated duplicate-sensitive fixture and package syntax remains inactive | No objection; delivery must reject cross-domain, multiplicity-changing, incompatible-encoding, relabeled-exact, and private-construction cases and preserve the public read-only fixture boundary |
| `semantic_trust_semantics_review` | Relational Semantics | Approved | Final review confirmed logical proof is separate from executable encoding, native Boolean composition falls back conservatively, exact/superset preserve duplicate occurrences, operation ambiguity fails, and order/limit evidence follows SQL semantics | Initial single-candidate, negation, fixture-only-oracle, encoding, and bag-proof objections resolved; the service exit remains open until the production law matrix passes and consumers perform no reclassification |
| `semantic_trust_runtime_review` | Remote Runtime | Approved | Final review confirmed one typed conditional input remains the sole request authority, unencodable composition falls back, operation ambiguity fails before Runtime, ownership/bounds remain in DuckDB, and lifecycle evidence distinguishes exhaustion, early close, and pruning | No objection; delivery must extend fail-closed admission and public fixtures while preserving cancellation, resources, redaction, and zero-I/O invalid-state evidence |

## Decision and rationale

- **Technical decision owner:** Lead agent.
- **Product approval:** The product manager approved the trustworthy composed
  remote-query outcome, ordinary-SQL and explain success signals, local
  fallback, and exclusion of new public predicate mappings and remote
  projection/order/bound pushdown on 2026-07-19.
- **Rationale:** Accept. The bounded candidate algebra gives Semantics enough
  protocol-neutral structure to own composition without moving DuckDB
  lifecycle or Connector mapping knowledge into the subsystem. Three-valued
  exactness closes the negation counterexample, retained DuckDB ownership keeps
  bounds conservative, and the validated exact fixture proves the production
  decision state without inventing another public mapping. All affected teams
  approve the corrected decision.
- **Material objections:** Query Experience objected that the first draft
  promised syntactic retention and a full request trace even when DuckDB had
  simplified a predicate or pruned the scan. The decision now promises only
  that Query erases none of the expressions offered to it and distinguishes
  executed fallback from zero-Runtime pruning; focused re-review approved.
  Relational Semantics objected that a single preselected candidate could not
  support Semantics-owned composition, that true-set implication alone was
  insufficient for negation under `NULL`, and that an exact test path could
  bypass production validation. The decision now uses a bounded Query-produced
  algebra, Semantics-only composition, three-valued exactness, a distinct
  Connector-validated proof identity, and an actual-DuckDB production-path
  oracle; focused re-review approved. Independent adversarial review found that
  logical Boolean safety did not itself prove an executable request encoding,
  row-wise truth did not prove duplicate-preserving bag behavior, operation
  ambiguity could not be treated as ordinary predicate fallback, and exact
  sequence comparison would invent ordering for unordered or tied results. The
  final decision adds operation-scoped encoding capabilities, same-domain
  occurrence-preservation proofs, deterministic operation-ambiguity failure,
  and SQL-appropriate bag, total-order, tie-group, and limit oracles. It also
  separates unrestricted request shape, one exhausting trace, early local
  close, and zero-Runtime pruning. Both adversarial reviewers and every affected
  charter approved the final corrections. No unresolved objection remains.
- **Superseded by:** Not applicable.

## Follow-on goals

| Outcome or objective | Accountable team | Supporting teams and interaction modes | Activation condition |
| --- | --- | --- | --- |
| Deliver trustworthy composed remote queries and complete the `0.6.0` semantic-trust outcome | Query Experience | Relational Semantics — Collaboration then X-as-a-Service; Connector Experience and Remote Runtime — X-as-a-Service with bounded interface collaboration | RFC 0010 Accepted and product goal explicitly activated through `docs/PRODUCT_DELIVERY.md` |

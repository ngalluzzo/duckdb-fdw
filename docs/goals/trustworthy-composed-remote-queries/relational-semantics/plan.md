# Relational Semantics plan: trustworthy composed remote queries

## Outcome, authority, and boundary

Status: **Delivered; producer Collaboration exited and consumer service is
X-as-a-Service**.

Relational Semantics supports Query Experience's accountable product outcome by
providing one deterministic, protocol-neutral decision service from immutable
Connector facts and a bounded `ScanRequest` to a complete immutable `ScanPlan`.
The service owns operation selection, predicate matching and composition,
classification, residual ownership, projection closure, ordering and bound
prerequisites, conservative fallback, and structured semantic reasons. Planning
performs no DuckDB callback, secret resolution, environment or filesystem read,
Runtime construction, or network I/O.

[RFC 0010](../../../rfcs/0010-prove-conservative-relational-composition.md)
is the accepted decision record. This plan allocates its Semantics-owned source,
oracle, and service boundaries; it does not reopen the product choice or repeat
the RFC. The installed GitHub catalog retains one public Superset mapping for
`visibility = 'private'`. Exact classification is proved only by the distinct
Connector-owned controlled fixture. No work here authorizes another public
mapping, residual removal, Runtime filtering, remote projection or ordering,
remote or Runtime bounds, SQL-text recovery, or another protocol.

Query Experience remains accountable for the user's composed DuckDB result.
Relational Semantics is the supporting complicated-subsystem provider. The lead
agent retains technical integration authority, and public-policy changes remain
with the product manager under `AGENTS.md`.

## Bounded candidate and decision service

The current two-state `RequestedPredicate` becomes an immutable candidate tree
whose meaning is owned by Semantics and whose construction from DuckDB state is
owned by Query. Its public pre-`1.0` value contract is exactly:

- `TRUE`, meaning Query was offered no restriction relevant to the scan;
- one typed comparison leaf containing a relation-local bound output-column
  ordinal, its declared logical type, a closed comparison operator, and a typed
  non-secret scalar constant;
- ordered `AND`, `OR`, and unary `NOT` nodes; and
- an opaque unsupported leaf with a deterministic tree position, but no SQL
  text, DuckDB object, expression digest, function body, collation inference,
  request field, credential, or execution authority.

The initial comparison vocabulary admits equality over the logical types Query
can identify without coercion; unsupported operators, casts, functions,
unresolved values, unsafe bindings, and unrepresentable `NULL` behavior become
one opaque leaf at the affected position. The shared value service enforces a
maximum depth of 16 and 64 total nodes. Query replaces an over-limit subtree
with one opaque unsupported leaf. Semantics revalidates shape, arity, type, and
the limits defensively; a structurally malformed tree is an invalid contract,
not ordinary fallback.

`ScanRequest` keeps four concerns independent:

1. the candidate tree offered for remote selection;
2. whether the retained filter is absent, exactly the complete candidate tree,
   or a larger opaque complete DuckDB filter, always with DuckDB as owner;
3. the native full declared-column closure, empty ordering, and unset limit and
   offset; and
4. explicit adapter capability facts proving structured candidate inspection
   and retention of every expression Query was offered.

A candidate paired with the larger opaque complete-filter scope must itself
contain at least one opaque unsupported position. A fully represented tree uses
the exact-candidate retained scope; otherwise it could be an unsafe fragment of
an unseen outer `OR` or `NOT`. This structural coherence rule still permits a
represented safe conjunct such as `p AND Unsupported` to narrow remotely while
DuckDB evaluates the complete filter.

The request contains no residual evaluator, SQL text, DuckDB lifetime object,
remote operation input, or I/O authority. Query may simplify the tree only by
preserving its ordered logical meaning and opaque positions; it may not match
Connector declarations, count “safe” leaves, preselect an operation or input,
or assign accuracy.

The public production entry point remains the sole
`CompiledConnector + ScanRequest -> ScanPlan | PlanningError` service. It
returns one frozen decision in `ScanPlan` with:

- the selected base operation and base-domain identity;
- a typed remote restriction and its sole executable conditional input, or
  remote `TRUE` and no conditional input;
- a closed classification category, a stable reason code, and safe prose that
  is never parsed as authority;
- remote accuracy, retained filter scope, and DuckDB as the one residual owner;
- complete output-column closure and DuckDB ownership of final projection;
- empty remote and Runtime ordering and DuckDB ordering ownership;
- unset remote and Runtime limit/offset and DuckDB bound ownership; and
- all existing immutable authentication, network, pagination, resource, and
  lifecycle obligations narrowed to the selected operation.

The observable decision outcomes are deliberately distinct:

| Outcome | Plan behavior |
| --- | --- |
| `Exact` | Emits a declared typed restriction only after three-valued equivalence and exact occurrence preservation are proved; DuckDB still retains and owns the offered filter in the native profile. |
| `Superset` | Emits a declared typed restriction only after `D => R` and DuckDB-true occurrence preservation are proved; DuckDB owns the complete retained filter. |
| `Unsupported` | Emits remote `TRUE`, no conditional input, DuckDB ownership, and a reason identifying absent proof, capability, or encoding. |
| `Ambiguous` | Emits the same unrestricted safe plan, but records that more than one predicate mapping or input encoding remained possible after one base operation was selected. |
| `Invalid` | Returns `PlanningError` and no `ScanPlan` when an invariant, identity, ownership fact, capability combination, or operation decision is inconsistent; failure is deterministic and precedes Runtime entry. |

Accuracy and ownership are independent. In particular, `Exact` never removes a
DuckDB filter, grants Runtime filter evaluation, or makes a bound early in this
native profile. `Unsupported` and `Ambiguous` are successful unrestricted
plans; `Invalid` is not relabeled as fallback.

## Selection and composition laws

Semantics applies these laws through the production decision function, not a
test-only classifier:

- **Operation selection first.** Validate every declared operation, evaluate
  eligible non-fallback candidates using only Connector-declared selectors and
  mappings, rank by the accepted specificity and priority rules, and select
  exactly one. Use the single fallback only when no non-fallback operation is
  eligible. Equal-ranked eligible operations, multiple fallbacks, contradictory
  selectors, cross-domain mappings, or an incoherent ranking fail as `Invalid`.
  Semantics never chooses by declaration order. This operation-selection
  failure is separate from predicate/input ambiguity after an operation has
  been selected.
- **Leaf exactness.** `Exact` requires the DuckDB predicate `D` and declared
  remote predicate `R` to have the same `TRUE`, `FALSE`, and `NULL` result for
  every value in the declared domain, and the selected operation to return
  exactly the same base occurrences with the same multiplicities.
- **Leaf superset safety.** `Superset` requires every occurrence for which `D`
  is `TRUE` to be retained by `R` with the same multiplicity. Extra occurrences
  are permitted; loss, duplication, cross-domain substitution, or a
  multiplicity-changing operation is invalid declaration evidence.
- **Unsupported leaves.** An opaque or unmapped leaf contributes remote `TRUE`
  and remains inside the complete DuckDB-owned filter. It cannot be parsed,
  evaluated, or discarded by Semantics.
- **`AND`.** An unsupported child contributes remote `TRUE`. The native profile
  may emit at most one deterministically selected positive conditional input.
  Repeated occurrences may resolve to that one restriction only when their
  public mapping facts, typed binding, and proof identity are identical;
  otherwise multiple safe candidates are `Ambiguous` and produce remote
  `TRUE`. The classification of the complete conjunction is computed from
  complete `D` versus emitted `R`, not copied from a selected leaf; an exact
  leaf inside a larger conjunction is ordinarily a Superset restriction.
- **`OR`.** Every branch needs a safe approximation and the selected operation
  needs a declared union encoding that preserves occurrences and multiplicity.
  An unsupported branch or absent/incompatible union encoding produces
  `Unsupported` with remote `TRUE`. The native profile declares no union
  encoding.
- **`NOT`.** Complement is allowed only with `TRUE`/`FALSE`/`NULL` equivalence,
  or an explicitly total two-valued domain with complement-preserving
  equivalence, plus a declared complement encoding. Superset negation is never
  safe. The native profile declares no complement encoding and therefore uses
  remote `TRUE`.
- **Encoding is a separate proof.** Logical implication, three-valued
  equivalence, and occurrence preservation do not authorize a request.
  Semantics emits only an operation-scoped input encoding that Connector
  declares executable. Conflicting values, incompatible encodings, or more
  than one possible restriction after operation selection produce the explicit
  Ambiguous fallback.
- **No hidden reconstruction.** `NULL`, casts, functions, unsupported
  comparisons, unresolved parameters, unavailable capability state, and
  malformed bindings are never recovered from SQL, an explanation, an encoded
  request, or response values.

Duplicate candidate occurrences retain distinct tree positions even when they
bind the same declared input. Duplicate base rows retain distinct occurrence
identities throughout the proof. Neither kind of duplicate may disappear
merely because scalar values compare equal.

## Executable law oracle

Relational Semantics owns one focused law target that invokes the public
production decision service with Query-reachable candidate values and
Connector-owned validated fixtures. It does not reproduce matching,
classification, or input selection in test code.

The bounded truth domain contains rows whose candidate predicates evaluate in
DuckDB to `TRUE`, `FALSE`, and `NULL`, plus equal-valued duplicate rows carrying
distinct occurrence identifiers. The oracle evaluates `D` with the pinned
DuckDB engine over that domain, applies the decision's typed remote restriction
to the same duplicate-preserving base bag through the fixture's declared
semantics, then applies DuckDB's retained `D` to the remote bag. Assertions are:

- Superset: every DuckDB-true occurrence and multiplicity survives `R`, and
  remote-plus-DuckDB-residual equals DuckDB-only as a bag;
- Exact: the per-occurrence `D` and `R` `TRUE`/`FALSE`/`NULL` vectors and the
  selected occurrence bags are identical, while ownership remains in DuckDB;
- Unsupported and Ambiguous: `R` is `TRUE`, no conditional input exists, and
  DuckDB-only and remote-plus-residual bags are identical;
- Invalid: the production call fails deterministically, returns no partial
  plan, and a Runtime-open probe remains zero; and
- copies of the same connector, request, decision, and plan remain immutable
  and explain identically across locale and hostile ambient state.

The matrix covers `TRUE`, mapped and unmapped comparisons, exact and Superset
leaves, `AND`, `OR`, `NOT`, duplicate leaves, duplicate rows, `NULL`, the
`D=FALSE/R=NULL` negation counterexample, compatible and incompatible input
encodings, missing inspection or retention capabilities, cross-domain and
multiplicity-changing evidence, equal-ranked operation failure, and the
distinction between operation failure and predicate ambiguity. The permanent
GitHub mapping is never relabeled Exact; the Exact cases enter only through the
Connector-owned distinct proof identity after production validation.

For every successful category, the same target checks native relational
ownership: full output closure, DuckDB-owned retained filter and projection,
empty remote/Runtime ordering, unset remote/Runtime bounds, DuckDB order/limit/
offset ownership, and no authority inferred from source cardinality, page size,
or resource ceilings. Query's actual-DuckDB product differential owns final SQL
bags, order sequences, tie groups, and local-limit behavior; the Semantics law
target owns the plan prerequisites that make those outcomes safe.

## Source, test, and fixture ownership

Responsibilities are fixed even if implementation file names are tightened
during the coherent delivery:

| Artifact | Relational Semantics ownership |
| --- | --- |
| `src/include/duckdb_api/relational_predicate.hpp` and `src/semantics/relational_predicate.cpp` | Evolve the closed value into the bounded immutable candidate algebra, its limits, typed identities, structural validation, and safe snapshot. Query constructs these values but does not own their meaning. |
| `src/include/duckdb_api/scan_planner.hpp` | Preserve the sole deterministic public planning entry point and document inputs, output/failure, ownership, lifetime, concurrency, no-I/O guarantee, and private pre-`1.0` compatibility. |
| `src/semantics/predicate_classifier.*` or a focused replacement | Own candidate matching, complete-tree composition, encoding selection, `Exact`/`Superset`/`Unsupported`/`Ambiguous` decisions, and stable reason codes. It must not import DuckDB or Runtime implementation types. |
| `src/semantics/scan_planner_validation.cpp` and `scan_planner_internal.hpp` | Own operation eligibility/ranking, defensive request/declaration validation, invalid-versus-fallback separation, projection/order/bound prerequisites, and zero-partial-plan failure. |
| `src/semantics/scan_planner.cpp` | Compose the one selected operation, relational decision, ownership facts, and existing non-relational obligations into a complete immutable plan. |
| `src/include/duckdb_api/scan_plan.hpp`, `src/semantics/scan_plan.cpp`, and `scan_plan_explain.cpp` | Add Exact accuracy, structured classification/reason, typed restriction/conditional-input authority, retained scope, and explicit local ownership without turning snapshots into serialization. |
| `src/semantics/README.md`, `sources.cmake`, and `targets.cmake` | Keep the candidate value, plan value, and planner construction services separate and make their dependency direction discoverable. |
| `test/cpp/semantics/relational_predicate_tests.cpp` | Candidate construction, bounds, copy, typed identity, opaque-leaf, position, and safe-snapshot contract. |
| Focused Semantics composition-law target | The actual-DuckDB `TRUE`/`FALSE`/`NULL` and duplicate-occurrence matrix through the production decision function. This target links public Connector fixtures but no Query adapter or Runtime production sources. |
| `test/cpp/semantics/scan_planner_tests.cpp` | Operation selection/ranking failure, request and capability validation, classification/failure separation, full closure, local ownership, and immutable-copy coverage. |
| `test/cpp/semantics/scan_plan_contract_tests.cpp` | Closed plan states, Exact/Superset/fallback explanation facts, safe reason codes, independence from ambient state, and non-relational obligation preservation. |
| `test/cpp/semantics/support/` and `duckdb_api_semantics_fixture_service` | Public test factories for complete installed Superset, Unsupported, and Ambiguous `ScanPlan` values plus bounded invalid-plan admission counterexamples, including attempted Exact relabeling. Consumers receive only public const plan values and documented expected admission; fixture construction remains hidden. |

Connector Experience exclusively owns the `CompiledConnector` declaration
shape and validation, base-domain and occurrence evidence, operation-scoped
encoding capability, permanent mapping, and controlled Exact catalog fixture.
Query Experience exclusively owns DuckDB expression translation, callback
capability reporting, retained-expression behavior, bind-copy refinement, and
rendering of structured plan facts. Remote Runtime exclusively owns typed-plan
admission and execution. Semantics tests must not use Connector private test
access, Query expression constructors, Runtime request builders, or copied
provider production sources.

The public fixture boundary is a provider service, not a shared construction
toolkit. Its implementation links only the plan-value service, hides all
Semantics-owned construction, and exposes frozen installed-profile `ScanPlan`
values plus expected admission outcomes. It never relabels the installed GitHub
Superset mapping as Exact. Exact semantic proof remains exclusively the law
target's production planner call over Connector's validated controlled catalog;
because Runtime does not admit that controlled operation, the value-only
fixture service exposes attempted Exact relabeling only as invalid admission
counterexamples. Runtime tests link `duckdb_api_semantics_fixture_service`;
they do not link Connector, Query, or planner construction. Query integration
calls the public planner with its own `ScanRequest`; it does not use plan-fixture
factories as product behavior.

## Dependencies and observable interaction exits

The intended target direction is:

```text
Semantics candidate value <- Query request construction
Connector metadata -------\
Query ScanRequest ----------> Semantics planning -> immutable ScanPlan -> Query
Semantics candidate value --/                                      \-> Runtime

Connector fixture -> Semantics law target
private provider construction -> Semantics fixture service -> Runtime tests
```

- **Connector Experience — X-as-a-Service with bounded Collaboration.**
  Semantics depends on the public read-only declaration API for operation,
  domain, proof identity, occurrence preservation, accuracy, and encoding. The
  interaction exits when the permanent Superset and distinct Exact fixtures
  both pass production Connector validation; the law target imports no
  `ConnectorCatalogTestAccess` or Connector-private construction; and the
  focused Semantics target links only the metadata and fixture service targets.
- **Query Experience — Collaboration, then X-as-a-Service.** Semantics consumes
  only `ScanRequest`, the bounded candidate value, retained scope, full closure,
  and explicit capabilities. The interaction exits when every Query-reachable
  tree has a deterministic plan or failure, Query contains no mapping match,
  safe-candidate count, implication proof, operation selection, accuracy, or
  ownership logic, and Query targets link the public planner rather than list
  Semantics production sources.
- **Remote Runtime — X-as-a-Service.** Runtime consumes only immutable
  `ScanPlan` and may reject unsupported executable facts without reclassifying
  them. The interaction exits when focused Runtime targets link the plan value
  and Semantics fixture services only, import no `ScanRequest`, Connector,
  Query, classifier, or planner-private header, and prove Exact, Superset,
  unrestricted, ambiguous, and invalid-admission behavior without parsing
  explanation.
- **Internal Semantics service exit.** Candidate, plan-value, planning, law,
  and fixture targets have disjoint source inventories. The law target can run
  without Query adapter and Runtime production sources; the plan-value service
  can compile without Connector or Query; no focused consumer target directly
  compiles provider production files.

All exits remain **Open** until the final implementation-exit audit checks
declarations, includes, source inventories, link interfaces, fixture headers,
and tests. A passing end-to-end query or the existence of a named type is not
exit evidence.

## Documentation and verification

Adjacent API documentation must state semantic purpose and ownership, input and
output identities, tree bounds, three-valued and occurrence invariants,
operation-selection and fallback rules, lifetime/immutability/concurrency, no-
I/O behavior, error ownership, resource authority, and private pre-`1.0`
compatibility. Non-obvious `AND`/`OR`/`NOT`, encoding, duplicate, and bound
prerequisites belong beside the decision code. `src/semantics/README.md` must
identify the law and fixture targets and the exact consumer dependency rules.

The coherent delivery propagates the accepted laws and native limitations to
`docs/ARCHITECTURE.md`, the private native-mapping note in
`docs/CONNECTOR_SPECIFICATIONS.md`, `docs/RUNTIME_CONTRACTS.md`, safe plan and
Query explanation, examples, release notes, and the root goal completion
record. Those durable-contract and cross-workstream edits are lead-agent
integration responsibilities; this workstream supplies the Semantics facts and
evidence.

Required Semantics evidence is:

1. candidate value/boundary tests and the complete production-function law
   matrix over `TRUE`, `FALSE`, `NULL`, duplicates, Boolean composition,
   encoding, capability, and invalid-operation counterexamples;
2. planner and plan-contract targets covering full projection closure, local
   filter/projection/order/bound ownership, stable structured reasons,
   immutable copies, deterministic failure, and no ambient-state dependence;
3. fixture-service contract and consumer-boundary tests proving public plan
   values hide provider construction and are sufficient for Runtime admission;
4. Connector Exact/Superset fixture validation and Query/Runtime consumer
   contract tests supplied through their public services;
5. `make build`, `make test`, and `make demo`;
6. `scripts/verify-source-identities.py`,
   `python3 -I -B scripts/test-native-dependencies.py`, and a fresh
   `scripts/run-native-product-tests.sh /absolute/new/build-root debug` cell;
7. `ruby scripts/validate-agent-assets.rb`, `git diff --check`, and staged
   `git diff --cached --check`; and
8. independent Relational Semantics and adversarial review of implication,
   three-valued composition, occurrence preservation, operation failure,
   ownership, fixture oracles, and the final source/test dependency graph.

Completion requires every observable interaction exit above to be satisfied by
the final dependency graph and executable evidence, with no consumer semantic
reclassification and no weakening of the unrestricted safe baseline.

# Query Experience plan: predicate-selective repository scans

## Outcome, status, and authority

Status: **Delivered; Query outcome and all interaction exits satisfied**.

Query Experience is accountable for the DuckDB user's path from the existing
`github.authenticated_repositories` SQL relation to a correct, explainable,
predicate-selective result. For the approved product slice, the adapter will
expose the relation's new required trailing `visibility` `VARCHAR` column,
offer only the structured `visibility = 'private'` predicate to Relational
Semantics, consume the resulting complete immutable `ScanPlan`, keep DuckDB as
the sole evaluator of the original predicate, and expose the decision through
ordinary `EXPLAIN`. The supported query must return the same row bag as
complete traversal followed by local DuckDB filtering while the controlled
optimized trace contains fewer repository requests.

This plan records Query-owned implementation and evidence under Accepted RFC
0008. Query review approved the callback, lifecycle, fallback, and explanation
contract, and the implementation and interaction-exit evidence now pass. If the
accepted decision is superseded or materially revised, this plan must be
re-reviewed before product code establishes the changed contract. Query does
not decide connector mapping meaning, implication or accuracy, typed HTTP
inputs, pagination policy, or transport behavior.

After the additive schema change, the safe baseline remains a complete-schema
request including the trailing `visibility` column, `TRUE` remote predicate,
complete repository traversal, and DuckDB-owned filtering. Missing capability,
an unsupported or ambiguous DuckDB expression, an absent Connector mapping, or
a conservative Semantics result changes only optimization. It must not change
the selected schema, SQL syntax, secret behavior, errors, ordering, limit,
offset, duplicate preservation, or lifecycle behavior.

## Structured DuckDB complex-filter boundary

Under Accepted RFC 0008, registration will install the pinned DuckDB 1.5.4
`TableFunction::pushdown_complex_filter` callback while continuing to set
generic `filter_pushdown = false` and `filter_prune = false`. This is a narrow
optimizer advisory boundary, not a claim that the table function executes
DuckDB filters.

The callback will consume only the structured objects supplied by DuckDB: the
logical scan, its bind data, and the mutable vector of bound filter
expressions. It will not parse SQL, use `Expression::ToString` as input, inspect
an `EXPLAIN` rendering, reconstruct unavailable syntax, resolve a secret, read
the environment or filesystem, or perform network I/O. The Query adapter owns
only structural recognition and conversion:

- the expression is a bound equality in the accepted callback position;
- exactly one operand is the depth-zero column reference belonging to this
  `LogicalGet` and resolves by binding and ordinal to the declared trailing
  `visibility` output column whose logical type is exactly `VARCHAR`;
- the other operand is a non-`NULL` `VARCHAR` constant whose value is exactly
  `private`, with no cast or collation node remaining in the callback tree;
  pinned DuckDB may erase a same-type literal cast before this callback, in
  which case the resulting typed constant is intentionally indistinguishable
  from and equivalent to the accepted literal;
- either accepted operand order produces the same protocol-neutral value; and
- binding, return type, expression class, operator, constant type, and nullness
  are checked directly rather than inferred from spelling.

The focused conversion oracle will also cover wrong table bindings, wrong
column ordinals or types, any predicate on the distinct BOOLEAN privacy
column, other visibility literals, differently typed strings, typed and
untyped `NULL`, unresolved parameters, retained casts, collations, volatile or correlated
expressions, disjunction, negation, constants, unsupported operators, nested
or ambiguous shapes, duplicate accepted equalities, conflicting visibility
literals, and conjunction structure. Each vector element is considered
independently: one or more exact accepted equalities produce the same
idempotent candidate, while every other expression remains local and cannot
alter that candidate. An ambiguous shape that cannot be isolated as the exact
accepted callback element retains the baseline request. Representing a
counterexample structurally is not permission to push it remotely.

For the one recognized shape, Query constructs the accepted closed,
protocol-neutral `RequestedPredicate` and places it in a copied `ScanRequest`.
That value may identify the SQL column, logical type, operator, and literal,
but it contains no REST field, REST value encoding, operation, or request-path
choice. Query does not classify `Exact` or `Superset`, bind an operation input,
or assign residual ownership. Those decisions come only from the complete
Semantics plan built from the original request plus the structured predicate
and immutable Connector facts.

### Residual retention and capability reporting

The callback never erases, moves from, replaces, or rewrites any expression in
DuckDB's filter vector. Every supported and unsupported expression remains for
DuckDB to regenerate above the table scan. The accepted capability profile
must distinguish this selective structured advisory surface from generic
filter execution; it cannot change the current `filter` Boolean to an
overstated claim that Query evaluates arbitrary filters.

Query will admit a selective plan only through the accepted complete plan API.
The adapter may validate that the plan is compatible with its retained-local
profile, but it will not recompute accuracy or ownership. For this goal the
user-visible plan must say that the remote restriction is conservative and
that `DUCKDB` owns the complete original residual. A missing or incompatible
capability produces the baseline plan. A contradictory provider plan is a
redacted planning error rather than a partially applied plan or an invented
Query-side interpretation.

These rules preserve the required order: Runtime emits the selected base rows,
DuckDB applies the retained predicate, and only then does DuckDB apply local
ordering, limit, or offset. This work enables none of those additional
pushdowns.

## Bind, copy, prepare, explain, and execution lifecycle

The current `DuckdbApiBindData` retains only one `const ScanPlan`. The accepted
implementation must make the optimizer refinement window explicit without
making execution plans mutable:

1. **Registration:** function information owns only the immutable compiled
   connector and shared executor provider. Registration publishes the narrow
   callback and conservative capability profile; it creates no query state.
2. **Bind:** named arguments and relation identity are validated, the logical
   secret name is copied without resolving it, and Query builds the complete
   credential-free baseline `ScanRequest` and baseline `ScanPlan`. Bind data
   owns independent values for the original request, currently selected plan,
   and executor. Bind remains deterministic and I/O-free.
3. **Logical optimization:** the complex-filter callback derives any candidate
   request from the retained original request, never from a previously refined
   plan. It asks the sole Semantics planning entry point for a complete
   replacement and atomically replaces the selected immutable plan value only
   after successful validation. Repeated optimizer calls are idempotent; an
   unsupported later expression cannot accumulate or partially mutate remote
   inputs.
4. **Copy and prepare:** `FunctionData::Copy()` deep-copies the original request
   and selected plan values while sharing only immutable provider services.
   Prepared, explained, and concurrently executed statements cannot share a
   mutable selection cell. Preparing the supported constant is offline;
   execution resolves the exact current named temporary secret during global
   initialization as it does today. Parameterized or otherwise unstable shapes
   remain baseline/local.
5. **Explain:** the table-function explanation reads only the post-optimization
   selected plan in its own bind-data copy. It neither refines a plan nor
   resolves execution authority. Repeated `EXPLAIN` is deterministic, performs
   no request, and cannot expose logical secret names, credential material,
   row data, received URLs, or unstable expression text.
6. **Global initialization and scan:** initialization treats the selected plan
   as frozen, resolves and moves one execution-scoped authorization capability,
   and opens the unchanged Runtime executor. Pull, `DataChunk` population,
   single-thread stream ownership, cancellation, early destruction, close,
   exhaustion, and exception translation retain their current contracts.

No callback stores a DuckDB expression pointer beyond its call, and no request
or bind object stores `ClientContext`, `LogicalGet`, catalog handles, secrets,
mutable Runtime state, or page state. Exceptions from Query/provider code must
be contained at the existing DuckDB C++ callback boundaries and translated to
stable redacted diagnostics; no failed refinement may leave half-selected bind
state. No new C ABI, language FFI, reload, or shutdown surface is introduced.

The pinned optimizer invalidates unresolved parameters before invoking a
complex-filter callback, then may substitute an execution-bound parameter with
a typed constant during rebind. Query classifies only the structured expression
offered for that execution: exact non-null `VARCHAR 'private'` may refine from
the retained baseline request, while `public`, `NULL`, unbound, differently
typed, and unsupported values remain baseline/local. Query neither reconstructs
nor retains parameter provenance. No selected plan may leak from one prepared
execution, rebind, `EXPLAIN`, or copied statement into another.

## Query-visible explanation and compatibility

For the accepted SQL, `EXPLAIN` must report at least the relation, structured
remote predicate, conservative remote accuracy, and `DUCKDB` residual owner
from the selected plan. DuckDB's ordinary filter must remain visibly above the
scan. The rendering is a safe explanation, not serialization or execution
authority; Query must not parse it and Runtime must not consume it.

Fallback explanation must truthfully distinguish why the base plan was kept,
using stable structural reasons such as capability unavailable, expression
unsupported or ambiguous, or mapping unavailable. It must not echo SQL text,
secrets, tokens, headers, response bodies, or executable URLs. Existing
unfiltered queries, other relations, differently filtered queries, `DESCRIBE`,
preparation, and error wording remain compatible except for the new truthful
plan fields on the supported optimization. Named-column queries retain their
existing meaning and order. The new required `VARCHAR` value is extracted and
strictly converted with the other base-row fields, and is appended after every
existing column; `SELECT *` intentionally observes that pre-`1.0` additive
schema change.

## Source and oracle ownership

Concrete names may change during implementation, but the final source and test
graph must preserve these independently understandable responsibilities:

| Artifact | Query Experience responsibility |
| --- | --- |
| `src/include/duckdb_api/scan_request.hpp` and `src/query/scan_request.cpp` | Protocol-neutral request construction, exact adapter capability reporting, separate remote-candidate and retained-predicate-scope facts, deterministic safe snapshots, and a credential-free baseline/candidate request API; no DuckDB expression or provider implementation types |
| `src/query/duckdb/complex_filter_adapter.*` with a private header | Pinned DuckDB expression inspection, exact binding/type/constant checks, conversion to the accepted remote candidate, and truthful exact-versus-opaque complete residual scope; no mapping lookup, implication, operation input, request encoding, or Runtime dependency |
| `src/query/duckdb/table_function_plan_state.*` with a private header | Original request retention, selected immutable-plan replacement during optimization, deep copy isolation, freeze/read access, and safe explanation state |
| `src/query/duckdb/table_function_adapter.cpp` | Registration, bind, init, scan, callback composition, DuckDB exception boundaries, secret handoff, `BatchStream` consumption, and `DataChunk` output; provider details stay behind team APIs |
| `src/query/sources.cmake` and `src/query/targets.cmake` | Separate protocol-neutral request service from DuckDB adapter sources and keep dependency direction visible |
| `test/cpp/query/duckdb/complex_filter_adapter_tests.cpp` | Pinned callback-shape, expression-binding, non-erasure, capability fallback, and safe conversion oracle |
| `test/cpp/query/duckdb/table_function_plan_state_tests.cpp` | Baseline/refinement idempotence, strong failure behavior, deep copy, prepare/explain isolation, immutable execution handoff, and redaction oracle |
| Existing Query request, secret, adapter, auth/lifecycle, and stream-contract targets | Regression coverage for conservative profiles, offline bind, exact secret resolution, initialization, repeated/concurrent scans, cancellation, close, failure containment, and typed output |
| `test/sql/duckdb_api.test` | Installed SQL surface, `DESCRIBE`, supported and fallback `EXPLAIN`, retained DuckDB filter, `PREPARE`, ordering, and existing-relation regressions |
| `test/python/repository_pagination_product_contract.py` and its focused support package | Query-owned black-box mixed-visibility equivalence, public SQL, smaller trace, mapping-absent fallback, sequential and synchronized concurrent prepared executions, and installed-artifact demonstration |
| `src/query/README.md` | Registration-to-`DataChunk` lifecycle, callback compatibility, capability fallback, source/test map, and supported private pre-`1.0` interface |

The complex-filter and bind-state modules have separate reasons to change from
stream lifecycle. They must not be folded into an expanding adapter catch-all,
nor may a generic `core`, `common`, or `utils` module obscure ownership. Query
tests may compile Query adapter sources, but must consume Connector,
Semantics, and Runtime through their bounded public APIs or explicit provider
fixture services; they may not list those teams' private production sources.

## Query-owned acceptance evidence

### Focused adapter and lifecycle oracles

- Build bound DuckDB expression fixtures through the pinned optimizer surface,
  not by constructing a string that merely resembles SQL. Prove both operand
  orders for the one supported shape and the complete unsupported/ambiguous
  matrix above.
- Snapshot the filter vector before and after the callback and inspect the
  resulting logical/physical plan to prove no predicate was removed or
  transferred. `filter_pushdown` and `filter_prune` remain disabled.
- Prove a recognized candidate reaches the public Semantics planning service
  once with the original credential-free request, while an unsupported shape
  does not alter the baseline. Decoy Connector facts and exact/superset/
  fallback plan fixtures come from the owning provider services.
- Prove `Copy()` isolation before and after refinement, repeated optimizer
  invocation, concurrent prepared executions, statement destruction, failed
  refinement, and independent explanation state. Execution observes only a
  frozen plan.
- Use request/secret canaries and a no-I/O executor to prove bind, callback,
  `DESCRIBE`, `PREPARE`, and `EXPLAIN` perform no secret lookup, DNS, socket,
  HTTP, environment, or filesystem access. Execution continues to resolve the
  exact temporary secret at global initialization.
- Preserve existing success, late failure, cancellation, early close,
  exhaustion, connection teardown, and exception-containment tests. The
  Runtime `BatchStream` API and Query's `DataChunk` conversion do not change.

### Public product oracle

The controlled product fixture will expose a deterministic multi-page base
domain containing public, private, and internal rows, duplicates, and page
boundaries chosen so remote narrowing demonstrably eliminates unrelated pages.
The output `visibility` value and the provider-owned restriction must be driven
by the same fixture fact so an internal row is neither DuckDB-true nor expected
in the narrowed result. Two isolated DuckDB compositions will execute the same
approved SQL: the accepted
selective capability and an explicit provider-owned no-mapping/no-capability
fixture that forces complete traversal and local DuckDB filtering. The oracle
will compare ordered rows and row-bag multiplicities, not only sets or counts.
No test-only capability switch enters the installed SQL surface.

The selective trace must show the accepted canonical restriction on every
page and fewer requests than the forced-local trace. The product oracle may
observe paths and fixture responses, but it must not duplicate Runtime's URL,
Link, authorization, or pagination implementation; Remote Runtime separately
owns the exhaustive typed-request and continuation counterexamples. The public
installed-artifact demonstration then proves the existing secret syntax and
approved SQL against the controlled service, with the same result and bounded
trace.

Additional black-box cases prove that unsupported, ambiguous, `NULL`,
non-private or unbound parameter values, differently filtered, and
capability-absent queries issue the existing full traversal and remain
DuckDB-filtered. One prepared statement executed with
`private`/`public`/`NULL`/`private` proves sequential per-execution selection
and fallback. Two synchronized prepared executions then overlap private and
public values, proving independent plans, rows, and request traces. `ORDER BY id` remains
local and stable for evidence only; no remote ordering is claimed. `EXPLAIN`
shows remote superset plus DuckDB residual and makes zero requests, while
prepare is offline and repeated execution uses independent current
authorization.

## Dependencies, parallel work, and overlap control

1. **RFC gate:** satisfied by Accepted RFC 0008 and its Query review. Query's
   production callback, shared request state, explanation, and public behavior
   still require the implementation and interaction-exit evidence below.
2. **Semantics dependency:** Query needs the closed predicate value, complete
   `ScanRequest -> ScanPlan` service, adapter-capability vocabulary, residual
   ownership, safe explanation, and provider-owned positive/counterexample
   fixtures. Query must not copy implication or input-binding rules.
3. **Connector dependency:** Query registration consumes an immutable compiled
   catalog and fixture service. The callback neither reads mapping internals
   nor selects or infers a REST field, encoded value, operation input, or
   request path from the SQL column and literal.
4. **Runtime dependency:** Query consumes the existing low-friction
   `ScanExecutor`/`BatchStream` services and complete plan. It does not need a
   query-field encoder, pagination target, Link validator, transport, or
   authorization-policy type beyond the current execution handoff.
5. **Product integration:** controlled equivalence starts only after complete
   provider plans and Runtime execution fixtures exist. The lead owns shared
   contract propagation, root product composition, release records, and final
   Git integration.

After the RFC freezes the shared values, Query's structured DuckDB conversion
may proceed in parallel with Runtime's typed-request execution because they
consume opposite ends of the immutable plan. Within Query, the pure
`ScanRequest` capability work, DuckDB expression conversion oracle, and
controlled product fixture design may proceed in parallel. Bind-state
integration and `table_function_adapter.cpp` changes are serialized under one
Query owner.

Likely overlap files are `scan_request.hpp`, `scan_request.cpp`,
`table_function_adapter.cpp`, Query CMake inventories, SQLLogicTest inventory,
and the controlled repository service. Shared headers and source inventories
have one writer at a time. The lead integrates provider facades before Query
switches to them; no temporary request-string parser, relation-name branch,
locally assembled `ScanPlan`, or imported provider-private fixture is an
acceptable bridge.

## Interaction exit audit

Current state: **Satisfied**. The adapter recognizes only the pinned structured
equality, leaves DuckDB's filter vector untouched, and replans each deep-copied
bind state from an immutable baseline. Focused tests exercise the actual
`FunctionData::Copy()` boundary concurrently after destroying its ancestor;
SQLLogicTest and controlled product oracles prove offline planning, truthful
explanation, selective and compound execution, ambiguous and mapping-absent
fallback, parameter rebinding, exact rows, and exact request traces. Final
includes and targets preserve every provider boundary below.

Query Experience's collaborations exit only when final source and target
dependencies demonstrate all of the following:

- **Relational Semantics -> X-as-a-Service:** every capability profile and
  structured counterexample produces a complete conservative plan and truthful
  explanation through the public planning service. Query does not classify
  accuracy, select an operation/input, simplify a residual, or construct a
  provider-private plan.
- **Remote Runtime -> X-as-a-Service:** Query passes the complete immutable plan
  and moved authorization to the documented executor, then consumes only
  `BatchStream`. Query contains no request-field encoding, Link validation,
  transport, page-budget, or remote cancellation implementation.
- **Connector Experience collaboration exit:** the public SQL and Connector
  author/catalog narratives agree in one fixture, while Query consumes only
  the immutable compiled catalog API and never infers mapping semantics from a
  GitHub request or Connector-private construction.
- **Engineering Enablement facilitation exit:** Query Experience independently
  maintains the pinned DuckDB callback source identity, complex-filter
  compatibility oracle, bind/init/copy/prepare/explain/cancel/shutdown matrix,
  and its focused build gates.

The interaction reopens if Query tests compile Connector, Semantics, or
Runtime private production sources; if a provider fixture requires private
construction; if Runtime details appear in the callback; if plan meaning is
recoverable only from an explanation string; or if residual retention and copy
isolation are proven only by the cross-layer product test.

## Code and contract documentation obligations

Adjacent declarations must let a maintainer trace registration, bind, base
request construction, optimizer callback, selected-plan replacement, copy,
explanation, global initialization, stream pull, cancellation/close, exception
translation, and `DataChunk` output. They must state:

- the pinned DuckDB callback signature and supported version/source identity;
- which callback owns each object and why expression pointers cannot escape;
- the exact structured recognition checks and conservative fallback point;
- why leaving the filter vector unchanged preserves DuckDB residual ownership;
- the only mutable window for bind-state plan selection and the freeze/copy
  rules before execution;
- no-I/O and credential-free obligations for bind, optimization, prepare, and
  explain;
- lifetime, concurrency, cancellation, close, and exception-containment rules;
  and
- that Connector mapping, Semantics proof, and Runtime execution rationale
  remain behind provider APIs.

During delivery under Accepted RFC 0008, the coherent change must propagate the
accepted behavior in `docs/ARCHITECTURE.md`,
`docs/CONNECTOR_SPECIFICATIONS.md`, and `docs/RUNTIME_CONTRACTS.md`; update
Query's README and public explain/examples; and add controlled fixtures,
diagnostics, changelog/release notes, and roadmap evidence required by the
active goal. The RFC records the decision rationale, but cannot substitute for
adjacent lifecycle and compatibility documentation.

## Verification and completion evidence

Run narrow Query gates first: the request capability target, new complex-filter
and bind-state targets, existing adapter/auth/lifecycle/stream/secret targets,
SQLLogicTests, and the predicate-selective product contract. Then run the
supported development and product gates:

```sh
make build
make test
make demo
scripts/verify-source-identities.py
python3 -I -B scripts/test-native-dependencies.py
scripts/run-native-product-tests.sh /absolute/new/build-root debug
ruby scripts/validate-agent-assets.rb
git diff --check
git diff --cached --check
```

The native product runner uses a new build root. Stage only the intended
coherent goal before the cached-diff check so new files are included. Product
source or build changes require the fresh native cell; the authoritative
community/release gates are run only when their documented release scope is
affected.

Before completion, use the required adversarial review for DuckDB lifecycle,
FFI/callback safety, relational residual correctness, request security and
pagination, provider dependencies, and test-oracle independence. Resolve or
evidence-reject every finding, inspect the final source/test dependency graph
against each interaction exit above, review the complete diff for unrelated
changes, and commit the integrated goal with one coherent Conventional Commit.
The goal is not complete merely because the optimized SQL passes: baseline
equivalence, smaller trace, offline explanation/prepare, conservative fallback,
copy isolation, lifecycle regressions, contract propagation, RFC acceptance,
and actual interaction exits are all required.

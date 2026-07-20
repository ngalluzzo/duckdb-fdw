# Query Experience plan: trustworthy composed remote queries

Status: **Delivered; provider interactions exited to X-as-a-Service**

Charter: `docs/teams/QUERY_EXPERIENCE.md`
Accountability: Query Experience is accountable for the user-visible DuckDB
result, adapter lifecycle, explanation, and product-level composition evidence.
This plan implements the Query responsibilities accepted by RFC 0010; it does
not reopen that decision.

## Outcome

A DuckDB user can compose projection, predicates, ordering, limit, and offset
over the existing authenticated-repositories relation and receive the same
DuckDB result as a forced-local traversal. A supported conjunct may narrow the
remote request, while unsafe or unencodable shapes remain unrestricted. The
adapter leaves every offered filter expression with DuckDB and truthfully
explains what happened.

## Query-owned contract

### DuckDB candidate translation

Query owns a dedicated adapter from structured filter expressions exposed by
the supported DuckDB API to Relational Semantics' protocol-neutral
`PredicateCandidate` API.

- Query constructs the Semantics-owned candidate type; it does not define a
  parallel AST.
- `PredicateCandidate`'s hard limits are owned by Semantics: maximum depth 16
  and maximum node count 64. Query consumes those published constants or a
  bounded builder rather than copying numeric literals.
- No offered restriction becomes `TRUE`; only the absence of an offered
  restriction does.
- A safely bound output column, supported typed comparison, and typed constant
  become a comparison leaf without losing column or literal identity.
- Exposed `AND`, `OR`, and `NOT` structure is preserved. Structure that cannot
  be represented safely becomes an opaque unsupported leaf at its original
  position.
- Exceeding the shared depth or node budget collapses the complete candidate to
  one opaque unsupported candidate with a stable adapter fact. Query never
  truncates a Boolean tree or retains only a convenient branch.
- Casts, functions, unresolved parameters, unsupported operators, unsafe
  `NULL` forms, and missing adapter structure remain opaque. Query does not
  infer their meaning.
- Translation uses no SQL text, expression evaluator, Connector mapping,
  remote input name, proof identity, or Runtime authority.
- Query does not match leaves, prove implication, select operations, choose a
  conditional input, classify accuracy, or compose remote predicates.

Focused translation tests own structural fidelity, typed identity, binding,
unsupported-position preservation, boundary values at depth 16 and 64 nodes,
over-budget collapse, missing capabilities, and immutable request copies.

### `ScanRequest`

Query owns construction of one immutable, protocol-neutral `ScanRequest` with
separate fields for:

1. the constructed `PredicateCandidate`;
2. the scope of the filter DuckDB offered and Query left untouched, with
   DuckDB as semantic owner;
3. the complete declared column closure required by the adapter profile;
4. empty remote ordering and unset remote limit and offset; and
5. explicit capability facts for safe candidate inspection and residual
   retention.

The request contains no DuckDB object, SQL text, secret, remote request field,
or I/O handle. Query neither derives a remote operation nor mutates a returned
`ScanPlan`. If DuckDB simplifies or prunes the scan, Query constructs no
request and performs no Runtime open.

### Explanation

Query renders explanation only from the adapter facts and the complete
Semantics-owned `ScanPlan`. It reports:

- selected remote predicate and `Exact`, `Superset`, or unrestricted accuracy;
- the offered filter scope, that Query erased none of it, and DuckDB's residual
  ownership;
- complete projection closure and DuckDB projection ownership;
- DuckDB ordering, limit, and offset ownership, with no remote bound;
- capability fallback, or DuckDB-pruned execution;
- the structured classification category and safe reason returned by
  Semantics; and
- deterministic planning failure without suggesting a fallback ran.

Explanation is not parsed, serialized into a plan, or used by Runtime. Query
does not replace Semantics' category or reason with an adapter-local semantic
classification.

## Source and test ownership

Query changes remain within these responsibility-shaped seams:

- `src/query/duckdb/predicate_candidate_translation.*`: DuckDB expression to
  bounded candidate construction only;
- `src/query/duckdb/table_function_adapter.cpp`: callback wiring,
  `ScanRequest` construction, pruning boundary, stream consumption, and
  `DataChunk` production;
- `src/query/explain/scan_explanation.*`: query-visible rendering from typed
  adapter and plan facts;
- `src/include/duckdb_api/query/*`: the bounded Query team API, including
  `ScanRequest` and adapter capability facts, where not supplied by Semantics;
- `test/cpp/query/predicate_candidate_translation_test.cpp`: focused adapter
  translation and budget cases;
- `test/cpp/query/scan_request_test.cpp`: ownership, closure, capability,
  immutability, and pruning cases;
- `test/cpp/query/scan_explanation_test.cpp`: complete and non-overstated
  explanation facts; and
- `test/cpp/query/duckdb_composition_test.cpp`: the named actual-DuckDB product
  and differential integration oracle.

Exact filenames may follow the adjacent source convention during integration,
but responsibilities may not be folded back into one table-function module or
a monolithic cross-team test. Query-focused targets consume provider headers
and public fixture services; they do not list Connector, Semantics, or Runtime
production sources or import provider-private test construction. Only the
explicitly named DuckDB composition integration target links the whole graph.

## Actual-DuckDB product and differential oracle

The product oracle executes SQL in actual DuckDB against identical controlled
rows through two production-reachable paths:

- the composed path supplies the structured candidate to the production
  Semantics decision service; and
- the forced-local baseline disables remote candidate selection while keeping
  the same rows, schema, DuckDB-owned relational work, and Runtime fixture
  behavior.

Fixtures contain duplicate occurrences and values that make predicates
evaluate `TRUE`, `FALSE`, and `NULL`. Assertions follow SQL observability:

- unordered results compare duplicate-sensitive bags;
- a total explicit `ORDER BY` compares the complete sequence;
- a non-total ordering compares the ordered sequence of key groups and treats
  rows tied within each group as duplicate-sensitive bags;
- `LIMIT` or `OFFSET` without total order does not assert exact row identity;
  it proves DuckDB ownership, absence of a remote bound, valid output
  cardinality, and membership in the local result domain;
- projection tests prove the declared closure remains available for residual
  filter and ordering before DuckDB emits the requested columns; and
- the public composed-query narrative exercises projection, a supported
  conjunct, an unsupported conjunct, descending total order, and local limit.

The oracle records requests through the Runtime public fixture service:

- the supported, unambiguous conjunction selects the sole typed
  `visibility=private` conditional input and narrows the trace;
- `OR`, `NOT`, unsupported, capability-missing, and incompatible-safe-candidate
  fallbacks use the unrestricted request shape and carry no remote bound;
- one unrestricted fallback exhausts traversal and proves the complete request
  trace;
- a separate local-limit case may close the unrestricted stream early and
  asserts close behavior rather than a complete trace;
- a DuckDB-pruned query asserts zero Runtime-open calls, not merely zero remote
  requests; and
- deterministic planning failures assert zero Runtime opens and zero network
  work.

The actual table-function differential covers the production-installed
`Superset` mapping and unrestricted unsupported shapes. It executes identical
SQL through mapping-present and mapping-absent connections over two remote
views of one duplicate-preserving logical bag, then compares the complete
DuckDB result sequence for projection, `AND`, `OR`, `NOT`, total ordering,
local limit/offset, and row-level `NULL` outcomes.

`Exact`, `Ambiguous`, and operation-selection-invalid outcomes stop at a
different public boundary. The controlled exact operation is deliberately not
installed in the product Runtime; ambiguous planning has no uniquely selected
conditional input; and invalid operation selection produces no `ScanPlan` to
execute. Relational Semantics therefore owns their provider-backed evidence:
public Connector fixtures enter the production planner, and actual DuckDB
checks the resulting relational laws. Query consumes the returned typed
category, reason, and plan through public APIs and exhaustively tests truthful
explanation rendering, but does not invent an executable provider path for an
outcome that cannot authorize Runtime.

## Lifecycle evidence

Query owns deterministic tests for bind, repeated execution, scan
initialization, pull, cancellation, close, shutdown, exception translation,
and `DataChunk` output. Bind, candidate translation, request construction,
planning, and explanation perform no network I/O. Each executed scan opens at
most one immutable plan/stream instance, cancellation reaches the public
Runtime service, close is idempotent, and no callback retains DuckDB expression
objects beyond their valid lifetime. Provider errors cross the DuckDB boundary
as redacted actionable diagnostics.

## Dependencies and parallel delivery

Query consumes, without duplicating provider logic:

- Connector Experience's validated immutable mapping facts;
- Relational Semantics' bounded candidate type and limits, decision service,
  immutable `ScanPlan`, classification category, safe reason, and public plan
  fixtures; and
- Remote Runtime's bounded `BatchStream`, cancellation/close contract, and
  observable public fixture service with open and request counters.

Translation and SQL-oracle fixture scaffolding may proceed in parallel with
provider implementation against reviewed public headers. Final explanation,
request-trace, pruning, and lifecycle assertions wait for those provider APIs.
The lead agent owns root build registration, shared-contract integration,
release records, Git history, and the final whole-graph dependency audit.

## Documentation deliverables

- Document the callback state, lifetimes, cancellation, close, exception, and
  capability-fallback rules beside the Query declarations and implementation.
- Give the lead precise Query-owned contract text for `docs/ARCHITECTURE.md`
  and `docs/RUNTIME_CONTRACTS.md`; the lead integrates shared documents.
- Provide a developer-facing composed-query example and explanation output for
  the product documentation and release note, without conversation history or
  internal delivery narration.
- Record final Query evidence and interaction exits in the goal completion
  record.

## Verification

Query Experience runs, in order:

1. focused candidate-translation, request, explanation, and lifecycle targets;
2. the named actual-DuckDB composition integration target and its complete
   differential matrix;
3. `make build`, `make test`, and `make demo`;
4. `scripts/verify-source-identities.py` and
   `python3 -I -B scripts/test-native-dependencies.py`;
5. a fresh `scripts/run-native-product-tests.sh` cell; and
6. agent-asset validation plus staged and unstaged whitespace checks.

Independent review must cover DuckDB expression/lifetime safety, conservative
capability fallback, explanation truthfulness, SQL comparison oracles,
pruning/open counters, early-close behavior, and the Query/provider dependency
boundary.

## Interaction exit conditions

Query Experience is complete only when:

- ordinary composed SQL matches the forced-local baseline under the specified
  bag, sequence, tie-group, and local-bound assertions;
- the supported conjunction narrows work while every fallback and failure has
  the required request/open trace;
- DuckDB owns every offered residual plus final projection, ordering, and
  bounds, and Query erases no offered expression;
- Query constructs the shared bounded candidate and request without semantic
  matching, classification, remote encoding, or provider-private construction;
- explanation is complete, deterministic, safe, and never used as authority;
- lifecycle and FFI tests prove deterministic no-I/O planning, pruning,
  cancellation, close, shutdown, and error translation; and
- focused Query targets compile against bounded provider APIs while the named
  product integration target is the only whole-graph composition point.

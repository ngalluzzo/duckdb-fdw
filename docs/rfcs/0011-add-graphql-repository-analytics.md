# RFC 0011: Add GraphQL repository analytics

```yaml
rfc: "0011"
title: "Add GraphQL repository analytics"
status: "Accepted"
rfc_type: "Product"
sponsor_team: "Query Experience"
technical_decision_owner: "Lead agent"
product_approver: "Product manager"
authors:
  - "Lead agent"
required_reviewers:
  - "graphql_query_review"
  - "graphql_connector_review"
  - "graphql_runtime_review"
  - "graphql_semantics_review"
affected_teams:
  - "Query Experience"
  - "Connector Experience"
  - "Remote Runtime"
  - "Relational Semantics"
linked_outcome_or_objective: "Query repository analytics through GraphQL — prove or remove GraphQL for 0.7.0"
supersedes: "none"
```

## Summary

Add one authenticated native preview relation,
`github.viewer_repository_metrics`, backed by GitHub's GraphQL
`viewer.repositories` connection. Connector supplies one immutable canonical
query identity and document; Relational Semantics carries a typed GraphQL operation and
cursor plan; Remote Runtime executes JSON `POST` requests through the existing
authorization, network, budget, stream, cancellation, and close boundaries;
and Query exposes the relation only through the existing `duckdb_api_scan`
surface. The relation deliberately adds no connector-package syntax, generated
selection sets, arbitrary documents, introspection, mutations, remote
predicate mapping, retry, or cache behavior.

## Sponsorship and context

- **RFC type:** Product.
- **Sponsoring team:** Query Experience.
- **Linked outcome:** Query repository analytics through GraphQL and decide
  whether GraphQL remains in the v1 product claim for `0.7.0`.
- **Why now:** RFC 0009 and `ROADMAP.md` require one user-visible GraphQL
  relation to pass the permanent SQL, semantic, policy, resource, diagnostic,
  cancellation, and lifecycle boundaries before package lifecycle work begins.

The product manager approved the authenticated viewer repository-analytics
relation and the preview relation name `viewer_repository_metrics` on
2026-07-19. Internal protocol machinery alone does not satisfy the outcome.
Failure to meet this RFC's acceptance evidence narrows the v1 claim to REST
rather than leaving conditional GraphQL support unresolved.

## Problem

The installed native catalog and every executable plan currently contain only
REST `GET` operations. The shared `CompiledOperation` and `ScanPlan` models
embed one REST request shape, pagination models only REST `Link` transitions,
the transport request has no planned body, and the value handoff cannot
represent SQL `NULL`. Adding a GraphQL relation by disguising a document as a
REST body or by creating a relation-specific executor would make a successful
demo bypass the architecture the release is intended to prove.

The approved user scenario is:

```sql
SELECT full_name, stars, primary_language, updated_at
FROM duckdb_api_scan(
    connector := 'github',
    relation := 'viewer_repository_metrics',
    secret := 'github_default'
)
WHERE archived = FALSE
ORDER BY stars DESC
LIMIT 10;
```

This query must bind, describe, explain, and prepare without network or secret
resolution. At execution, it must traverse the authenticated viewer's
repository connection sequentially and let DuckDB own the complete filter,
ordering, and limit. GraphQL data, errors, cursors, and nullable nested fields
must cross the same bounded stream and failure boundary as the existing REST
relations.

Observed facts:

- GitHub GraphQL uses the fixed `https://api.github.com/graphql` endpoint,
  bearer authentication, and a JSON `POST` body containing a query.
- `viewer.repositories` is an authenticated repository connection; repository
  fields include `nameWithOwner`, `stargazerCount`, `primaryLanguage`, and
  `updatedAt`.
- GitHub connections require a bounded `first` or `last` value between 1 and
  100 and expose cursor state through `pageInfo`.
- The current native product supports only `BIGINT`, `VARCHAR`, and `BOOLEAN`;
  it does not need another scalar kind to expose the accepted relation when the
  GraphQL `DateTime` serialization remains a strict `VARCHAR` in this preview.

The first three facts are documented by GitHub's primary documentation:

- <https://docs.github.com/en/graphql/guides/forming-calls-with-graphql>
- <https://docs.github.com/en/graphql/reference/repos>
- <https://docs.github.com/en/graphql/guides/using-pagination-in-the-graphql-api>

## Decision drivers and invariants

- **Must preserve:** Bind, `DESCRIBE`, `EXPLAIN`, and `PREPARE` are
  deterministic and perform no network I/O or credential resolution.
- **Must preserve:** DuckDB remains the owner of filtering, projection,
  ordering, limit, and offset. The GraphQL operation declares no remote
  predicate, projection, ordering, or bound authority.
- **Must preserve:** A plan and connector snapshot are immutable. Runtime
  consumes typed operation, authorization, network, resource, and pagination
  authority rather than parsing explanation, relation names, or SQL text.
- **Must preserve:** Read-only and replay-safe authority comes from the admitted
  canonical GraphQL document identity and matching content digest, never from
  an independent operation-kind or replay-safety assertion.
- **Must preserve:** Connector network policy may only narrow host authority.
  The bearer value is placed only in the approved Authorization header for the
  exact planned origin and never enters a document, variables, plan,
  diagnostic, or continuation.
- **Must preserve:** Pagination is sequential and bounded. A received cursor
  grants no destination or document authority.
- **Must preserve:** A later-page failure fails the statement rather than
  turning already-produced batches into complete-looking success.
- **Must preserve:** GraphQL `errors`, type mismatches, missing required data,
  invalid cursors, resource exhaustion, and cancellation have explicit owners
  and cannot be interpreted as clean exhaustion.
- **Must preserve:** The complete serialized JSON request body, including the
  document, variables, cursor, and escaping expansion, is bounded before a
  bearer header is placed or transport begins.
- **Must enable:** One permanent user-visible relation using the same
  connector, semantics, runtime, Query, and `BatchStream` boundaries as REST.
- **Must enable:** Nullable nested scalar values without weakening strict
  conversion of non-nullable columns.
- **Must not introduce:** Connector-package or YAML syntax, arbitrary caller
  documents, schema introspection or import, generated selection sets,
  mutations, partial-data recovery, protocol-specific SQL arguments, retries,
  rate-limit waiting, caching, concurrent page requests, or resume state.

## Proposed decision

### Public behavior

The native GitHub connector adds the authenticated preview relation
`viewer_repository_metrics`. It is queried through the existing function:

```sql
FROM duckdb_api_scan(
    connector := 'github',
    relation := 'viewer_repository_metrics',
    secret := 'github_default'
)
```

Its fixed schema is:

| Column | DuckDB type | Nullability | GraphQL field |
| --- | --- | --- | --- |
| `id` | `VARCHAR` | required | `id` |
| `full_name` | `VARCHAR` | required | `nameWithOwner` |
| `owner_login` | `VARCHAR` | required | `owner.login` |
| `stars` | `BIGINT` | required | `stargazerCount` |
| `primary_language` | `VARCHAR` | nullable | `primaryLanguage.name` |
| `private` | `BOOLEAN` | required | `isPrivate` |
| `archived` | `BOOLEAN` | required | `isArchived` |
| `updated_at` | `VARCHAR` | required | `updatedAt` |

`id` is an opaque GraphQL node identifier rather than the numeric identifier
returned by the REST relation. `updated_at` preserves GitHub's validated
GraphQL `DateTime` serialization as `VARCHAR` for this preview; adding a new
DuckDB temporal conversion contract is not part of the protocol decision.

The relation returns the authenticated viewer's repository connection. It
makes no stable row-order promise and exposes no caller inputs other than the
existing connector, relation, and explicit secret selection. DuckDB owns every
SQL predicate and relational operator. Existing relations and their schemas
remain unchanged.

The fixed canonical GraphQL operation uses exactly this root invocation and
selection profile:

```graphql
query DuckdbApiViewerRepositoryMetrics($pageSize: Int!, $cursor: String) {
  viewer {
    repositories(
      first: $pageSize
      after: $cursor
      affiliations: [OWNER, COLLABORATOR, ORGANIZATION_MEMBER]
      ownerAffiliations: [OWNER, COLLABORATOR, ORGANIZATION_MEMBER]
      orderBy: {field: UPDATED_AT, direction: DESC}
    ) {
      nodes {
        id
        nameWithOwner
        owner { login }
        stargazerCount
        primaryLanguage { name }
        isPrivate
        isArchived
        updatedAt
      }
      pageInfo { hasNextPage endCursor }
    }
  }
}
```

`before` and `last` are absent because traversal is forward-only. `privacy`,
`visibility`, `hasIssuesEnabled`, `isArchived`, `isFork`, and `isLocked` are
absent and therefore apply no source-domain filter. The
three complete `RepositoryAffiliation` values are explicit rather than left to
an upstream default. Fixed `UPDATED_AT DESC` determines cursor enumeration but
grants neither a stable snapshot nor DuckDB-visible ordering authority.

The base domain is the duplicate-preserving occurrence bag of every repository
node returned by complete sequential traversal of that exact invocation for
the bearer-authenticated viewer. If mutable pagination returns the same node
more than once, every occurrence remains in the bag; neither Semantics nor
Runtime deduplicates. The operation has zero-to-many cardinality, zero
conditional-domain inputs, unsupported remote-predicate capability, and no
remote or Runtime relational delegation.

The page-info fields are execution metadata and never become rows. The
operation is a query only. The product does not accept caller documents,
variables, root fields, selections, arguments, or endpoint overrides.

Any nonempty top-level GraphQL `errors` array fails the statement, including a
response that also contains `data`. Partial-data recovery remains unsupported.
The diagnostic identifies the GraphQL response stage and a safe structural
field but includes no remote message, response value, cursor, document,
variable, credential, or repository data.

### Shared interfaces

Connector Experience replaces the REST-shaped operation payload with a closed
protocol-operation sum type. The GraphQL alternative contains:

- the closed document identity
  `GITHUB_VIEWER_REPOSITORY_METRICS_V1`, its exact canonical document, and a
  deterministic content digest;
- typed origin and path;
- non-sensitive fixed headers;
- closed integer page-size and nullable cursor variable bindings;
- typed response paths for `data.viewer.repositories.nodes`, top-level
  `errors`, and `pageInfo`; and
- sequential cursor-pagination declarations, per-request serialized-body
  ceiling, and aggregate scan-body ceiling.

Native catalog validation requires that the closed identity, exact canonical
document bytes, digest, variable bindings, response paths, columns, and cursor
declaration match one reviewed profile. Byte mismatch, an empty or oversized
document, an extra operation, a mutation or subscription, changed selection,
credential-bearing headers or variables, missing or overlapping response
paths, unsupported variable sources, unbounded page size, inconsistent cursor
declarations, and fields outside the closed native scalar/nullability profile
all fail validation. This proof uses equality to one repository-owned canonical
profile rather than trusting a separately supplied operation-kind flag or
building a general GraphQL parser. Accepting author-supplied documents requires
the later package compiler and is not authorized by this interface.

Relational Semantics copies validated GraphQL source facts into a distinct
`PlannedGraphqlOperation` alternative. `ScanPlan::Operation()` becomes a typed
protocol-operation sum rather than a REST accessor. A GraphQL cursor plan
contains the fixed page-size value, cursor variable name, typed response paths,
maximum page count, per-page budgets, and aggregate scan budgets. It contains
no mutable cursor or response state. REST plans retain their existing values
and behavior.

The planned GraphQL operation retains the closed document identity, exact
canonical bytes, and content digest. Planning derives replay safety from that
recognized query profile; no independent GraphQL replay-safety declaration can
authorize execution. An unknown identity or mismatched digest fails planning
without producing a `ScanPlan`.

The operation remains a complete base-row operation with unsupported remote
predicate accuracy, no conditional input, DuckDB-owned relational operators,
required bearer authentication, one exact network origin, and disabled retry,
cache, and providers. GraphQL does not add a new semantic classification path.

The immutable `ScanPlan` states the GraphQL repository-occurrence base-domain
identity, zero-to-many cardinality, zero conditional-domain inputs, unsupported
remote predicate, DuckDB-owned relational operators, and exact canonical
operation profile directly. Runtime and Query never inspect the document to
infer those facts.

Remote Runtime consumes the GraphQL alternative through the existing
`ScanExecutor` and returns the existing `BatchStream`. The shared HTTP request
gains a non-secret byte body and content type; the transport receives only an
already-admitted request. Runtime admission recognizes the canonical document
identity, recomputes and compares its digest, and validates the exact variable,
response, pagination, and resource profile. Runtime constructs the JSON
envelope from the admitted document, fixed page size, and current cursor,
measures the complete serialized body including JSON escaping, debits both the
per-request and aggregate body ceilings, and only then places the bearer header
and enters transport. It does not read connector metadata or relation names to
discover execution behavior.

The protocol-neutral typed row handoff adds nullable scalar representation. A
`TypedValue` carries its existing `ValueKind` plus an explicit validity state.
A null value retains the planned scalar kind. Runtime may produce it only for a
nullable planned column; null or missing data for a required column is a schema
failure. Query writes an invalid DuckDB vector entry for null and never invents
a sentinel value. Existing non-null REST batches remain source- and
behavior-compatible.

Query Experience keeps the current SQL function, bind data, immutable plan
retention, explicit secret resolution, and adapter lifecycle. It learns only
how to expose nullable planned columns, translate nullable typed values, and
render the GraphQL protocol and cursor facts in safe explanation.

### Operational behavior

The first request uses `first = 100` and `after = null`. On a successful page:

1. Runtime validates the complete JSON envelope and requires top-level `data`.
2. A nonempty `errors` array fails the scan before that page's rows are
   published.
3. Runtime decodes `nodes` incrementally under the planned schema and budgets.
4. When `hasNextPage` is false, the source is exhausted.
5. When `hasNextPage` is true, `endCursor` must be a nonempty string distinct
   from every cursor already used by the scan.
6. Runtime constructs the next body from the unchanged plan and that cursor;
   the origin, path, document, headers, page size, and bearer placement cannot
   change.

Pages are requested one at a time with concurrency one. The scan is limited to
32 pages, 100 decoded rows per page, and 3,200 decoded rows per scan, subject to
the existing host intersections for response bytes, decompressed bytes,
headers, memory, strings, JSON depth, batch rows, and wall time. Each page is
also subject to an 8 KiB connector serialized-request-body ceiling intersected
with a 16 KiB host ceiling, and the scan is limited to 256 KiB of serialized
request bodies. Each page is one replay unit because it uses the admitted
canonical query profile; automatic retry remains disabled. The connection is
treated as mutable and supplies no snapshot or ordering guarantee.

Cancellation is checked before authorization, before each request, during
transport, during decode, before publishing a batch, and before starting the
next page. `Cancel`, `Close`, destruction, success exhaustion, and every error
release the authorization capability and transport state through the existing
idempotent lifecycle.

GraphQL application errors receive a distinct safe remote-protocol diagnostic
category rather than being mislabeled as JSON syntax or schema conversion.
HTTP status, transport, authentication, authorization, decode, schema, policy,
resource, and cancellation ownership otherwise remain unchanged. Raw GitHub
messages are not passed through.

## Topology impact

| Team | Role in this RFC | Interface or charter impact | Interaction mode | Exit condition |
| --- | --- | --- | --- | --- |
| Query Experience | Sponsor and owner of the DuckDB-user outcome | Adds the preview relation, nullable adapter values, and safe GraphQL explanation | Collaboration | Query binds the bounded immutable `CompiledConnector`, produces `ScanRequest`, and consumes only the documented `ScanPlan`, authorization, executor, and `BatchStream` team APIs without Connector or Runtime internals |
| Connector Experience | Provider of immutable relation and protocol facts | Adds the validated GraphQL operation alternative and native relation | Collaboration, then X-as-a-Service | Semantics consumes one bounded compiled-operation API; Query and Runtime do not construct or inspect Connector internals |
| Remote Runtime | Provider of bounded GraphQL execution | Adds JSON-body requests, GraphQL envelope/error handling, and sequential cursor streaming | Collaboration, then X-as-a-Service | Query opens the standard executor and consumes `BatchStream`; Runtime needs no Query or Connector internals or relation-specific entry point |
| Relational Semantics | Provider of protocol-neutral planning and correctness | Adds the typed protocol-operation and cursor-plan alternatives while preserving relational ownership | X-as-a-Service | Query and Runtime use one immutable `ScanPlan`; GraphQL requires no new predicate or relational-ownership classification |

No team accountability moves. The temporary collaboration closes only when
source targets, test fixtures, and includes demonstrate these dependency
directions; names and passing end-to-end tests alone are insufficient.

## Correctness, security, and lifecycle analysis

- **Relational semantics and conservative fallback:** GraphQL declares no
  pushdown capability. Complete sequential traversal of the canonical root
  invocation is the duplicate-preserving base-domain bag, including repeated
  occurrences from mutable traversal, and DuckDB owns all filtering,
  projection, ordering, limit, and offset. The relation uses the existing
  unsupported remote-predicate result and cannot authorize a conditional
  input.
- **Authentication, credentials, network policy, and privacy:** The existing
  explicitly named temporary bearer secret is resolved only for execution and
  scoped to `https://api.github.com:443`. The token cannot enter the GraphQL
  document, variables, cursor, logs, snapshots, fixtures, or errors. Local
  deterministic fixtures use synthetic credentials and data.
- **Resource budgets, backpressure, and cancellation:** Every page and whole
  scan intersect connector ceilings with host ceilings. Cursor traversal is
  demand-driven by `BatchStream::Next`; it does not prefetch or retain the
  complete relation. Serialized request bodies are measured and debited before
  bearer placement or transport. Cancellation remains terminal and distinct
  from clean exhaustion.
- **Replay units, retries, caching, and duplicate prevention:** Each page is a
  replay-safe query unit only because admission recognized the canonical query
  identity, bytes, digest, and variable profile; no independent flag supplies
  that authority. No automatic retry or cache is enabled.
  Mutable upstream data may produce duplicates or misses across pages; the
  product makes no snapshot guarantee and preserves every received occurrence.
  Repeated cursors are rejected to prevent loops, not to deduplicate rows.
- **Concurrency, immutability, and state ownership:** Plans and connector
  snapshots are immutable and may be shared. Each stream exclusively owns its
  current cursor, seen-cursor set, transport, decoder, budgets, and moved
  authorization capability. Page concurrency is one.
- **FFI, initialization, reload, shutdown, and failure containment:** No new
  public FFI or initialization contract is introduced. Nullable values cross
  the existing C++ team API and DuckDB adapter. Stream cleanup remains
  idempotent and non-throwing across close, cancellation, failure, and
  destruction.
- **Diagnostics, redaction, metrics, and progress:** Explanation reports the
  protocol, fixed endpoint identity, query-only kind, cursor strategy, and
  bounds without the document or variables. Runtime failures report stable
  stages and structural fields; remote response content and cursor values are
  redacted. No new telemetry sink is introduced.

## Compatibility and migration

Existing SQL, relation names, schemas, connector snapshots, REST request
shapes, and execution behavior remain available. `viewer_repository_metrics`
is a new pre-1.0 preview relation, and RFC 0009 still requires the v1 SQL and
naming surface to be decided before `0.8.0`; this RFC does not promote the name
to a 1.0 compatibility promise.

The shared C++ types are private pre-1.0 team APIs, not a public native ABI.
Their REST alternatives retain the same semantic values, but internal
consumers must switch from a REST-only accessor to exhaustive protocol
handling and must honor nullable typed values. There is no stored data or
connector-package migration because package loading is not yet available.

Rollback before `0.7.0` removes the new relation and GraphQL alternatives and
narrows the v1 claim to REST. After release, disabling or removing the relation
is an incompatible public-SQL change: it requires an RFC and the release
classification required by `ROADMAP.md`. An urgent security or correctness
containment follows only the scoped exception in `docs/RFC_PROCESS.md`; it does
not pre-authorize a durable patch-level removal. Unsupported protocol facts
fail closed before network activity; they never fall back to another protocol
or an anonymous request.

## Evidence and bounded trials

| Question or claim | Evidence required | Method or fixture | Result and limitations |
| --- | --- | --- | --- |
| The upstream API provides the accepted query, fields, bearer placement, and cursor contract | Primary-source API evidence | GitHub GraphQL call, repository-schema, and pagination documentation linked above | Confirmed for GitHub.com; live compatibility remains release evidence, not the correctness oracle |
| The pre-delivery architecture did not provide a GraphQL operation or nullable value handoff | Acceptance-time source inventory | Inspect the pre-implementation `connector_catalog.hpp`, `scan_plan.hpp`, `execution.hpp`, transport request, and Runtime admission | Confirmed at RFC acceptance: the installed operation was REST `GET`, pagination was `Link`, requests had no planned GraphQL body, and `TypedValue` was non-null; the delivered rows below supersede that baseline |
| A GraphQL relation can use the permanent team APIs without relation-specific Query or Runtime entry points | Low-coupling source and target evidence | Connector fixture -> Semantics plan -> generic Runtime executor -> Query adapter controlled-service test | Delivered: the actual-DuckDB product target composes the provider services through the existing scan, executor, and stream APIs; the final target audit found no consumer-compiled provider production source |
| Every team agrees on the same relational base domain without parsing the document | Exact Connector-to-Semantics source-domain oracle | Assert canonical root invocation and omitted-filter profile, zero-to-many cardinality, zero conditional inputs, unsupported remote predicate, duplicate-preserving GraphQL repository-occurrence domain, and unchanged immutable plan snapshot | Delivered: focused Semantics profiles and variation/counterexample tests prove the immutable occurrence-bag domain without granting consumers document authority |
| Only the reviewed read-only operation can reach transport | Canonical authority and negative admission evidence | Exact identity/bytes/digest oracle plus query-labeled mutation, subscription, extra-operation, changed-selection, and digest-mismatch fixtures | Delivered: Connector validation and Runtime admission reject the negative authority corpus before authorization placement or I/O |
| Cursor and error variations fail without partial-looking success | Deterministic protocol corpus | Controlled HTTPS-compatible service with zero, one, and multiple pages; nullable data; top-level errors; data plus errors; missing pageInfo; empty/repeated/oversized cursor; malformed types; oversized document and serialized-body overflow; cancellation and exhausted budgets | Delivered: the bounded Runtime corpus and product-level error/cancellation paths are terminal, redacted, and retain no partial-looking success |
| Existing REST behavior is unchanged | Regression and differential evidence | Existing native C++, SQLLogicTest, controlled-service, and clean-host direct-load gates | Delivered: retained 20-, 20-, and 122-request product suites, 159 SQL assertions, `make test`, `make demo`, and the fresh native cell passed |

No bounded throwaway implementation is required to decide the contract. The
first executable slice after acceptance will be permanent source code and will
exercise a deterministic first page before cursor traversal is added.

## Alternatives considered

### Narrow v1 to REST now

This avoids GraphQL-specific request, error, pagination, and nullability work
and minimizes shared-interface change. It leaves the core multi-protocol value
claim untested despite an existing approved design and a representative
product path. RFC 0009 intentionally reserves this as the required outcome if
the accepted evidence cannot be achieved; it is the rollback, not the selected
starting decision.

### Implement generic GraphQL package compilation first

Generated selection sets, schema validation, author-defined documents, and
package syntax would exercise a broader future surface. They would combine the
`0.7.0` protocol question with the `0.8.0` authoring and package-lifecycle
question, substantially increase Connector cognitive load, and risk delivering
compiler infrastructure instead of a user-visible relation. This RFC chooses
one repository-owned prewritten query and keeps authoring unavailable.

### Encode GraphQL as REST `POST` with an opaque body

This could minimize type changes but would force Semantics and Runtime to infer
GraphQL response, error, and cursor behavior from strings or relation identity.
It contradicts the accepted architecture statement that GraphQL is a separate
compiler and would not prove a reusable protocol boundary.

### Add a relation-specific GraphQL executor

A dedicated `OpenViewerRepositoryMetrics` path could reach GitHub quickly. It
would couple Query to Runtime internals, duplicate authorization and lifecycle
logic, and evade the `ScanPlan` and `BatchStream` interactions that `0.7.0`
exists to prove. It is rejected even as an intermediate product implementation.

### Generate projection-specific GraphQL documents

Omitting unrequested fields is a valid future optimization. DuckDB's current
native adapter does not delegate projection and the approved goal requires
semantic trust, not remote projection pushdown. The fixed selection keeps the
base domain and response schema stable while GraphQL earns its protocol place.

### Accept partial GraphQL data with warnings

This could return useful rows when nullable fields fail remotely. It adds a
diagnostic channel and requires proof that error paths cannot alter
cardinality, predicates, or required values. The preview instead fails every
response containing GraphQL errors; a later RFC may add validated partial-data
policy after the package and diagnostic surfaces exist.

## Drawbacks and failure modes

The decision adds a second protocol operation model, JSON request bodies,
cursor state, GraphQL error classification, and nullable values before the
package compiler exists. Connector must keep the native document and field
mapping coherent; Semantics must keep protocol alternatives exhaustive;
Runtime must own more response-envelope logic; Query must translate nulls
without learning GraphQL internals.

GitHub's mutable connection can change while pages are traversed, so rows may
be duplicated or missed. The product preserves received occurrences and makes
no snapshot guarantee. API schema or permission changes can break the live
relation; deterministic fixtures remain the correctness oracle and a
privacy-safe live probe detects upstream compatibility drift.

A cursor loop, malformed pageInfo, successful HTTP response with GraphQL
errors, nullable value in a required field, oversized response, late
cancellation, or stream destruction after partial output can expose lifecycle
defects. Each must be a deterministic terminal case in the Runtime corpus and
the end-to-end adapter tests.

The preview relation name and `updated_at` string type may change before v1
under the RFC 0009 naming and package-version decision. README and release
notes must identify that compatibility status instead of implying stability.

## Acceptance and verification

- **End-to-end demonstration:** From the installed extension, create a
  temporary named secret, describe and explain the relation offline, query the
  authenticated viewer's repository metrics through multiple pages, and run a
  filtered, ordered, bounded DuckDB query. A controlled GraphQL error and
  cancellation both fail safely without credentials or response data in the
  diagnostic.
- **Automated oracle:** Connector validation and snapshot tests, including
  rejection of a query-labeled mutation, subscription, extra operation,
  changed document, changed root argument, omitted-filter drift, and oversized
  document; Semantics canonical identity/digest/replay, exact base-domain,
  zero-input, unsupported-predicate, cardinality, and pagination plan tests;
  Runtime GraphQL request,
  response, cursor, nullable, request-body budget, resource, cancellation, and
  cleanup corpus, including oversized cursor and serialized-body overflow
  before authorization or I/O; Query bind, prepare, repeated scan, schema,
  null, diagnostic, and end-to-end differential tests; existing REST
  regression suites.
- **Quality gates:** `ruby scripts/validate-agent-assets.rb`, both diff checks,
  `make build`, `make test`, `make demo`, source-identity and native dependency
  gates, and a fresh `scripts/run-native-product-tests.sh` root. Lifecycle,
  network, credential, and FFI-adjacent changes also require the relevant
  sanitizer evidence on the supported cell.
- **Independent review:** Query Experience, Connector Experience, Remote
  Runtime, Relational Semantics, plus adversarial review of security, resource,
  cursor, cancellation, nullability, and stream terminal behavior.
- **Interaction exit:** The final source/target audit must show Query binding
  the bounded immutable `CompiledConnector`, producing `ScanRequest`, and
  consuming only the documented `ScanPlan`, authorization, executor, and batch
  APIs; Runtime consuming no Connector or Query internals; Semantics owning all
  plan construction; and Connector fixtures proving the provider API without
  consumer-private constructors.

## Contract propagation

| Source of truth or artifact | Impact | Required update | Completion evidence |
| --- | --- | --- | --- |
| `docs/ARCHITECTURE.md` | Affected | Mark the closed prewritten native GraphQL profile and nullable row handoff as implemented while retaining generated mode as design | Complete in the `0.7.0` contract propagation commit |
| `docs/CONNECTOR_SPECIFICATIONS.md` | Affected | Distinguish the implemented native metadata subset from future author-facing GraphQL syntax and record fail-only partial data | Complete in the `0.7.0` contract propagation commit |
| `docs/RUNTIME_CONTRACTS.md` | Affected | Specify canonical document admission, replay derivation, the executable GraphQL operation, cursor transition, error policy, nullable values, request/response budgets, and lifecycle | Complete in the `0.7.0` contract propagation commit |
| `docs/TEAM_TOPOLOGY.md` and active charters | Not affected | Accountability and team APIs already cover this interaction; audit exits against implementation | Complete; each workstream plan records a Satisfied X-as-a-Service exit |
| `docs/PRODUCT_DELIVERY.md`, `AGENTS.md`, and skills | Not affected | Existing goal, RFC, contract-change, delivery, and review workflows apply | Current validation |
| README, changelog, release notes, examples, diagnostics, fixtures, and tests | Affected | Document and prove the user-visible relation, preview compatibility, GraphQL failure behavior, and retained REST behavior | Complete; the `0.7.0` release integration records the synchronized product evidence |

The implementation must apply `$contract-change` so architecture, connector,
runtime, examples, diagnostics, and tests agree in the same delivery.

## Unresolved questions

- The later v1 SQL/naming RFC may retain or replace
  `viewer_repository_metrics`; this does not block the explicitly preview
  `0.7.0` surface.
- A later package-surface decision may map GraphQL `DateTime` to a native
  temporal type. This relation deliberately exposes the upstream serialization
  as strict `VARCHAR` and does not pre-decide that authoring contract.
- Generated selection sets and author-provided explicit documents remain
  separate post-proof options, not implied follow-on commitments.

## Review record

| Required reviewer | Team | Result | Evidence or objection | Disposition by decision owner |
| --- | --- | --- | --- | --- |
| `graphql_query_review` | Query Experience | Approved | Re-review confirmed the corrected rollback classification and the documented `CompiledConnector -> ScanRequest -> ScanPlan -> BatchStream` dependency; final delivery audit satisfied the interaction | Initial objections accepted and both passages corrected before approval |
| `graphql_connector_review` | Connector Experience | Approved | The closed immutable operation profile matches Connector's provider API without exposing package syntax; final provider-fixture and dependency audit satisfied the interaction | Approved with the required final audit completed |
| `graphql_runtime_review` | Remote Runtime | Approved | Re-review confirmed canonical identity/bytes/digest admission, derived replay safety, and serialized-body budgets before bearer placement or I/O; final delivery audit satisfied the interaction | Initial authority and body-budget objections accepted and the contract corrected before approval |
| `graphql_semantics_review` | Relational Semantics | Approved | Re-review confirmed the exact root invocation, omitted-filter semantics, duplicate-preserving base domain, cardinality, zero inputs, unsupported predicate, and immutable plan oracle; final delivery audit satisfied the interaction | Initial missing-evidence finding accepted and the source-domain contract corrected before approval |

## Decision and rationale

- **Technical decision owner:** Lead agent.
- **Product approval:** Product manager approved the authenticated viewer
  repository-analytics relation and preview name on 2026-07-19.
- **Rationale:** Accepted. One canonical repository-owned GraphQL query proves
  the second protocol through the permanent product boundaries without
  prematurely accepting a connector-authoring surface. Exact document and
  base-domain identity prevent protocol or relational reinterpretation;
  request and response budgets, fail-only GraphQL errors, sequential cursors,
  explicit nulls, and the existing authorization/stream lifecycle preserve the
  accepted safety envelope. Primary GitHub documentation, current-source
  inventory, and all four affected-team reviews support the decision.
- **Material objections:** Query's rollback/versioning and interaction-exit
  objections were resolved by removing patch-disable authority and restoring
  the bounded Connector bind dependency. Runtime's opaque-document/replay and
  outbound-body-budget objections were resolved with canonical
  identity/bytes/digest admission, derived replay safety, hard body ceilings,
  and pre-I/O negative fixtures. Semantics' missing exact-domain evidence was
  resolved by pinning the root arguments, omitted filters, occurrence bag, and
  Connector-to-Semantics oracle. No material objection remains.
- **Superseded by:** Not applicable.

RFC acceptance alone was not implementation completion. The linked goal now
records the completed implementation, verification, interaction exits, and
decision to retain GraphQL in the intended v1 surface.

## Follow-on goals

| Outcome or objective | Accountable team | Supporting teams and interaction modes | Activation condition |
| --- | --- | --- | --- |
| Deliver `0.7.0` GraphQL repository analytics and record the retain-or-narrow v1 decision | Query Experience | Connector Experience and Remote Runtime — Collaboration then X-as-a-Service; Relational Semantics — X-as-a-Service | RFC 0011 Accepted and approved product surface recorded |

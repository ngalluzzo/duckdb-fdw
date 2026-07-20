# Goal: Query repository analytics through GraphQL

Follow `docs/PRODUCT_DELIVERY.md`.

Status: **Complete**. [RFC 0011](../rfcs/0011-add-graphql-repository-analytics.md)
is Accepted and its delivery evidence is recorded below.

## PM brief

### Outcome

For DuckDB users analyzing their GitHub repositories, enable a typed
repository-analytics relation through ordinary SQL so that GraphQL either
earns its place in the v1 product or is decisively removed.

### Why now

`0.6.0` proved trustworthy relational composition. Before investing in
package authoring and distribution, `0.7.0` must establish whether a second
protocol can use the permanent product architecture without bespoke authority,
semantics, or lifecycle machinery.

### Product guardrails

- Must deliver the permanent native `viewer_repository_metrics` relation with
  the same correctness and safety expectations as REST.
- Must not substitute generic GraphQL infrastructure, an importer, YAML
  compilation, or throwaway experimental code for product delivery.
- Preserve existing REST behavior, offline binding and planning, conservative
  relational semantics, bounded execution, and secret isolation.

### Success signals

- An authenticated DuckDB user can query repository identity, ownership,
  stars, nullable primary language, visibility, archive state, and update time.
- Filtering, ordering, limits, joins, preparation, and repeated execution
  retain normal DuckDB semantics.
- Cursor traversal, cancellation, resource limits, authentication failures,
  GraphQL errors, and nullable nested data produce bounded, redacted,
  actionable behavior.
- The completed evidence records an explicit decision to retain GraphQL in v1
  or narrow v1 to REST.

### Reserved product decisions

The product manager approved the authenticated GitHub
`viewer.repositories` analytics relation and preview name
`viewer_repository_metrics` on 2026-07-19. RFC 0011 records the exact public
schema and compatibility status.

## Agent commitment

### Observable interpretation

A user creates a temporary named DuckDB secret and queries
`viewer_repository_metrics` through `duckdb_api_scan`. DuckDB returns strictly
typed repository rows across sequential GraphQL cursor pages. A nullable
primary language becomes SQL `NULL`; failed authentication, GraphQL errors,
invalid cursors, cancellation, and exceeded budgets fail safely without
leaking credentials, cursors, documents, variables, remote messages, or row
data.

### Acceptance evidence

- Demonstration: the installed native extension binds and explains offline,
  then executes repository analytics and ordinary composed DuckDB SQL.
- Automated oracle: deterministic single- and multi-page GraphQL fixtures
  cover the exact canonical query, nullable data, errors with and without data,
  cursor failures, serialized-body and response budgets, cancellation,
  terminal failure, and retained REST behavior.
- Compatibility evidence: a privacy-safe live GitHub probe confirms the
  canonical operation against the current upstream service without becoming
  the correctness oracle.
- Quality gates: focused team targets, `make build`, `make test`, `make demo`,
  source and dependency identity checks, a fresh native product cell,
  agent-asset validation, and staged and unstaged whitespace checks.
- Independent review: affected-team interaction-exit audit plus fresh
  adversarial transport/policy/lifecycle and product/test-oracle perspectives.

### Contract and invariant impact

- Propagate RFC 0011 through `docs/ARCHITECTURE.md`,
  `docs/CONNECTOR_SPECIFICATIONS.md`, `docs/RUNTIME_CONTRACTS.md`, native
  metadata, compiled and planned protocol operations, cursor planning,
  transport, typed rows, adapter null handling, diagnostics, examples, tests,
  changelog, and release notes.
- Keep connector-package GraphQL syntax explicitly future design. The active
  product accepts exactly one repository-owned canonical document profile and
  no caller documents, variables, endpoints, introspection, or mutations.
- Planning stays deterministic and network-free. DuckDB owns every relational
  operator. Execution stays immutable, sequential, bounded, cancelable,
  credential-safe, strictly converting, and fail-only for GraphQL errors.
- Canonical identity, exact bytes, content digest, base-domain identity,
  replay derivation, request-body budgets, cursor transitions, and nullable
  values remain typed facts rather than strings consumers reinterpret.

### Team and RFC routing

- Accountable stream: Query Experience.
- Connector Experience — **Collaboration, then X-as-a-Service:** provide the
  canonical native relation, protocol operation, validation, snapshots, and
  provider fixtures. Exit when consumers use only the bounded compiled API.
- Relational Semantics — **X-as-a-Service with bounded collaboration:** provide
  the exhaustive protocol-operation plan, base domain, cursor plan, budgets,
  nullability, and immutable fixtures. Exit when consumers do no semantic or
  replay reclassification.
- Remote Runtime — **Collaboration, then X-as-a-Service:** admit and execute
  canonical query-only plans through the standard executor and stream. Exit
  when Query uses no Runtime internals and negative authority/budget cases fail
  before bearer placement or I/O.
- RFC: RFC 0011 is Accepted and authorizes the shared and public contract.

### Unknowns and first trial

None remain decision-critical. Delivery begins with a permanent deterministic
first-page vertical slice through the provider APIs; cursor traversal extends
that same production design rather than promoting experiment structure.

### Delivery path

1. Each selected charter produces its own workstream plan, source/test
   ownership, dependency map, and observable interaction exit.
2. Deliver the canonical Connector and Semantics provider interfaces and their
   independent fixtures.
3. Deliver Runtime first-page execution, nullable rows, safe errors, and
   cursor/resource lifecycle while Query integrates the public SQL surface
   against provider services.
4. Propagate contracts and product documentation, run independent review and
   complete gates, audit actual target/include/test dependencies, commit the
   coherent product outcome, and record the retain-or-narrow decision here.

## Responsibility and dependency map

| Workstream | Primary source ownership | Oracle ownership | Consumes | Provides | Interaction exit |
| --- | --- | --- | --- | --- | --- |
| [Connector Experience](graphql-repository-analytics/connector-experience/plan.md) | Canonical native GraphQL relation, operation declarations, validation, identity, and digest | Provider validation, negative authority profiles, immutable snapshots, consumer fixture | RFC 0011 and native catalog invariants | Bounded compiled GraphQL operation and column/nullability facts | Consumers use public const metadata and fixture APIs without constructing or parsing Connector internals |
| [Relational Semantics](graphql-repository-analytics/relational-semantics/plan.md) | Protocol-operation sum, exact base domain, replay derivation, cursor/resource plan, immutable explanation | Planner snapshots, invalid-contract counterexamples, provider fixture service | Connector facts and Query request | Complete immutable GraphQL `ScanPlan` and safe fixtures | Query and Runtime do not inspect document text or reclassify relational/replay meaning |
| [Remote Runtime](graphql-repository-analytics/remote-runtime/plan.md) | Canonical admission, JSON-body transport, envelope/error decode, cursor state, nulls, budgets, stream lifecycle | DuckDB-free authority, protocol, security, resource, cancellation, and terminal-state corpus | Complete plan, moved authorization capability, and protocol-neutral content digest service | Standard bounded `BatchStream` | Query consumes executor/stream only; Runtime imports no Connector metadata/private source, Query, or Semantics-private source, and the direct digest dependency grants no catalog or replay authority |
| [Query Experience](graphql-repository-analytics/query-experience/plan.md) | Public relation bind, nullable DuckDB vectors, safe explanation/diagnostics, product composition | Offline adapter, SQL, controlled end-to-end, prepare/repeat/join, and privacy-safe live oracles | Connector, Semantics, and Runtime provider APIs | User-visible repository analytics | Product narrative passes without cross-team internals or protocol-specific SQL |

Connector publishes the compiled operation before Semantics freezes the plan
sum and fixtures. Runtime and Query then implement in parallel against those
provider services. The lead agent owns integration, shared contract
propagation, root build/source identity/version/release records, final review,
Git history, and goal closure. No workstream edits another team's plan or
duplicates provider production source in a consumer target.

## Completion record

Completed on 2026-07-19. The product decision is to **retain GraphQL in the
intended v1 surface**: the permanent native `viewer_repository_metrics`
relation proves that the existing Connector, Semantics, Runtime, and Query
boundaries can carry a second protocol without a relation-specific executor or
protocol-specific SQL. Generated GraphQL and package authoring remain outside
this goal and do not become the next priority merely because the protocol was
retained.

The delivered relation has the RFC's exact eight-column schema, traverses the
canonical GitHub `viewer.repositories` cursor connection sequentially, maps a
missing primary language to SQL `NULL`, rejects any GraphQL error, and retains
DuckDB ownership of filtering, ordering, limits, joins, preparation, and
repeated execution. Connector publishes the closed operation and immutable
fixture, Semantics publishes the complete plan and base-domain facts, Runtime
uses the standard executor and `BatchStream`, and Query exposes the unchanged
`duckdb_api_scan` entry point.

Acceptance evidence includes focused provider and consumer targets, 159
SQLLogicTest assertions, actual-DuckDB duplicate/null/join/prepare/repeat and
relational-composition cases, the retained REST controlled-service suites,
`make build`, `make test`, `make demo`, source and dependency identity checks,
and a clean fresh native product cell at
`.build/verify-0.7-graphql-final-2`. A privacy-safe live GitHub compatibility
probe confirmed the canonical operation and fixed schema without emitting
repository values, row counts, documents, variables, cursors, remote errors,
or credentials. The deterministic fixtures remain the correctness oracle.

The final target/include/source audit marks every workstream interaction
**Satisfied; X-as-a-Service**. Consumers link bounded provider services rather
than compiling another team's production sources; Runtime imports no Connector
metadata, Query code, or Semantics planner internals; Query consumes the public
catalog, planner, executor, stream, authorization, typed-value, and structured
error APIs. Independent relational and security/lifecycle review found no
unresolved evidence-backed defect after the recorded corrections.

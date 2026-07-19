# Goal: Bounded authenticated repository traversal

Follow `docs/PRODUCT_DELIVERY.md`.

Status: **Active**. RFC 0007 is Accepted. Product delivery may establish the
approved public and shared contracts through the topology-owned workstreams
linked below.

## PM brief

### Outcome

For a DuckDB user, enable querying every repository exposed through one
bounded GitHub authenticated page sequence as a typed relation so that useful
multi-page API data requires no manual pagination.

### Why now

`0.4.0` proves one capability-scoped authenticated request. The next
fundamental product risk is whether the permanent extension can safely turn a
multi-page authenticated resource into one bounded DuckDB relation. This
delivers a useful repository dataset while exercising pagination, streaming,
cancellation, and resource boundaries before declarative distribution work.

### Product guardrails

- Must: expose fixed `github.authenticated_repositories` through the explicitly
  named temporary-secret path accepted by RFC 0006.
- Must: traverse accepted GitHub Link pages sequentially under per-page and
  per-scan request, page, byte, record, memory, time, and concurrency ceilings.
- Must: preserve DuckDB ownership of filtering, ordering, limit, and offset;
  report mutable-source limitations honestly; and fail rather than silently
  return a complete-looking partial relation.
- Must not: add retries, rate-limit sleeps, parallel pages, resume, caller URLs
  or headers, connector YAML/package loading, caching, providers, GraphQL, or a
  snapshot guarantee.
- Preserve: both `0.4.0` relations, offline bind/planning, immutable metadata
  and plans, exact host/header bearer authority, strict conversion,
  cancellation, idempotent close, and credential/private-row exclusion from
  repository evidence.

### Success signals

- A user queries `github.authenticated_repositories` with a named temporary
  secret and receives the fixed five-column repository row bag across more
  than one accepted page.
- Empty intermediate pages, cancellation, early close, malformed or
  authority-escaping Link values, exhausted budgets, and late-page failures
  have deterministic non-truncating behavior.
- Existing anonymous and authenticated-user queries retain their accepted
  behavior.

## Agent commitment

### Observable interpretation

The user creates the RFC 0006 temporary secret and executes:

```sql
SELECT id, full_name, private, fork, archived
FROM duckdb_api_scan(
    connector := 'github',
    relation := 'authenticated_repositories',
    secret := 'github_default'
)
ORDER BY id;
```

Execution resolves the named secret once, requests the fixed first repository
page, and consumes only strictly validated same-profile next-page transitions.
The runtime yields nonempty typed batches until true exhaustion. An empty
nonterminal page advances inside the same pull. A malformed page transition,
failed page, cancellation, close, deadline, or resource ceiling terminates the
scan without a successful-partial-result claim. The row domain is a
duplicate-preserving bag from a mutable source; local `ORDER BY id` makes the
acceptance result deterministic without implying remote ordering.

### Acceptance evidence

- Demonstration: a controlled three-page SQL query, including an empty middle
  page case, returns the exact five-column row bag; a privacy-safe live query
  records only schema, aggregate row count, fixed request envelope, and version
  identity. Actual page and request counts remain controlled-oracle evidence;
  they do not require a new public telemetry surface.
- Automated oracle: immutable catalog and plan snapshots; Link grammar and
  authority counterexamples; page request sequences; nonempty-success stream
  behavior; per-page and aggregate budget exhaustion; cancellation during and
  between pages; early close; late status/decode/schema failures; capability
  release and redaction; and regressions for both existing relations.
- Quality gates: focused responsibility targets, `make build`, `make test`,
  `make demo`, source/dependency identity gates, a fresh native product cell,
  agent-asset validation, and staged/unstaged diff checks.
- Independent review: final Query lifecycle/FFI, Connector metadata,
  Relational base-domain/limit, Runtime pagination/security/resource/lifecycle,
  test-oracle, and adversarial perspectives.

### Contract and invariant impact

- RFC 0007 is the accepted rationale. `docs/ARCHITECTURE.md`,
  `docs/CONNECTOR_SPECIFICATIONS.md`, `docs/RUNTIME_CONTRACTS.md`,
  `ROADMAP.md`, release notes, diagnostics, examples, fixtures, and tests must
  carry the delivered behavior without turning the RFC into the sole contract.
- Connector's immutable pagination declaration, Semantics' immutable
  `PaginationPlan`, Runtime's normalized Link metadata/state machine/budgets,
  and Query's fixed relation and `BatchStream` consumption change as private
  pre-`1.0` team APIs. None becomes a public native ABI or connector-package
  compatibility promise.
- Pagination is sequential. Every accepted page has one attempt and no replay.
  Link data can narrow only the typed next page; it grants no destination,
  path, query-field, header, or credential authority.
- A successful `BatchStream::Next` contains a nonempty schema-aligned batch;
  `false` means clean exhaustion. Work remains pull-controlled, bounded,
  cancelable, and closeable.

### Team and RFC routing

- Accountable stream: Query Experience.
- Connector Experience — **Collaboration, then X-as-a-Service:** provide the
  fixed native relation and explicit immutable pagination declaration. Exit
  when metadata compiles/explains deterministically and consumers neither
  construct its internals nor infer pagination from request fields.
- Relational Semantics — **Collaboration, then X-as-a-Service:** provide the
  offline pagination plan, duplicate-preserving bag meaning, mutable
  consistency, budgets, and no relational delegation. Exit when planner
  oracles pass and Runtime consumes the plan without reclassification.
- Remote Runtime — **Collaboration, then X-as-a-Service:** provide normalized
  Link capture, strict page transitions, aggregate accounting, repeated
  authorized requests, nonempty pull behavior, cancellation, close, and
  redacted failure. Exit when DuckDB-free evidence passes and Query contains no
  pagination or transport internals.
- RFC: RFC 0007 is Accepted with product approval and all four affected-team
  reviews recorded. Contract propagation and acceptance evidence remain goal
  completion requirements.

### Unknowns and first trial

- Unknown: none identified. Primary GitHub documentation and the existing curl
  header callback and pull stream establish feasibility.
- Trial: no disposable implementation. The first executable increment is the
  permanent provider interfaces and deterministic state-machine oracle, kept
  unexposed until the complete thin public path is executable.

### Delivery path

1. Establish and test the immutable Connector and Semantics provider contracts
   without exposing an unexecutable public relation.
2. Implement and independently exercise Runtime's DuckDB-free Link parser,
   state machine, aggregate budgets, repeated authorization, and lifecycle.
3. Activate the fixed relation through Query, prove the controlled and live
   SQL narratives, propagate contracts and `0.5.0` identity, review the final
   dependency graph, and pass cached and fresh gates.

### Recorded delivery evidence

On 2026-07-18, the controlled DuckDB product oracle passed 59 exact requests:
seven relational, 45 failure/recovery, and seven lifecycle interactions. The
privacy-safe live compatibility check loaded the `0.5.0` artifact and recorded
the fixed five-column schema, an aggregate count of 432 repositories, and the
accepted one-at-a-time, 32-page, 30-second, zero-retry request envelope. It
recorded no credential, repository identity, row value, or Link value.

## Responsibility and dependency map

| Workstream | Primary source ownership | Oracle ownership | Consumes | Provides | Interaction exit |
| --- | --- | --- | --- | --- | --- |
| [Connector Experience](bounded-authenticated-repository-traversal/connector-experience/plan.md) | Native relation metadata, explicit pagination declaration, catalog validation and explanation | Catalog contract, fixture, and snapshot tests | Accepted RFC and native metadata conventions | Immutable `CompiledConnector` facts | Consumers use const public accessors and no request-field inference |
| [Relational Semantics](bounded-authenticated-repository-traversal/relational-semantics/plan.md) | `CompiledConnector + ScanRequest -> ScanPlan`, pagination meaning and resource intersection | Planner snapshots, counterexamples, no-I/O and ownership tests | Connector provider API and Query request | Immutable `PaginationPlan` and complete `ScanPlan` | Runtime executes without semantic reclassification |
| [Remote Runtime](bounded-authenticated-repository-traversal/remote-runtime/plan.md) | Link metadata/parser, page state, repeated transport, aggregate accounting, stream lifecycle | DuckDB-free protocol, security, budget, cancellation, close, and failure tests | Complete plan and authorization capability | Bounded `BatchStream` and structured errors | Query consumes service without Runtime internals |
| [Query Experience](bounded-authenticated-repository-traversal/query-experience/plan.md) | Fixed SQL relation activation, DuckDB lifecycle/error translation, controlled/live product evidence | Adapter, SQL, prepared-secret, black-box product, regression, and privacy-safe live tests | Connector, Semantics, and Runtime provider APIs | User-visible `0.5.0` outcome | Accepted narrative passes without cross-team internals |

Provider interfaces sequence Connector before Semantics before Runtime. After
those interface shapes are accepted, Runtime's parser/state-machine work and
Query's provider-fake lifecycle/product-oracle work are file-disjoint and may
proceed in parallel. Public relation activation waits for the real Runtime
consumer path so `main` never advertises an unexecutable relation.

The lead agent owns integration-only overlaps: root build registration, source
and version identity, authoritative contract propagation, public changelog,
final dependency/interaction audit, live-secret handling, Git history, and
goal closure. A team does not edit another plan or duplicate another provider
inside its consumer to avoid a dependency.

# Goal: Bounded reactive rate-limit handling

## PM brief

### Outcome

For DuckDB users querying rate-limited HTTP and GraphQL APIs, enable explicit,
bounded, cancellable cooperation with remote quota signals so that a declared
replayable query can recover without turning `429` or another author-selected
status into unbounded retry behavior or blocking unrelated quota identities.

### Why now

The product now has the prerequisite failure/accounting model, opaque
credential authority, immutable scan snapshots, and one bounded replay-safe
attempt loop. Rate-limit recovery can therefore use those authorities instead
of inventing mutable credentials, mechanism-private attempt pools, or another
replay interpretation.

### Product guardrails

- Must: support declared fail, bounded wait, and deadline-aware bounded wait
  modes with precise redacted terminal diagnostics.
- Must: isolate quota coordination by exact authority and operation identity,
  use cancellable finite waits, and preserve one scan deadline and resource
  ledger.
- Must not: proactively pace successful responses, infer an unstable standard
  grammar, coordinate across processes, enable parallel pagination, cache
  results, or add circuit breaking.
- Preserve: deterministic offline bind/planning, replay only before step
  acceptance/exposure, exact credential/destination authority, sequential
  pagination, strict conversion, immutable plans/snapshots, and backpressure.

### Success signals

- A v3 connector author can declare a closed rate-limit policy for a proved
  read, and v1/v2 packages retain prior behavior.
- A DuckDB query either recovers within its declared attempt, wait, queue,
  resource, cancellation, and deadline bounds or reports the exact closed
  redacted reason.
- Same-key recovery attempts are fair and serialized while independent
  principals, operation families, connectors, database instances, and
  processes progress independently.

## Agent commitment

### Observable interpretation

Add an exact `duckdb_api/v3` package grammar that compiles immutable status,
mode, guidance, scope, operation-family, optional remaining/bucket, and maxima
facts. Semantics copies them into an immutable plan and derives combined
attempt/wait authority. Runtime observes only targeted complete-response
fields, converts guidance to steady time, and uses one executor-owned bounded
FIFO coordinator from the sole resilience loop. Query explains safe planned
facts and renders content-free terminal observations.

### Acceptance evidence

- Demonstration: controlled REST and GraphQL queries recover from a declared
  quota response and return the same duplicate-sensitive relational result as
  their failure-free reference; terminal variants render precise redacted
  reasons.
- Automated oracle: exact v3 schema/compiler tests; immutable-plan and checked
  algebra tests; strict parser/clock tests; coordinator fairness, isolation,
  cancellation, saturation, and close tests; protocol execution and actual
  DuckDB equivalence tests.
- Quality gates: all documentation, agent-asset, contract-freeze, source-
  identity, native dependency, fresh native product, demo, and Community gates
  required by `AGENTS.md`.
- Independent review: Query Experience, Remote Runtime, Relational Semantics,
  and Connector Experience RFC review; fresh adversarial correctness,
  credentials, concurrency, cancellation, and lifecycle review before commit.

### Contract and invariant impact

- Affects `docs/ARCHITECTURE.md`, `docs/CONNECTOR_SPECIFICATIONS.md`,
  `docs/RUNTIME_CONTRACTS.md`, the candidate 1.0.0 freeze, connector schema and
  compiled metadata, `ScanPlan`, `BatchStream` diagnostics, transport metadata,
  resource accounting, and executor lifecycle.
- The per-step attempt ceiling is the maximum of ordinary retry and rate-limit
  maxima, never their sum. Aggregate waiting is a checked sum within one hard
  ceiling. A repeat still requires declared replay safety and an unaccepted,
  unexposed step.
- Credentials, opaque identities, response values, remote bucket text, URLs,
  and timestamps never enter plans or diagnostics.

### Team and RFC routing

- Accountable stream: Query Experience.
- Supporting interactions: Remote Runtime as a service for the reusable
  resilience/coordinator boundary; Connector Experience in collaboration for
  v3 grammar and compiled facts; Relational Semantics in collaboration for
  checked plan algebra. Each exits only when final source and focused consumer
  targets depend on the bounded team API rather than provider internals.
- RFC: [RFC 0025](../rfcs/0025-enable-bounded-reactive-rate-limit-handling.md),
  Accepted.

### Unknowns and first trial

- Unknown: the active IETF RateLimit field work may not yet be a stable
  compatibility surface.
- Trial: verify the current primary standard sources, then exercise one
  connector-declared generic policy through a complete controlled REST
  response, strict clock conversion, one scheduler key, and the existing
  replay loop. The primary-source check confirmed the generalized field work
  remains an Internet-Draft, so v3 intentionally exposes only closed generic
  declarations and makes no proactive or standards-profile claim.

### Delivery path

1. Accept the cross-team contract and exact v3 compatibility boundary.
2. Implement compiled facts, immutable planning algebra, the targeted Runtime
   observation/coordinator service, unified resilience execution, diagnostics,
   and focused evidence.
3. Audit topology exits, apply independent adversarial findings, run every
   relevant repository gate, and commit one coherent release increment.

## Completion record

### Delivered behavior

- `duckdb_api/v3` packages declare a closed reactive rate-limit policy while
  v1 and v2 packages preserve their frozen behavior. Connector validation and
  compilation cover statuses, mode, targeted guidance, quota identity inputs,
  and bounded maxima without teaching Runtime to parse package syntax.
- Semantics copies the compiled facts into an immutable `ScanPlan`, derives the
  maximum rather than the sum of attempt authorities, and admits the checked
  combined waiting ceiling without creating another replay interpretation.
- Runtime retains only declared complete-response metadata, parses guidance
  strictly against receipt-time clocks, freezes the remote bucket identity,
  and serializes same-key recovery through an executor-local bounded FIFO
  coordinator. Every wait is cancellable and charged to the one scan ledger;
  partial-response cancellation also commits observed attempt bytes exactly
  once before cancellation crosses the public boundary.
- Query exposes safe planned facts and content-free execution observations.
  Controlled REST and GraphQL recoveries preserve duplicate-bearing relational
  results, while terminal variants report the accepted closed reasons.

### Acceptance evidence

- Exact schema/compiler, migration, immutable-plan, algebra, guidance, clock,
  quota-key, fairness, deadline-boundary, saturation, cancellation, shutdown,
  byte-accounting, REST, GraphQL, curl, and actual-DuckDB oracles pass.
- `make build`, `make test`, and `make demo` pass on the pinned developer cell.
  Agent assets, public-surface inventory, contract freeze, RFC evidence,
  source-identity mutation oracles, native dependency records, Community
  installation, Community enablement, native formatting, and both whitespace
  checks pass.
- The fresh native product gate intentionally archives `HEAD`; goal closure is
  conditioned on running it against the final commit, and that exact
  committed-tree evidence is reported in the delivery handoff.
- Independent adversarial reviews covered relational correctness, credential
  isolation, targeted metadata, scheduling, cancellation, wait/byte
  accounting, and lifecycle behavior. The supported findings were fixed; the
  final security/lifecycle and accounting re-reviews returned no findings.

### Topology interaction exits

| Interaction | Final dependency evidence | Exit |
| --- | --- | --- |
| Query Experience accountable delivery | Query consumes immutable `ScanPlan` facts and the documented `BatchStream` execution/diagnostic boundary; actual DuckDB explanation and duplicate-equivalence targets cover the public behavior | Satisfied |
| Remote Runtime service | `duckdb_api_runtime_resilience_service` owns clocks, guidance, and coordination; focused provider tests link that bounded service, while executor consumers use its public Runtime-private API rather than scheduler internals | Satisfied |
| Connector Experience collaboration | v3 YAML compiles through the versioned schema and compiled declaration API; Runtime consumes planned facts and has no package-syntax dependency | Satisfied |
| Relational Semantics collaboration | Planner targets own checked attempt/wait algebra and immutable plan construction; Runtime consumes `ScanPlan` rather than reconstructing SQL or connector declarations | Satisfied |

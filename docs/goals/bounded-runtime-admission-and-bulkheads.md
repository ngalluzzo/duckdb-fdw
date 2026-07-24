# Goal: Bounded Runtime admission and bulkhead isolation

## PM brief

### Outcome

For operators running concurrent remote scans, prevent one slow, failing, or
throttled connector, destination, or credential principal from exhausting
shared workers, sockets, memory, queues, or waiting capacity.

### Why now

Retries and rate-limit waits can amplify load and retain Runtime state.
Production resilience is incomplete until shared capacity is bounded,
isolated, cancellable, and recoverable under sustained remote failure. The
existing scan ledger, opaque credential authority, replay-safe retry loop, and
reactive rate-limit coordinator now provide the identities and accounting this
work must compose with.

### Product guardrails

- Must: bound active scans, in-flight requests, credential resolution,
  ordinary retry and rate-limit waiting, buffered bytes, and decoded rows at
  global and isolation dimensions.
- Must: derive finite bulkheads only from existing connector, operation,
  destination, and opaque authority facts; no arbitrary author pool names.
- Must: keep every queue finite, fair under its declared law, cancellable,
  deadline-aware, and drained by orderly executor close.
- Must: preserve backpressure, immutable plans and credential snapshots, one
  scan deadline/ledger, sequential pagination, and exact replay authority.
- Must not: classify local overload as remote timeout, silently retry locally
  rejected work, add distributed coordination, or introduce Connector or
  Relational Semantics interpretation.
- Must not: add circuit breaking in this goal. The product manager approved it
  as a follow-on only after admission control is demonstrated and complete.

### Success signals

- A distinct healthy connector continues within declared latency/resource
  bounds while another destination or principal is continuously slow,
  retrying, or rate-limited.
- Queue or capacity saturation produces one stable, redacted local resource
  diagnostic and performs no transport for rejected work.
- Canceled queued work is removed promptly; clean exhaustion, failure, close,
  and destruction release every permit and reservation exactly once.
- Shared counts and bytes remain at or below their installed limits during
  saturation and return to zero after drain.

## Agent commitment

### Observable interpretation

Add one executor/DatabaseInstance-local admission service that atomically
acquires complete global, connector, destination, provider-principal, and
exact-bulkhead vectors. It governs bounded provider resolution, active scans,
transport requests, recovery waiters, response/decode capacity, and row
buffers. Query remains a consumer of `ScanExecutor`, `BatchStream`, and closed
safe diagnostic facts; ordinary bind, `DESCRIBE`, `EXPLAIN`, and `PREPARE`
perform no admission, credential, clock, or network work.

### Acceptance evidence

- Demonstration: actual-DuckDB REST and GraphQL cases each saturate one exact
  bulkhead, reject same-key work before transport, and complete a distinct
  healthy connector. A named v3 REST pressure case keeps one slow scan live
  beside a second scan that traverses ordinary retry and a real rate-limit wait
  while the healthy connector completes; show exact safe diagnostics, bounded
  public stream/`Next` lifecycle counts, and clean shutdown.
- Automated oracle: every global and per-dimension boundary/one-over and
  simultaneous failure; same-key FIFO and eligible-key bypass; repeated-arrival
  starvation; provider/scan/request queue saturation, timeout, and cancellation
  races; checked ticket exhaustion; retry/rate waiter caps; attempt-not-charged
  law; response release before recovery wait; byte/row allocation and
  multi-batch page/handoff ownership; credential authority replacement; REST,
  GraphQL, single-page, and pagination paths; executor close/destruction and
  retained exhausted streams.
- Failure-path evidence: retry storm, rate-limit growth, worker/socket
  saturation, curl retained-metadata and chunked/decompressed co-live buffers,
  GraphQL body and cursor-transfer capacity,
  provider failure, cancellation before/during/after grant, close races, and
  actual quiescent DatabaseInstance teardown.
- Quality gates: every documentation, agent, public-surface, contract-freeze,
  RFC-evidence, source-identity, native-dependency, fresh native product,
  developer, demo, Community installation, and Community enablement gate in
  `AGENTS.md`.
- Independent review: Query Experience and Remote Runtime approved the RFC;
  fresh adversarial concurrency/lifecycle and transport/policy reviewers must
  approve the final implementation or have findings explicitly dispositioned.

### Contract and invariant impact

- Affects `docs/ARCHITECTURE.md`, `docs/RUNTIME_CONTRACTS.md`, public failure
  properties/rendering, Runtime execution/resource/lifecycle code, Query's
  executor-close composition, release/freeze inventory, roadmap, examples,
  diagnostics, and tests.
- Does not affect Connector package syntax, validation, compiled package bytes,
  `ScanRequest`, immutable `ScanPlan` meaning, relational pushdown, SQL
  settings, or explanation policy.
- A local rejection occurs before attempt debit and never enters replay or
  remote-failure classification. Every acquisition checks all applicable
  dimensions atomically; no nested partial permits exist.
- Response storage is released before retry/rate waiting. A decoded-page
  reservation remains live across every batch from that page, with a distinct
  handoff charge, until full drain or terminal cleanup.

### Team and RFC routing

- Accountable stream: Query Experience.
- Supporting interaction: Remote Runtime in Collaboration, then
  X-as-a-Service. Exit requires final source/test dependencies to expose one
  documented admission service while Query imports no scheduler identity,
  counters, mutexes, or Runtime-private test construction.
- RFC: [RFC 0026](../rfcs/0026-bound-runtime-admission-and-bulkheads.md),
  Accepted 2026-07-23 after independent Query and Remote Runtime approval.
- Connector Experience and Relational Semantics are not affected because this
  goal consumes already-admitted bounded facts without changing syntax,
  compilation, planning, or SQL behavior.

### Unknowns and first trial

- Resolved: synchronous adapter callbacks do not monopolize DuckDB's configured
  shared execution workers. The pinned DuckDB 1.5.4 trial used `threads=1`,
  held sixteen real adapter scans in `Next`, completed a seventeenth healthy
  relation within two seconds, and released all streams exactly once.
- Resolved: the standalone admission provider composes with request/recovery
  ordering and pre-allocation byte/row reservations. Controlled REST and
  GraphQL actual-DuckDB saturation rejects same-bulkhead work before transport,
  completes an unrelated connector through the same executor, and observes
  exact quiescent DatabaseInstance close. A separate named v3 pressure case
  composes a slow scan with sequential ordinary-retry and rate-limit recovery;
  the provider oracle independently proves both waiter classes can coexist in
  one exact bulkhead.

### Delivery path

1. Add the closed Runtime admission identity/profile/controller service and
   exhaustive deterministic provider oracles.
2. Integrate provider resolution, stream/request/recovery admission, precise
   buffer/page ownership, attempt accounting, diagnostics, and explicit close.
3. Compose Query lifecycle and rendering through only public Runtime
   interfaces; add controlled REST/GraphQL and actual DuckDB saturation proof.
4. Propagate architecture/runtime/freeze/roadmap/release contracts, perform
   independent adversarial review and topology-exit audit, run every relevant
   gate, and commit the coherent delivery increment.

## Governance

Follow `docs/PRODUCT_DELIVERY.md`. Pursue the bounded-admission outcome above
under Accepted RFC 0026. Query Experience is accountable and Remote Runtime is
the collaborating platform provider. Completion requires the listed
demonstration, deterministic oracles, contract propagation, independent review,
topology exit evidence, repository gates, and a Conventional Commit. Circuit
breaking remains the separately approved follow-on and is not activated by
this goal.

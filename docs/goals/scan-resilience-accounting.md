# Goal: Scan resilience accounting model

## PM brief

### Outcome

For DuckDB users and operators, make every remote scan's execution, failure,
replay, waiting, and resource behavior explicit and diagnosable so later
resilience mechanisms cannot silently change query meaning or reset safety
limits.

### Why now

`ROADMAP.md` intentionally excludes automatic retry, rate-limit waiting,
configurable caching, and related stateful behavior from `1.0.0`. Those
mechanisms need one shared vocabulary and accounting model before any of them
can be added safely.

### Observable scope

- Stable identities for a logical scan, remote operation, traversal step or
  page, and transport attempt.
- One primary failure taxonomy covering configuration, authorization,
  credential-provider, destination-policy, transport, timeout, remote-status,
  rate-limit, protocol, decode, schema, resource-budget, cancellation, and
  internal failures.
- Structured failure properties: phase, replay classification, attempt number,
  rows exposed, remote status class, and terminating budget.
- Replay decisions distinguishing never replayable, replayable before exposure,
  atomic traversal-step replay, server-directed delay, and indeterminate.
- Aggregate scan budgets for attempts, elapsed time, cumulative waiting,
  responses, bytes, rows, memory, and other existing runtime limits.
- The effective resilience and budget policy exposed through deterministic
  explanation or structured diagnostics.

### Product guardrails

- **Must:** Preserve redaction, offline bind and planning, cancellation,
  immutable plans, bounded execution, and current relational semantics.
- **Must:** Treat indeterminate replay safety as non-replayable.
- **Must:** Ensure attempts and waits consume aggregate scan budgets rather than
  resetting deadlines or counters.
- **Must not:** Add automatic retry, rate-limit waiting, caching, circuit
  breaking, or credential-provider expansion in this goal.
- **Preserve:** Existing rendered diagnostic strings unless RFC 0021 explicitly
  extends the public diagnostic surface (it does, additively).

### Success signals

- Every existing remote failure maps to exactly one primary failure class with
  stable structured attributes.
- Cancellation, deadline expiry, remote timeout, and local resource exhaustion
  remain distinguishable.
- A user or operator can determine whether rows were exposed, how many attempts
  occurred, and which budget terminated execution.
- Downstream resilience goals can consume the contract without inventing
  mechanism-specific identities or counters.

### Reserved product decisions

- **Resolved (2026-07-22):** the additive public-diagnostic-surface extension
  (new structured primary-class + properties field; existing rendered strings
  preserved verbatim) is approved by the product manager as part of accepting
  RFC 0021.

## Agent commitment

### Observable interpretation

A DuckDB user or operator runs a representative remote scan and, on success,
failure, cancellation, or budget exhaustion, inspects one stable structured
diagnostic and/or `EXPLAIN` output. From that output alone they can identify the
logical scan and the traversal step / transport attempt that failed, the single
primary failure class, whether any rows were already exposed, how many attempts
occurred, which aggregate budget terminated execution, and whether the operation
is replayable. The observable behavior of an existing successful or failing scan
is unchanged at the rendered-string level; this goal adds a unified accounting
and vocabulary layer beneath those observable results, and no resilience
mechanism.

### Acceptance evidence

- Demonstration: representative successful, failed, canceled, and
  budget-exhausted scans; stable structured diagnostics and `EXPLAIN` show
  identity, class, replay, attempts, rows-exposed, and terminating budget.
- Automated oracle: exhaustive failure-taxonomy mapping (every existing remote
  failure path → exactly one class); four-way termination-distinguishability
  (cancellation/deadline/timeout/exhaustion); aggregate-budget debit/no-reset
  oracle; per-field redaction fixtures; a property test proving Runtime consumes
  the plan's replay obligation without reclassifying relational meaning.
- Failure-path evidence: failures before connection, before headers, during body
  receipt, during decode, after traversal-step acceptance, during cancellation,
  and at each resource boundary.
- Quality gates: `AGENTS.md` "Current verification" gates; the closed-vocabulary
  freeze-section gate extension with removal/rename mutation tests; source
  identities and native-dependency gates if product source changes; `make build`,
  `make test`, `make demo`.
- Independent review: `$adversarial-review` of replay, resource, diagnostic, and
  lifecycle/cancellation semantics.

### Contract and invariant impact

- `docs/ARCHITECTURE.md` (Diagnostics and explanation; Bounded streaming
  lifecycle) and `docs/RUNTIME_CONTRACTS.md` (Error ownership and redaction;
  Resource accounting; Cancellation, close, and failure; pagination/lifecycle).
- The closed-vocabulary freeze section enumerating the 14 primary classes and 5
  replay classifications.
- Invariants carried: redaction; offline deterministic bind/planning; immutable
  plans; bounded/cancelable work; conservative fallback; indeterminate replay =
  non-replayable; a retry requires declared replay safety + an uncommitted replay
  unit; budget arithmetic checked, never reset.

### Team and RFC routing

- Accountable stream: Query Experience.
- Supporting interactions: Remote Runtime (Collaboration — model owner/
  implementer), Relational Semantics (Collaboration — plan replay/exposure
  obligations), Connector Experience (Collaboration — replay fact; exit
  Satisfied per RFC review), Engineering Enablement (Facilitation — oracles +
  closed-vocabulary gate, transferred to owners).
- RFC: required; **RFC 0021 Accepted 2026-07-22** after five-team topology review
  (one objection accepted and resolved in-text) and product approval.

### Unknowns and first trial

- None blocking: the spike established exhaustiveness, the replay-fact
  sufficiency, the aggregate-ledger basis, and the `timeout` reservation. The
  exact C++ enum spellings and the freeze-section shape are delivery decisions.

### Delivery path

1. Closed vocabulary (C++ enums) + freeze-section gate extension with mutation
   tests — the stable, mechanically-protected foundation.
2. Identity ordinals + structured failure-property types.
3. Aggregate budget: add `cumulative_waiting` + codify the no-reset invariant.
4. Failure emission: classify into primary classes; fix the mis-stagings (429/503,
   malformed Link, GraphQL page-budget, transfer-encoding, AUTHENTICATION split);
   structurally disambiguate `GraphqlCursorError`.
5. Replay classification: combine plan obligation + exposure state into the
   per-step/per-failure fact.
6. Query rendering: surface the new structured field at the DuckDB boundary
   (additive; preserve strings); re-introduce the Semantics code.
7. `EXPLAIN` effective-policy facts.
8. Oracles (mapping/distinguishability/debit-reset/redaction/no-reclassification).
9. Contract propagation (ARCHITECTURE/RUNTIME_CONTRACTS/ROADMAP) + adversarial
   review + commit.

## Governance

Follow docs/PRODUCT_DELIVERY.md. Pursue: one authoritative, stable model for a
remote scan's execution identity, failure classification, replay decision,
waiting accounting, and aggregate resource budget — with no resilience mechanism
enabled.
Completion requires: the acceptance evidence above, RFC 0021's Acceptance and
verification section, the closed-vocabulary freeze gate, and independent
adversarial review.
Preserve: redaction; offline deterministic bind/planning; immutable plans;
bounded/cancelable execution; conservative fallback; indeterminate replay as
non-replayable; the retry invariant; budget arithmetic never reset; AGENTS.md
and the architecture/runtime contracts.
Governance: Accountable stream is Query Experience. RFC 0021 accepted
2026-07-22 (`docs/rfcs/0021-establish-scan-resilience-accounting.md`) — five-team
topology-consult review complete; one Engineering Enablement objection accepted
and resolved by correcting gate mechanics and scoping the closed-vocabulary
freeze section; product approval recorded (Nic Galluzzo, 2026-07-22).

## Completion record

### Delivered

One authoritative, stable scan resilience accounting model (RFC 0021),
additive and with no resilience mechanism enabled: closed `FailureClass` (14)
and `ReplayClassification` (5) vocabularies bound to
`release/1.0.0/freeze.json` and enforced by `scripts/contract_freeze.py`;
`FailureProperties` (identity ordinals, phase, attempt, rows exposed, remote
status class, terminating budget); the `cumulative_waiting` aggregate-budget
dimension and `CommitWait` with the no-reset invariant; failure classification
wired at every emission path (HTTP status, GraphQL cursor, Link pagination)
with unified catch-boundary enrichment; `ClassifyReplay` (the retry-invariant
combine); `EXPLAIN` effective-policy (declared replay safety; retry/rate-limit
waiting/cache disabled); and the structured class surfaced at the DuckDB
boundary as an additive suffix preserving existing rendered strings.

### Evidence

- `make build`, `make test` green; format, source-identity, contract-freeze,
  public-surface-inventory, and agent-asset gates green across the change set.
- Oracles: exhaustive HTTP-status classification; `GraphqlCursorError`/
  `LinkPaginationError` kind mapping; `ClassifyReplay` truth table;
  `BudgetDimensionFromField`; `CommitWait` no-reset (zero-ceiling fails closed,
  additive debit, deadline never reset); four-way termination distinguishability
  (cancellation / deadline-TIME / reserved timeout / exhaustion); per-field
  structured redaction; REST + GraphQL EXPLAIN effective-policy; the controlled
  live-REST lifecycle oracle asserting the rendered class suffix.
- `$adversarial-review` (two perspectives) found P1 carried-fact defects
  (terminating_budget always none; cursor/link dropped step/rows_exposed;
  `ClassifyReplay` unwired); all fixed; a fresh fix-delta re-review confirmed
  the repairs introduce no new defects.
- Contract propagation: `docs/ARCHITECTURE.md` and `docs/RUNTIME_CONTRACTS.md`
  record the model.

### Material decisions and deviations

- **Additive diagnostic suffix.** The structured class is surfaced at the DuckDB
  boundary by appending a concise redacted suffix (`[class=<c> rows_exposed=<n>
  budget=<b>]`) after the preserved `[duckdb_api][<stage>]` prefix and message;
  existing rendered strings stay verbatim, INTERNAL stays opaque. An earlier
  verbose suffix was narrowed to the success-signal facts.
- **Replay is step-local.** `replay_classification` reflects whether the failed
  traversal step's rows were exposed, while `rows_exposed` is the cumulative
  count — distinct scopes by RFC design, not a contradiction.
- **Known limitation (P3, accepted).** `request_body_bytes` terminations classify
  as `resource_budget` with `budget=none` (the closed `BudgetDimension` set has
  no body-bytes member); adding it is a future RFC.
- **ROADMAP placement deferred.** Recording the model under a release in
  `ROADMAP.md` is a product-manager decision (the model is a cross-release
  foundation, not inherently one `0.Y.0` outcome); it remains the one open PM
  item before the goal fully closes.

### Product options discovered

- Automatic retry, rate-limit waiting, and author-configurable caching are
  follow-on goals that consume this contract (each requires its own RFC and
  debits the aggregate budget rather than resetting it).
- A distinct transport idle/connect timeout would graduate the reserved
  `timeout` class from unproduced to emitted (its own RFC).
- Threading the plan's declared replay safety through to the failure-path
  enrichment (vs the v1 `SAFE` invariant) becomes meaningful only when a
  non-replayable operation is introduced.

# RFC 0021: Establish the scan resilience accounting model

```yaml
rfc: "0021"
title: "Establish the scan resilience accounting model"
status: "Accepted"
rfc_type: "Product"
sponsor_team: "Query Experience"
technical_decision_owner: "Lead agent"
product_approver: "Nic Galluzzo"
authors:
  - "Lead agent"
required_reviewers:
  - "Query Experience"
  - "Remote Runtime"
  - "Relational Semantics"
  - "Connector Experience"
  - "Engineering Enablement"
affected_teams:
  - "Query Experience"
  - "Remote Runtime"
  - "Relational Semantics"
  - "Connector Experience"
  - "Engineering Enablement"
linked_outcome_or_objective: "Production-resilience foundation goal (brief under docs/PRODUCT_DELIVERY.md, pre-activation): make every remote scan's execution, failure, replay, waiting, and resource behavior explicit and diagnosable so later resilience mechanisms cannot silently change query meaning or reset safety limits."
supersedes: "none"
```

## Summary

Establish one authoritative, stable model spanning a remote scan's execution
identity, failure classification, replay decision, waiting accounting, and
aggregate resource budget — without enabling any resilience mechanism. Every
existing remote failure maps to exactly one primary failure class with stable
structured properties; every traversal step carries an explicit replay
classification (indeterminate treated as non-replayable); and attempts and
(future) waiting debit one aggregate scan budget rather than resetting
deadlines or counters. The taxonomy is new structured metadata layered on the existing coarse
error-stage classification: existing rendered diagnostic strings are preserved
verbatim, while a new primary-class field corrects several currently-conflated
or mis-staged failures. `EXPLAIN` gains the effective resilience and budget
policy, and no retry, rate-limit waiting, caching, circuit breaking, or
credential-provider behavior is introduced.

## Sponsorship and context

- **RFC type:** Product. The decision changes the public diagnostic contract
  (additively), the explainable policy surface, and shared team execution
  interfaces (`ScanPlan` replay/exposure obligations, `BatchStream` structured
  failure facts).
- **Sponsoring team:** Query Experience. The acceptance narrative ends with a
  DuckDB user or operator inspecting or diagnosing a remote scan's execution,
  failure, and resource behavior (`docs/teams/QUERY_EXPERIENCE.md`
  accountability clause).
- **Linked outcome or objective:** The production-resilience foundation goal.
  `ROADMAP.md` deliberately excludes "automatic retry or rate-limit waiting,
  author-configurable cache or single-flight behavior" from the `1.0.0`
  guarantee; those mechanisms need one shared vocabulary and accounting model
  before any of them can be added safely.
- **Why now:** Today's contracts carry the prerequisites for resilience only in
  scattered, coarse forms. `RUNTIME_CONTRACTS.md` "Error ownership and
  redaction" classifies failures by *owning team*; "Resource accounting" owns a
  scan ledger of checked counters; and the pagination and lifecycle sections
  state "one request/page is the replay unit, but v1 performs one attempt." None
  of these is a unified, stable, diagnosable model spanning identity, failure,
  replay, waiting, and aggregate budgets. Adding the first resilience mechanism
  without this model would force that mechanism to invent mechanism-specific
  identities and counters — exactly the outcome the linked goal forbids.

The DuckDB user sees the same successful rows and the same redacted failures for
any scan that succeeds or fails today. This decision adds a unified accounting
and vocabulary layer beneath those observable results.

## Problem

A remote scan today can fail or terminate in several distinguishable
circumstances — cancellation, scan-deadline expiry, a remote transport timeout,
local memory/byte/record exhaustion, an HTTP error, a decode failure, a
pagination-policy violation — but the contracts express only:

- a coarse, owner-boundary error set (`RUNTIME_CONTRACTS.md` "Error ownership
  and redaction": Runtime owns "authentication, policy, transport, HTTP, decode,
  GraphQL, pagination, resource, cancellation"; Query maps these once into
  bounded DuckDB diagnostics);
- a scan ledger of checked counters for requests/pages, bytes, records, strings,
  document bytes, memory, and elapsed time (`RUNTIME_CONTRACTS.md` "Resource
  accounting"); and
- a single sentence on replay: "One request/page is the replay unit, but v1
  performs one attempt," with `ScanPlan` carrying opaque
  "replay/cache/provider feature states" that are disabled.

Three concrete gaps follow:

1. **Indistinguishable terminations.** A user or operator cannot tell from a
   diagnostic whether a scan ended because the operator canceled, the local
   deadline expired, the remote endpoint timed out, or a local byte/memory
   budget was exhausted. The success signal requires these to remain
   distinguishable, but the current coarse classification cannot guarantee it.
2. **No exposure-aware replay fact.** The existing replay sentence names the
   replay unit but not its *commitment boundary*. `AGENTS.md` states "a retry
   requires both declared replay safety and an uncommitted replay unit," yet
   neither half is an explicit, stable fact on a traversal step or failure. A
   future retry mechanism would have to reconstruct replay safety from context.
3. **Per-mechanism budget risk.** Counters exist for resources and time but not
   for *attempts* or *waiting*. If a future retry or rate-limit-waiting
   mechanism owned its own counters, it could reset a deadline or counter
   mid-scan, violating "attempts and waits consume aggregate scan budgets rather
   than resetting deadlines or counters."

These are not defects in shipped behavior — v1 performs one attempt and no
mechanism exists. They are gaps in the *shared contract* that the next
resilience goal would otherwise fill ad hoc.

## Decision drivers and invariants

- **Must preserve:** redaction (diagnostics never contain absolute roots, YAML
  scalar content, secret names or values, generated documents, request/response
  bodies, rows, cursors, or remote messages — `ARCHITECTURE.md` "Diagnostics and
  explanation"); offline, deterministic bind and planning; immutable plans
  during execution; bounded, cancelable work; checked unsigned arithmetic with
  zero never meaning unlimited; conservative fallback; and current relational
  semantics (`D` implies `R` for safe pushdown; every residual has one owner;
  limit/offset only after required filtering and ordering).
- **Must preserve:** "a retry requires both declared replay safety and an
  uncommitted replay unit" — this decision makes both halves explicit, it does
  not weaken either.
- **Must preserve:** cancellation remains DuckDB interruption, not a project
  error string; terminal failure after prior rows repeats the same safe
  classification on later pulls and is never converted to clean exhaustion.
- **Must enable:** stable identities for scan, operation, traversal step, and
  transport attempt; one primary failure taxonomy with structured properties;
  explicit replay classification; one aggregate scan budget debited by attempts
  and waiting; and an explainable effective resilience and budget policy.
- **Must not introduce:** automatic retry, rate-limit waiting, caching, circuit
  breaking, single-flight execution, new credential providers, live progress
  reporting, external metrics/tracing exporters, or any path that resets an
  aggregate budget, deadline, or counter. Indeterminate replay safety must be
  treated as non-replayable.

## Proposed decision

The decision defines five coupled facts that every remote scan carries, owned at
their existing team boundaries. No fact is content; every fact is an ordinal,
structural identifier, a closed code, or a checked count already permitted by the
redaction contract's "stable codes, phases, structural identifiers, [and]
bounded field names."

### 1. Execution identity

A stable, scan-local identity hierarchy, generated by Remote Runtime and carried
through the execution and diagnostic path:

| Identity | Definition | Owner |
| --- | --- | --- |
| Scan | One `ScanExecutor::Open` producing one `BatchStream`, first pull through terminal state | Remote Runtime |
| Operation | The selected closed protocol operation in the immutable `ScanPlan` (plan-derived, immutable) | Relational Semantics (plan) / Remote Runtime (admission) |
| Traversal step (page) | One fetch-and-decode cycle in the sequential pagination state machine (`INITIAL→REQUESTING→DECODING→DRAINING→NEXT_PAGE\|EXHAUSTED\|FAILED\|CLOSED`); one live at a time | Remote Runtime |
| Transport attempt | One request/response at a traversal step; v1 performs exactly one (ordinal 1) | Remote Runtime |

Scan, step, and attempt identifiers are scan-local ordinals (not global,
serialized, or externally addressable). Operation identity is plan-derived.
None is content; all are structural identifiers, which the redaction contract
already permits.

### 2. Primary failure taxonomy

One closed primary classification per terminal failure. Every existing remote
failure maps to exactly one class:

| Primary class | Absorbs existing failures |
| --- | --- |
| `configuration` | Connector source/YAML/schema/reference/identity/compatibility; Query bind-shape; Semantics invalid-input / no-or-tied-operation / invalid-plan-contract |
| `authorization` | authentication rejection (`401`/`403`, invalid bearer/api_key) |
| `credential_provider` | Query secret lookup/resolution failure; missing-bearer at execution |
| `destination_policy` | network-policy violation, redirect denial, resolved-address policy, continuation target validation mismatch |
| `transport` | connection failure, TLS failure |
| `timeout` | transport-level connect/read idle timeout (distinct from the scan deadline) |
| `remote_status` | HTTP 4xx/5xx other than auth/rate-limit; GraphQL non-empty `errors` |
| `rate_limit` | `429` / `503` with defer intent (terminal in v1; recorded for future waiting) |
| `protocol` | malformed continuation, invalid pagination structure, GraphQL shape violation |
| `decode` | JSON type/structure mismatch during body decode |
| `schema` | schema drift, column type/arity mismatch, lossy integer, strict conversion failure |
| `resource_budget` | any aggregate scan budget counter exhausted (attempts, pages, responses, bytes, records, memory, **time/scan-deadline**, waiting) — see §4 |
| `cancellation` | operator/DuckDB-initiated interruption |
| `internal` | the fixed unknown-exception failure |

Each failure carries stable structured properties (all already permitted by the
redaction contract as codes/phases/structural identifiers/counts):

- `phase` — the existing execution phase at which the failure was classified
  (bind/plan/admit/request/transport/decode/paginate/emit/close);
- `replay_classification` — from §3;
- `attempt` — attempt ordinal within the scan (1 in v1);
- `rows_exposed` — count of rows emitted to DuckDB before this failure (a
  checked count from the ledger; never row content);
- `remote_status_class` — HTTP status class or GraphQL-error presence (e.g.
  `4xx`, `5xx`, `429`, `graphql_errors`); never a status message, body, or
  remote string (consistent with the existing redacted `401`/`403` handling);
- `terminating_budget` — for `resource_budget`/`timeout` failures, the aggregate
  counter that terminated execution.

The four-way distinguishability the goal requires is achieved by class plus
`terminating_budget`/`phase`: cancellation → `cancellation`; scan-deadline expiry
→ `resource_budget` with `terminating_budget = time`; remote transport timeout →
`timeout` (transport phase); local exhaustion → `resource_budget` with
`terminating_budget = memory`/`bytes`/`records`/`pages`.

Two evidence-grounded caveats from the spike: (a) **`timeout` is currently
unproduced** — `curl_transfer.cpp:293-295` sets libcurl's `CURLOPT_TIMEOUT_MS` to
the same scan-deadline value, so today the only time-based termination is the
scan deadline (`resource_budget`/`time`); the `timeout` class is therefore
*reserved* for a future distinct transport idle/connect timeout, which would
itself be a behavior addition requiring its own RFC; and (b) the existing
`ErrorStage` set is coarser than the primary classes above (one `HTTP_STATUS`
covers `remote_status`/`rate_limit`/`authorization`; one `POLICY` covers
`destination_policy`/`protocol`/`configuration`/`resource_budget`; one
`TRANSPORT` also covers some `protocol`), so the primary-class field is the
distinguishing signal. Remote Runtime review validates the exact
class↔phase↔budget assignment.

### 3. Replay classification

Every traversal step and every failure carries a replay decision from a closed
set, combining a Semantics-owned plan obligation with a Runtime-owned exposure
state:

| Classification | Meaning |
| --- | --- |
| `never_replayable` | the operation or step is declared non-replayable, or exposure has already committed the step |
| `replayable_before_exposure` | a transport attempt that failed before this step's rows were emitted; safe to repeat if a mechanism existed |
| `atomic_traversal_step` | the whole fetch-and-decode step is an idempotent replay unit; replay re-fetches the step atomically |
| `server_directed_delay` | the server signaled defer (`Retry-After`/`429`/`503`); terminal in v1, but the fact is recorded |
| `indeterminate` | replay safety cannot be proven — treated as `never_replayable` (guardrail) |

This refines "one request/page is the replay unit, but v1 performs one attempt"
into an explicit, stable fact. The `AGENTS.md` retry invariant now resolves to
two named facts: *declared replay safety* (Semantics plan obligation) and
*uncommitted replay unit* (Runtime exposure state, `rows_exposed` below the step
boundary). In v1 no retry occurs; the classification is recorded so a downstream
goal consumes it without reconstructing safety from context.

The spike confirmed the *declared replay safety* fact already exists end-to-end:
the required author field `replay_safety: safe` compiles to
`CompiledReplaySafety{SAFE}` (`compiled_protocol_operation.hpp:461`) and is
lifted into the plan as `PlannedReplaySafety`
(`rest_operation_planner.cpp:169`, mapper at `scan_planner_internal.hpp:76-81`).
The existing declaration suffices to classify v1 replay safety; no new author
syntax is required, and every accepted v1 operation is replay-safe.

### 4. Aggregate scan budget

The existing scan ledger (`scan_resource_accounting.cpp`, documented in
`RUNTIME_CONTRACTS.md` "Resource accounting") already debits `request_attempts`,
`wall_milliseconds`, `pages`, and the byte/record/memory/string counters as one
checked aggregate. This decision (a) names that aggregate the authoritative scan
resilience budget, (b) adds one new counter dimension — `cumulative_waiting` —
and (c) codifies the no-reset invariant. Invariants:

- every transport attempt debits `attempts`, the relevant response/byte/record
  counters, and elapsed time;
- every (future) wait debits `cumulative_waiting` and elapsed time;
- exceeding any aggregate counter terminates with the `resource_budget`
  class and names the counter in `terminating_budget` (the scan deadline is the
  `time` counter; a distinct transport `timeout` class is reserved, see §2);
- **a retry or wait never resets a counter, deadline, or budget — it debits the
  aggregate** (the guardrail this model exists to enforce);
- arithmetic remains checked unsigned; zero never means unlimited; exceeded or
  overflowing budgets fail without another allocation or request.

In v1, `attempts = 1` per step and `cumulative_waiting = 0` (no mechanism). The
*model* is what is new; no mechanism consumes the waiting budget yet. A future
retry/rate-limit goal debits these counters rather than owning private,
resettable ones.

### 5. Explainable effective policy

`EXPLAIN` output (`ARCHITECTURE.md` "Diagnostics and explanation," which already
"distinguishes operation/protocol identity, predicate accuracy, residual owner,
pagination strategy, resource envelope, and safe provenance") gains the
effective resilience and budget policy: the replay classification applicable to
the operation, the aggregate budget ceilings in effect (host ∩ compiled), and
that no retry/cache/waiting mechanism is enabled. As with all explanation, it is
derived from typed immutable facts and is never parsed for authority.

### Public behavior

The taxonomy is a new structured primary-class field plus properties, layered
on the existing coarse `ErrorStage` classification. Existing rendered diagnostic
strings are preserved verbatim — every successful or failing scan today keeps
its result, its `[duckdb_api][<stage>] ...` prefix, and its safe message — and
the new primary-class field is added alongside them. The spike confirmed that
Query's one-time translation already preserves all ten Runtime `ErrorStage`
values except the intentional `INTERNAL` `SafeMessage` collapse, and preserves
`QueryStagingError` code/phase/coordinates in full.

The new primary-class field is *finer* than today's stages, so within that new
field several currently-conflated or mis-staged failures are corrected (these
are classification refinements in the new field, not changes to existing
strings):

- `429`/`503` (today lumped under `HTTP_STATUS` with other 4xx/5xx) →
  `rate_limit`;
- malformed continuation headers and GraphQL shape violations (today flattened
  to `POLICY` because `LinkPaginationError::Kind()` and `GraphqlCursorError`
  class are dropped at re-throw) → `protocol`;
- GraphQL page-budget exhaustion (today mis-staged as `POLICY`) →
  `resource_budget`;
- transfer-encoding/chunk-framing violations (today `TRANSPORT`) → `protocol`;
- the `AUTHENTICATION` stage (today one bucket) splits into `authorization`
  (401/403 rejection) and `credential_provider` (missing/invalid credential at
  secret resolution or execution).

The new field also *re-introduces* fidelity lost today: Query currently discards
the Semantics `PlanningError` code (`INVALID_CONTRACT` vs
`OPERATION_SELECTION_FAILED`) at its boundary, keeping only the safe message; the
primary-class field carries a structured classification through.

`EXPLAIN` gains the effective resilience and budget policy. No new SQL function,
no new connector-package syntax (the existing compiled REST replay declaration
suffices — see §3), and no new lifecycle mechanism.

### Shared interfaces

- **Remote Runtime (provider):** owns scan/step/attempt ordinal generation; the
  aggregate scan ledger (with the two new counter dimensions); emission of the
  structured failure with taxonomy, properties, and replay classification; and
  the combination of the plan's replay obligation with its exposure state. The
  `BatchStream` terminal-failure contract ("repeats the same safe failure
  classification on later pulls") carries the enriched, stable classification.
- **Relational Semantics (provider):** the immutable `ScanPlan` carries explicit
  replay/exposure obligations, refining the existing "replay/cache/provider
  feature states" into a classified, stable replay decision per operation. The
  plan states what is replayable and the exposure window that defines the replay
  unit; Runtime consumes it without reclassifying relational meaning.
- **Connector Experience (provider):** emits the immutable operation replay
  fact through the existing compiled REST declaration. The spike resolved the
  open minimum-fact question: the required author field `replay_safety: safe` →
  `CompiledReplaySafety{SAFE}` (`compiled_protocol_operation.hpp:461`) already
  suffices to classify v1 replay safety, so no new author syntax or
  `CONNECTOR_SPECIFICATIONS.md` change is required. Connector's compile
  diagnostics are a distinct offline surface mapping 19/19 closed codes to
  `configuration`, outside the remote-scan execution taxonomy except as its
  `configuration` source (with three disambiguations: compile-time
  `POLICY_WIDENING` and `RESOURCE_EXHAUSTED` are not the runtime
  `destination_policy`/`resource_budget` classes).
- **Query Experience (consumer/renderer):** renders the structured diagnostic
  once at the DuckDB exception boundary (preserving the one-translation
  invariant) and renders the effective-policy explain facts. Cancellation remains
  DuckDB interruption.

### Operational behavior

No new concurrency, FFI, initialization, reload, or shutdown behavior. Cancellation
checkpoints remain as today. The only operational change is that a terminal
failure now carries the stable structured properties above and debits the
aggregate budget; the lifecycle, backpressure, and cleanup paths are unchanged.
No retry, wait, cache, or circuit-break path is added or enabled; unknown enum
values for any future mechanism still fail admission.

## Topology impact

| Team | Role in this RFC | Interface or charter impact | Interaction mode | Exit condition |
| --- | --- | --- | --- | --- |
| Query Experience | Sponsor; renders diagnostics and explain | Enriched one-time diagnostic translation and effective-policy explain facts; no charter text change | Collaboration, then X-as-a-Service | A user/operator can read identity, class, replay, attempts, rows-exposed, and terminating budget from one stable diagnostic or explain output |
| Remote Runtime | Primary model owner and implementer | Owns identity ordinals, aggregate ledger with `attempts`/`cumulative_waiting`, structured failure emission, replay+exposure combination | Collaboration, then X-as-a-Service | Query Experience consumes a documented service without Runtime-internal knowledge; deterministic failure/budget oracles cover the complete contract |
| Relational Semantics | Plan obligation provider | `ScanPlan` carries explicit replay/exposure obligations; Runtime executes them without reclassifying relational meaning | Collaboration, then X-as-a-Service | The immutable plan carries complete replay and exposure obligations; a property test proves Runtime does not reclassify |
| Connector Experience | Replay-fact provider | Emits the minimum immutable operation replay fact consumed through the established Connector interface | Collaboration | Validated operations compile into closed replay facts without exposing package syntax to consumers |
| Engineering Enablement | Facilitator | Facilitates the exhaustive failure-taxonomy mapping oracle, aggregate-budget debit/reset oracle, per-field redaction fixtures, and the closed-vocabulary freeze-section extension; transfers each to its owning team | Facilitation | Remote Runtime (mapping/debit-reset oracles), Query Experience (redaction fixtures), and the closed-vocabulary gate owners each demonstrate independent maintenance; Enablement exits |

No accountability or interface boundary moves, so `docs/TEAM_TOPOLOGY.md` and the
active charters need no text change. The decision concentrates resilience
vocabulary in Remote Runtime and replay obligation in Relational Semantics,
matching existing charters; it does not create a new catch-all module.

## Correctness, security, and lifecycle analysis

- **Relational semantics and conservative fallback:** Not affected in its laws.
  Replay classification is a new fact carried *on* the plan; it grants no remote
  authority and changes no predicate/residual/limit/offset ownership. Semantics
  supplies the obligation; Runtime does not reclassify meaning.
- **Authentication, credentials, network policy, and privacy:** Not affected. No
  credential, placement, destination, or network-policy authority changes. The
  new properties contain no secret, body, document, cursor, or row content.
- **Resource budgets, backpressure, and cancellation:** Affected (additive). The
  ledger gains two counter dimensions; cancellation remains interruption and
  remains distinguishable from deadline/timeouts/exhaustion via class +
  `terminating_budget` + `phase`. Backpressure and the one-page-in-flight model
  are unchanged.
- **Replay units, retries, caching, and duplicate prevention:** Affected (model
  only). Replay becomes an explicit, stable, per-step/per-failure fact. No retry,
  cache, deduplication, or single-flight mechanism is enabled. Indeterminate
  replay safety is non-replayable.
- **Concurrency, immutability, and state ownership:** Not affected. Plans remain
  immutable during execution; the ledger remains per-stream; one attempt per step
  in v1.
- **FFI, initialization, reload, shutdown, and failure containment:** Not
  affected. No new FFI, thread, reload, or shutdown path. Non-throwing
  cancel/close/destructors and primary-error preservation are unchanged.
- **Diagnostics, redaction, metrics, and progress reporting:** Affected
  (additive). New stable fields are codes/phases/structural-identifiers/counts
  already inside the redaction contract's allowed set. Each new field needs a
  redaction fixture. Live progress reporting and external metrics/tracing remain
  explicitly out of scope.

## Compatibility and migration

- **Public diagnostics:** additive at the string level. Existing rendered
  prefixes and safe messages are preserved verbatim; a new structured
  primary-class field is added alongside them. The new field is finer than
  today's `ErrorStage`, so it *refines* classification for several currently
  conflated/mis-staged failures (listed in Public behavior) — this is an
  intentional, versioned extension of the public diagnostic surface
  (PM-reserved), not a silent change. No existing successful or failing scan
  changes its result.
- **SQL, connector packages, artifacts, stored data:** no migration required.
  The existing compiled REST replay declaration suffices (no new author syntax);
  no accepted package changes behavior.
- **Team consumers:** the enriched `ScanPlan` obligations and `BatchStream`
  structured facts are private pre-release team interfaces; all consumers migrate
  atomically inside the repository.
- **Rollback:** restores the coarse owner-boundary classification and reopens the
  three gaps in Problem. Rollback is acceptable only as short-lived containment;
  it cannot satisfy the interaction exits or enable a later resilience goal
  safely.
- **Unsupported capability profiles:** still fail closed. This RFC does not
  authorize retry, waiting, caching, circuit breaking, single-flight, new
  credential providers, progress reporting, or metrics export.

## Evidence and bounded trials

| Question or claim | Evidence required | Method or fixture | Result and limitations |
| --- | --- | --- | --- |
| Every existing remote failure maps to at least one primary class | Exhaustive mapping of every failure-emission site | Spike: ~250 Runtime + 39 Semantics-grouped + ~91 Query emission sites tabulated against the taxonomy | **Established:** every site maps to ≥1 class; no gaps. Not 1:1-preserving of current `ErrorStage` — several failures are mis-staged/conflated today and are corrected within the new primary-class field (see Public behavior) |
| Existing rendered diagnostic strings are preserved; new field is additive | Public-surface-inventory diff + boundary inspection | Spike: Query translation preserves 10 `ErrorStage` values (except the intentional `INTERNAL` collapse) and full `QueryStagingError`; discards the Semantics `PlanningError` code today | **Spike finding:** existing strings are preservable verbatim; the new primary-class field re-introduces the Semantics code lost at the Query boundary. Inventory gate runs during delivery |
| The scan ledger already supports aggregate attempts/waits debit | Code inspection of the ledger | Spike: `scan_resource_accounting.cpp` already debits `request_attempts`, `wall_milliseconds`, `pages`, and byte/record/memory counters as one checked aggregate | **Established:** attempts are already aggregate; the model adds `cumulative_waiting` and the no-reset invariant. Delivery confirms no reset path exists |
| Replay classification splits cleanly between Connector-declared fact and Semantics-derived obligation | Minimum-fact determination | Spike: traced `replay_safety: safe` → `CompiledReplaySafety{SAFE}` (`compiled_protocol_operation.hpp:461`) → `PlannedReplaySafety` (`rest_operation_planner.cpp:169`) | **Resolved:** the existing declaration suffices for v1; Connector-declared fact and Semantics obligation already share one value. No new author syntax |
| New diagnostic fields are redaction-safe | Per-field redaction fixture | One fixture per new field asserting no body/document/cursor/row/secret content | Pending — run during delivery |
| Distinguishability of cancellation/deadline/remote-timeout/exhaustion | Four-way oracle + source inspection | Spike: traced `curl_transfer.cpp:293-295` (time termination) and the `ExecutionCancelled` path | **Spike finding:** 3 of 4 emitted today (cancellation/deadline/exhaustion); `timeout` is unproduced (reserved — no distinct transport timeout exists; curl timeout = scan deadline). Oracle over the produced cases runs during delivery |
| The new closed diagnostic vocabularies are protected against silent drift | Checked closed-set freeze section + mutation tests | `contract_freeze.py` never reads `freeze["diagnostics"]` (grep-confirmed); `inventory.schema.json` is table-function-only | Pending — delivery scopes the Enablement-facilitated gate extension enumerating the 14 classes + 5 replay classifications with removal/rename mutation tests |

The spike (rows 1, 2, 3, 4, 6) is complete and establishes exhaustiveness, the
Query-boundary fidelity baseline, the aggregate-ledger basis, the replay-fact
resolution, and the `timeout` reservation. The remaining rows (per-field
redaction fixtures, the distinguishability oracle over produced cases, the
inventory gate) run during delivery. None of this enables a mechanism.

## Alternatives considered

1. **Establish the unified model now, no mechanism enabled (proposed).** Benefit:
   downstream resilience goals consume one stable contract; terminations are
   distinguishable; budgets cannot be reset. Drawback: a classification/accounting
   layer across the execution path that is unused by any mechanism in v1, needing
   exhaustive oracles to keep it honest.
2. **Defer the model until the first resilience mechanism is added.** Rejected:
   that mechanism would then invent mechanism-specific identities and counters,
   which the linked goal explicitly forbids, and terminations stay
   indistinguishable in the meantime.
3. **Per-mechanism counters owned by each future retry/cache mechanism.** Rejected:
   violates "no resetting safety limits" — private counters could reset a deadline
   or budget mid-scan. The aggregate ledger exists precisely to prevent this.
4. **Keep the coarse owner-boundary classification as the primary taxonomy.**
   Rejected: it cannot distinguish cancellation, deadline expiry, remote timeout,
   and local exhaustion, which the goal requires, and it conflates replay
   decisions.
5. **Publish a full public SQL-facing error catalog (e.g. SQLSTATE-style)
   immediately.** Out of scope: a larger public-SQL contract change than this
   goal needs. This RFC keeps public diagnostics additive and reserves a full
   error catalog for a later product decision.

## Drawbacks and failure modes

- A classification layer that no v1 mechanism consumes can drift silently. The
  exhaustive mapping oracle and aggregate-budget debit/reset oracle must be
  mandatory gates, not optional tests. Engineering Enablement facilitates their
  establishment and transfers them to Remote Runtime, which maintains them
  independently (Facilitation exit on demonstrated independent maintenance).
- The new closed vocabularies (14 primary classes, 5 replay classifications) are
  not protected against silent removal or rename by any existing gate (see
  Contract propagation). They require a checked closed-vocabulary freeze section
  with mutation tests, scoped below as an Enablement-facilitated gate extension.
- `server_directed_delay` and `cumulative_waiting` are recorded but unused in v1.
  They are stable hooks for a downstream goal; misclassification would mislead
  that goal. Mitigated by the replay-classification oracle.
- Diagnostic surface growth increases redaction surface area. Each new field
  needs a redaction fixture; Engineering Enablement facilitates and transfers to
  Query Experience, which maintains them independently.
- The timeout/scan-deadline class split is subtle. If Remote Runtime review finds
  the proposed `timeout` vs `resource_budget(time)` split unsound, the class set
  is adjusted within this RFC before acceptance.
- Over-broad plan replay obligations could smuggle relational meaning into
  Runtime. Relational Semantics review must confirm Runtime consumes — not
  reclassifies — the obligation.

## Acceptance and verification

- **End-to-end demonstration:** run representative successful, failed, canceled,
  and budget-exhausted scans; from the stable structured diagnostic and/or
  `EXPLAIN` alone, confirm scan/step/attempt identity, primary class, replay
  classification, attempt count, rows-exposed, and terminating budget are visible
  and correct.
- **Automated oracle:** exhaustive failure-taxonomy mapping (every existing remote
  failure path → exactly one class); four-way termination-distinguishability
  oracle (cancellation/deadline/remote-timeout/exhaustion); aggregate-budget
  oracle proving attempts and waits debit shared counters and cannot reset a
  deadline or counter; per-field redaction fixtures; a property test proving
  Runtime consumes the plan's replay obligation without reclassifying.
- **Failure-path evidence:** failures before connection, before headers, during
  body receipt, during decode, after traversal-step acceptance, during
  cancellation, and at each resource boundary.
- **Quality gates:** `AGENTS.md` "Current verification" gates
  (`validate-agent-assets`, `verify-contract-freeze` + tests, `git diff --check`);
  source-identity and native-dependency gates if product source changes; `make
  build`, `make test`, `make demo` on the supported cell. Note:
  `verify-public-surface-inventory` governs SQL table-function shapes only
  (`inventory.schema.json` locks `kind` to `table_function`) — it does not
  represent diagnostic fields or `EXPLAIN` facts and so does not validate this
  RFC's diagnostic additions unless extended.
- **Closed-vocabulary protection:** a gate extension (facilitated by Engineering
  Enablement, transferred to the owning teams) enumerates the 14 primary failure
  classes and 5 replay classifications as a checked freeze/closed-set section
  with mutation tests proving removal or rename fails. This is the mechanical
  backstop against silent drift of the vocabularies whose stability is this RFC's
  purpose.
- **Independent review:** `$topology-consult` review from all five required
  reviewers; `$adversarial-review` of replay classification, resource accounting,
  diagnostic redaction, and lifecycle/cancellation semantics.
- **Interaction exit:** Query Experience renders the contract without
  Runtime-internal knowledge; Runtime's failure/budget oracles cover the complete
  contract; the plan carries complete replay/exposure obligations; Connector
  compiles closed replay facts.

## Contract propagation

| Source of truth or artifact | Impact | Required update | Completion evidence |
| --- | --- | --- | --- |
| `docs/ARCHITECTURE.md` | Affected | Record the identity/taxonomy/replay/budget model under "Diagnostics and explanation" and refine the "V1 performs one attempt" lifecycle statement into "one attempt, classified" | Pending implementation |
| `docs/CONNECTOR_SPECIFICATIONS.md` | Not affected | Spike resolved the open question: the existing `replay_safety: safe` declaration suffices; no author-syntax change | Not applicable |
| `docs/RUNTIME_CONTRACTS.md` | Affected | Extend "Error ownership and redaction" with the taxonomy + structured properties; name the existing aggregate ledger (incl. `request_attempts`) the authoritative resilience budget, add `cumulative_waiting`, and codify the no-reset invariant in "Resource accounting"; make replay classification explicit in "Cancellation, close, and failure" and the pagination/lifecycle sections; record explicit `ScanPlan` replay/exposure obligations | Pending implementation |
| `docs/TEAM_TOPOLOGY.md` and active charters | Not affected | No interface or accountability boundary moves; the model is consumed through existing interfaces | Not applicable |
| `docs/PRODUCT_DELIVERY.md`, `AGENTS.md`, and skills | Not affected | Existing RFC, contract-change, delivery, and review practices cover implementation | Not applicable |
| `ROADMAP.md` | Affected (placement) | Record the foundational resilience-accounting model; release placement is a separate product-manager decision (it is a cross-release foundation, not inherently a single `0.Y.0` outcome) | Pending product-manager placement decision |
| Public-surface inventory | Not affected (mechanically) | The inventory governs SQL table-function shapes only (`inventory.schema.json` locks `kind` to `table_function`); diagnostic fields and `EXPLAIN` facts are not table functions and cannot be represented without a schema+verifier extension. No inventory change is required for this RFC | Not applicable |
| `release/1.0.0/freeze.json` | Affected | The freeze `diagnostics` block is currently unchecked prose (`contract_freeze.py` never reads it). The new closed vocabularies (14 primary classes, 5 replay classifications) need a checked freeze/closed-set section with mutation tests to protect against silent removal/rename — scoped as an Enablement-facilitated gate extension | Pending implementation (gate extension) |
| Examples, diagnostics, fixtures, and tests | Affected | New mapping/distinguishability/budget/redaction oracles and effective-policy explain fixtures | Pending implementation |

The RFC records rationale; these sources define current behavior and operation.

## Unresolved questions

- Non-blocking: exact spelling of the primary class and replay-classification
  enum values — cosmetic, resolved at implementation time, following prior RFC
  precedent.
- Resolved by the spike (pre-review): (a) the minimum replay fact Connector must
  emit — the existing `replay_safety: safe` declaration suffices, so no new
  author syntax; and (b) the `timeout` vs `resource_budget(time)` split —
  `timeout` is reserved (no distinct transport timeout is emitted today; curl's
  timeout is the scan deadline), so scan-deadline expiry is `resource_budget`
  with `terminating_budget = time`. Both are recorded for confirmation in the
  Review record.
- Placement: which release (if any) this foundational model lands in is a
  product-manager decision, not a technical blocker for the contract itself.

## Review record

| Required reviewer | Team | Result | Evidence or objection | Disposition by decision owner |
| --- | --- | --- | --- | --- |
| Query Experience perspective | Query Experience | Approved | Verified one-time translation preserves 10 `ErrorStage` values except the intentional `INTERNAL` `SafeMessage` collapse (`relation_execution.cpp:46-49`); discarded Semantics `PlanningError` code confirmed (`table_function_adapter.cpp:74`); cancellation remains `InterruptException` (`relation_execution.cpp:63`); `EXPLAIN` facts derivable from immutable plan (`scan_plan_explanation.cpp:305-348`); aggregate ledger/no-reset confirmed | No decision-level action; delivery adds per-field redaction fixtures and effective-policy explain entries. Exit Open until end-to-end demo + gate evidence pass |
| Remote Runtime perspective | Remote Runtime | Approved | Source-verified: `timeout` reserved (curl timeout = scan deadline, `curl_transfer.cpp:222,257-258,293-294`); 429/503 in `HTTP_STATUS`, malformed-Link flattened to `POLICY`, GraphQL page-budget mis-staged as `POLICY` (`graphql_cursor_pagination.cpp:76-78`) — all confirmed and reclassifiable; aggregate ledger already debits `request_attempts`; no-reset invariant structural; redaction preserved. Notes `wall_milliseconds` is a deadline comparison, not a debited counter (wording), and `GraphqlCursorError` must carry class structurally, not via message parsing | No decision-level action; delivery makes the mapping/debit-reset/distinguishability oracles mandatory gates and structurally disambiguates `GraphqlCursorError` class. Exit Open until oracles land |
| Relational Semantics perspective | Relational Semantics | Approved | Verified `replay_safety` is a single-value closed enum at both layers, lifted by pure structural copy independent of predicate/residual/projection/ordering/limit/cardinality (`rest_operation_planner.cpp:169`); origin checks are plan-construction validations → `configuration` is the correct split (mapping to `destination_policy` would conflate offline fallback with execution enforcement and violate network-free planning); replay classification encodes no ordering/snapshot/multiplicity/cardinality claim | No decision-level action; delivery adds the property test proving Runtime consumes without reclassifying and refines the feature-states slot. Exit Open until those land |
| Connector Experience perspective | Connector Experience | Approved | Verified `replay_safety: safe` → `CompiledReplaySafety{SAFE}` (`compiled_protocol_operation.hpp:29,461`) is a single-value closed enum, schema-required (`package_operation_schema.cpp:55,59-60`), lifted to `PlannedReplaySafety`; no new author syntax, no `CONNECTOR_SPECIFICATIONS` change. 19 `PackageDiagnosticCode` values map 19/19 to `configuration`; `POLICY_WIDENING`/`RESOURCE_EXHAUSTED` are compile-time, correctly disambiguated. Execution-internal facts stay Runtime-owned, not package syntax | Exit Satisfied — the closed replay fact already flows through the established Connector interface; no remaining open work |
| Engineering Enablement perspective | Engineering Enablement | Approved (on re-review) | Initially Objected: `inventory.schema.json:6` is `additionalProperties:false` with `kind` locked to `table_function` (`:252`), making the original "additive inventory entries" claim mechanically false; `contract_freeze.py` never reads `freeze["diagnostics"]` (grep-confirmed), so the closed vocabularies had no drift protection; oracle co-ownership lacked a transfer clause. Re-review verified the revisions resolve all three | Objection accepted; RFC revised in Contract propagation, Acceptance, Drawbacks, and Topology to correct the gate mechanics, scope a checked closed-vocabulary freeze section with removal/rename mutation tests, and rewrite oracle ownership as a clean Facilitation transfer. Re-review Approved with `none` required for decision approval; gate extension lands in delivery. Exit Open until oracles + gate extension are delivered and teams demonstrate independent maintenance |

All five required reviewers returned a disposition. Four approved on first
review (Query Experience, Remote Runtime, Relational Semantics, Connector
Experience), each source-verifying the RFC's claims against its boundary.
Engineering Enablement objected that the RFC credited the public-surface
inventory and contract freeze with protection they do not provide (validated: the
inventory is table-function-only; `verify_freeze` never reads the `diagnostics`
block), leaving the new closed vocabularies unprotected and the Facilitation exit
ambiguous. The decision owner accepted the objection as correct and
evidence-backed and revised the RFC in four sections (Contract propagation,
Acceptance, Drawbacks, Topology) to correct the gate mechanics, scope a checked
closed-vocabulary freeze section with mutation tests, and rewrite the oracle
ownership as a clean Facilitation transfer. Engineering Enablement re-reviewed
the revised RFC and Approved with no decision-level action. No unresolved
objection remains.

## Decision and rationale

- **Technical decision owner:** Lead agent.
- **Technical decision:** Accept. The failure-mapping spike established
  exhaustiveness and resolved the replay-fact and `timeout` questions; the
  additive-string-compatibility finding holds (existing rendered strings are
  preserved verbatim; the new primary-class field corrects several currently
  mis-staged failures); and all five required reviewers approved, with the single
  Engineering Enablement objection accepted and resolved in-text.
- **Product approval:** Approved — Nic Galluzzo, 2026-07-22. The additive
  public-diagnostic-surface extension (a new structured primary-class plus
  properties field, plus `EXPLAIN` effective-policy facts; existing rendered
  strings preserved verbatim) is accepted as an intentional, versioned extension
  of the public diagnostic surface under the pre-`1.0.0` versioning model. This
  is the reserved decision the goal guardrail required.
- **Rationale:** The model is additive (no mechanism enabled), its central thesis
  — that today's coarse `ErrorStage` conflates genuinely distinct failure classes
  — is proven by source, the no-reset guardrail is already structurally enforced,
  all five required reviewers approved, and the one objection was accepted and
  resolved in-text. The additive-string-compatibility finding holds: existing
  rendered diagnostics are preserved verbatim while the new primary-class field
  carries the finer classification.
- **Material objections:** Engineering Enablement objected that the RFC credited
  the inventory and freeze gates with protection they do not provide, leaving the
  new closed vocabularies unprotected; accepted and resolved by correcting the
  gate mechanics and scoping a checked closed-vocabulary freeze section with
  mutation tests. No unresolved objection remains.
- **Superseded by:** Not applicable.

## Follow-on goals

| Outcome or objective | Accountable team | Supporting teams and interaction modes | Activation condition |
| --- | --- | --- | --- |
| Implement the resilience-accounting contract (identities, taxonomy, replay, aggregate budget, effective-policy explain) with no mechanism enabled | Query Experience | Remote Runtime (Collaboration), Relational Semantics (Collaboration), Connector Experience (Collaboration), Engineering Enablement (Facilitation) | This RFC accepted; production-resilience foundation goal activated via `docs/PRODUCT_DELIVERY.md` |
| Add automatic retry that debits the aggregate budget | Query Experience | Remote Runtime, Relational Semantics | Not activated by this RFC — consumes this contract; requires its own RFC |
| Add rate-limit waiting that debits `cumulative_waiting` | Query Experience | Remote Runtime | Not activated by this RFC — consumes this contract; requires its own RFC |
| Add author-configurable caching / single-flight | Query Experience | Remote Runtime, Relational Semantics | Not activated by this RFC — out of scope pending dedicated analysis |

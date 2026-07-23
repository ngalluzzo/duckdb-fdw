# RFC 0024: Enable bounded replay-safe scan retry

```yaml
rfc: "0024"
title: "Enable bounded replay-safe scan retry"
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
affected_teams:
  - "Query Experience"
  - "Remote Runtime"
  - "Relational Semantics"
  - "Connector Experience"
linked_outcome_or_objective: "For DuckDB users querying transiently unreliable APIs, automatically replay only operations and traversal steps proven safe, without duplicating exposed rows, changing credential identity, hiding permanent failures, or resetting scan budgets."
supersedes: "none"
```

## Summary

Enable automatic retry for closed read-only REST and GraphQL profiles whose
package explicitly recommends bounded retry and whose current uncommitted
traversal step proves replay safety. A retry repeats the exact step with the
same credential snapshot, on a fresh policy-checked connection, and consumes
one aggregate attempt, byte, wait, and deadline budget. The initial retry
matrix is limited to structurally pre-response transient transport failures
and fully received HTTP `502`, `503`, and `504` responses. `429`, ambiguous
partial responses, permanent failures, decode or continuation failures,
cancellation, deadline expiry, and any exposed step remain terminal.

## Sponsorship and context

- **RFC type:** Product. This changes successful query behavior, failure and
  explanation diagnostics, retry/resource/cancellation policy, and the shared
  `ScanPlan` and `BatchStream` interfaces.
- **Sponsoring team:** Query Experience. Acceptance ends with a DuckDB user
  obtaining the same accepted occurrence bag from a transiently failing read,
  or a precise terminal diagnostic when replay is unsafe or exhausted.
- **Linked outcome or objective:** The production-resilience retry goal
  supplied by the product manager on 2026-07-23.
- **Why now:** Accepted RFC 0021 supplies explicit failure, replay, exposure,
  attempt, and aggregate-wait facts. Accepted RFC 0023 supplies one immutable
  authority/revision snapshot for every page and future attempt. Retrying
  before either foundation would have required unsafe inference or mutable
  credential lookup.

## Problem

Every admitted request or page currently receives exactly one attempt. A
transient gateway failure or connection close therefore terminates a safe read
even though the plan proves replay safety and Runtime has not exposed a row
from that step.

A generic transport retry loop is not acceptable. Current transport errors can
represent no response, a partial response, TLS or destination-policy failure,
or malformed framing. Current page accounting also couples `pages += 1` with
`attempts += 1`, and failure cleanup makes the ledger terminal. Retrying that
coarse error or calling `BeginPage` again could conceal ambiguity, count a
retry as a new page, advance a cursor twice, refresh credentials, or reset a
budget.

## Decision drivers and invariants

- **Must preserve:** a retry requires declared replay safety and an uncommitted
  replay unit; indeterminate safety is non-replayable.
- **Must preserve:** one complete response is received, decoded, schema-checked,
  continuation-validated, resource-accepted, and installed before any row from
  that step becomes observable.
- **Must preserve:** one credential snapshot and opaque authority/revision
  identity for the complete scan, including every attempt.
- **Must preserve:** exact origin, address policy, authentication placement,
  byte limits, and cancellation checks on every attempt.
- **Must preserve:** attempts, pages, bytes, records, memory, waiting, and the
  scan deadline debit one checked aggregate and never reset.
- **Must enable:** the scripted `503 -> pre-response connection reset -> valid
  page` sequence with one accepted copy of every occurrence and structured
  attempt/wait evidence.
- **Must not introduce:** `429` retry or server-directed waiting, write
  operations, idempotency-key authoring, caching, parallel pages, resume,
  deduplication, snapshot claims, or result single-flight.

## Proposed decision

### 1. Operation replay classification

Operation semantics and execution state remain separate closed facts.

`CompiledConnector` and `ScanPlan` classify an operation as exactly one of:

| Operation class | Meaning | Initial support |
| --- | --- | --- |
| `non_replayable` | The operation is known unsafe to repeat | Never retried |
| `replayable_read` | A closed read profile is safe to repeat before commitment | Existing REST `GET` and admitted canonical GraphQL query profiles |
| `replayable_with_idempotency_mechanism` | Repetition depends on an explicit stable mechanism | Reserved and rejected by v1/v2; no write or idempotency-key syntax |
| `unknown` | Safety is absent or cannot be proved | Treated as non-replayable |

The existing author-visible REST `replay_safety: safe` declaration compiles to
`replayable_read` only when the complete REST `GET` profile agrees. Canonical
GraphQL query replay safety remains derived from document identity, query kind,
variables, response profile, and base domain rather than from an independent
author assertion. Replay safety is necessary but does not itself opt an
operation into retry.

`duckdb_api/v2` retains the complete v1 grammar and permits an operation to
additionally declare this closed recommendation:

```yaml
retry:
  max_attempts_per_step: 3
  max_delay_milliseconds: 100
  max_cumulative_waiting_milliseconds_per_scan: 250
```

All three fields are required when `retry` is present. Attempts are in `2..3`,
one delay is in `1..100`, and cumulative waiting is in `1..250`. Unknown or
partial fields fail schema validation. The compiler accepts the block only for
an operation classified `replayable_read`; non-replayable, explicit-idempotency,
and unknown operations reject it. An absent block compiles to retry disabled,
one attempt, and zero wait. `duckdb_api/v1` remains its frozen one-attempt
grammar and rejects `retry` as unknown, preserving its existing identity and
behavior.

RFC 0021's `ReplayClassification` remains the Runtime combination of this plan
fact with step commitment/exposure. It is not replaced by the operation enum.

### 2. Immutable retry policy and composition

The compiled operation carries the validated retry recommendation separately
from its operation class. An opted-in `replayable_read` carries the declared
attempt/delay/wait ceilings plus an aggregate-attempt recommendation derived
below. An operation without the block carries one attempt per step, one checked
attempt per reachable step, and zero wait.

Runtime owns hard maxima of the same values, including 96 aggregate attempts.
Query's executor construction profile supplies the operator policy; the
installed profile uses the same three-attempt/96-scan-attempt/100-millisecond/
250-millisecond ceiling, while private hosts and tests may only narrow it.
There is no SQL or environment override in this goal. The effective policy is
the field-wise minimum of Runtime hard maxima, operator policy, and connector
recommendation. Zero wait or one attempt disables retry; zero never means
unlimited.

Semantics writes both ceilings into the immutable plan. For a single-response
operation the planned aggregate attempt ceiling equals the per-step ceiling.
For pagination it is the checked product
`max_pages_per_scan * max_attempts_per_step`, narrowed by the connector's
aggregate recommendation and the hard 96-attempt ceiling; overflow fails
planning. Runtime admission then intersects the planned per-step and aggregate
values with the operator profile and hard maxima. It does not reconstruct the
product from page state. Thus a 32-page, three-attempt plan carries exactly 96
planned aggregate attempts, while an operator may deterministically narrow it.

Retry is automatic after an author opts in; there is no per-query switch. Three
attempts is the least request amplification capable of satisfying the supplied
three-step recovery demonstration. Existing v1 package bytes remain
one-attempt. Migrating a package from `duckdb_api/v1` to `duckdb_api/v2`, or
adding or changing the retry block on an existing v2 operation, requires a
package-major version under RFC 0013 because it changes specification identity,
replay/resource/failure behavior, and can make at most two additional requests
per step.

### 3. Closed retry matrix

Runtime retries only when all of these are true:

1. operation class is `replayable_read`;
2. the current step has accepted or exposed no row;
3. the attempt and wait/deadline budgets authorize another attempt; and
4. the final attempt disposition is one of:
   - a closed transient transport code for resolution, connect, send, empty
     response, or receive failure **and** response code, received header bytes,
     and received body bytes are all zero; or
   - a fully received HTTP `502`, `503`, or `504` response whose terminal
     header block contains no `Retry-After` field.

TLS verification/configuration failure, destination-policy denial, framing
failure, any response with partial headers or body, timeout/deadline expiry,
`429`, other HTTP status, authorization, credential-provider, resource,
protocol, decode, schema, pagination, cancellation, or internal failure is
terminal. No diagnostic string or dependency message participates in the
decision.

`429` remains `rate_limit/server_directed_delay`. Any otherwise candidate
`502`, `503`, or `504` carrying a `Retry-After` field also remains terminal
`rate_limit/server_directed_delay`. The transport retains only a bounded
boolean stating that the terminal header block contained the field; it never
retains, parses, renders, or acts on the field value. A candidate gateway
status without that field is classified as `remote_status/server_error` for
this retry goal. Interpreting `Retry-After` or waiting for its value belongs to
the separate rate-limit goal.

### 4. Attempt, page, and acceptance state

Runtime's page ledger becomes:

```text
READY
  -> STEP_ACTIVE (page debited once)
  -> ATTEMPT_ACTIVE (attempt debited)
       -> STEP_ACTIVE (retryable failure; observed use committed)
       -> RESPONSE_COMMITTED
  -> PAGE_ACCEPTED
  -> DRAINING
  -> READY | EXHAUSTED
```

Every attempt, including a failed one, debits the request body and every
observed header/wire/decompressed byte before a retry decision. A retry never
debits another page. A response or decode failure cannot roll counters back.
The ledger enforces both the per-step and aggregate attempt ceilings carried by
the admitted policy. When a retryable final cause consumes the last authorized
attempt, diagnostics preserve that cause and set `terminating_budget=attempts`;
when a narrowed aggregate ceiling prevents even the first attempt of a later
step, the terminal cause is `resource_budget/attempts`. Neither case performs
an unauthorized request.
Pagination next-page/cursor state is staged beside the decoded page and becomes
current only when the page is accepted. Retrying therefore rebuilds the exact
same request from unchanged step state.

Acceptance is the atomic boundary after complete transport, status acceptance,
decode, schema validation, continuation validation, resource accounting, and
decoded-page installation. Exposure begins only when Query receives the first
batch from that accepted page. A later page can still be retried before its own
acceptance even when earlier pages exposed rows; cumulative scan rows remain a
diagnostic count, not the step-local commitment decision.

This contract guarantees that Runtime emits every accepted page occurrence at
most once and never duplicates an exposed step. It does not promise a snapshot
or equality with a counterfactual live mutable API response; deterministic
fixtures prove equal bags only because their remote transcript is fixed.

### 5. Backoff, jitter, and cancellation

After retryable failed attempt `n`, the nominal delay is
`min(10 * 2^(n-1), effective_max_delay)` milliseconds, using checked arithmetic.
A stream-local seed selects a deterministic value in the inclusive
`[75%, 125%]` interval around that nominal value, then clamps it to the single
delay and remaining cumulative-wait ceilings. Tests inject the seed; production
seeds mix scan-local execution entropy so concurrent scans do not share one
schedule. Jitter never changes retry eligibility.

The waiter debits bounded elapsed slices through the aggregate `CommitWait`
path and checks the one scan deadline plus host/stream cancellation at least
every five milliseconds. Cancellation remains DuckDB interruption and never
starts another request. `Cancel`, `Close`, and destruction remain non-throwing
and idempotent.

### 6. Credential and destination identity

The credential provider is resolved once after complete plan admission.
Retries reuse the same move-only authorization snapshot and opaque
authority/revision identities; `401`/`403`, replacement, deletion, or
environment rotation never cause re-resolution inside the scan.

Every attempt builds and decorates a fresh request from the immutable admitted
profile, opens a fresh unshared connection, repeats DNS resolution, and
reapplies the exact origin/address/redirect/proxy/TLS policy. Re-resolution may
narrow or deny the destination and can never widen authority.

### 7. Diagnostics and explanation

`FailureProperties` adds cumulative delay and a closed exposure state
(`unaccepted`, `accepted_unexposed`, `exposed`). Its attempt ordinal is the
final attempt for the current step. Retry exhaustion preserves the final
underlying failure class and status while marking the attempts budget as the
reason no further attempt was permitted.

`BatchStream` exposes a content-free structured execution snapshot containing
the admission-effective attempt/delay ceilings, aggregate attempts, cumulative
delay, current step, and exposure state. It is available after successful
recovery as well as terminal failure, allowing the Runtime demonstration and
consumer diagnostics to observe recovery without a new metrics exporter or SQL
function. Query's terminal suffix renders final attempt, cumulative delay,
class, exposure, cumulative rows, and any terminating budget.

Pre-execution `EXPLAIN` renders the immutable **planned connector
recommendation**, not the later admission-effective policy. It labels that
distinction explicitly and keeps rate-limit waiting and cache disabled. Runtime
alone intersects the plan recommendation with its hard and operator ceilings at
`ScanExecutor::Open`; Query neither imports Runtime-private policy nor performs
admission during bind/planning. Execution diagnostics are the authoritative
effective-policy record. A narrowed operator profile must therefore produce a
smaller Runtime diagnostic than the planned recommendation without changing
`EXPLAIN` or granting another attempt.

All fields are codes, ordinals, or checked counts. They contain no URL, header,
body, row, cursor, credential, authority/revision identity, dependency message,
or timing timestamp.

## Public behavior

Existing valid `duckdb_api/v1` package bytes keep their syntax, result schema,
identity, and one-attempt behavior. A `duckdb_api/v2` author can opt a proved
safe-read operation into the closed transient matrix with the bounded `retry`
recommendation. Migrating an existing package requires a package-major version
change. Permanent or unsafe failures keep their existing redacted stage/message
and gain additive retry diagnostics. `EXPLAIN` renders disabled for v1 or an
absent v2 block, or the planned bounded recommendation for an opted-in v2
operation, labeling it as such; Runtime execution diagnostics carry the
admission-effective policy. There is no new SQL function, table-function
argument, setting, or environment input.

## Shared interfaces

- **Connector Experience:** schema and validation own the explicit retry block;
  compiled operations expose the closed operation class and validated immutable
  recommendation. Consumers do not reinterpret method strings, GraphQL
  documents, or author YAML.
- **Relational Semantics:** `ScanPlan` carries the operation class, enabled or
  disabled retry fact, immutable recommendation, attempt budget, and wait
  budget. Planning remains deterministic and performs no I/O.
- **Remote Runtime:** admission intersects plan recommendation with hard and
  operator ceilings; a reusable retry controller owns dispositions, delays,
  attempt/page accounting, diagnostics, and interruptible waiting.
- **Query Experience:** consumes the documented `BatchStream` service and
  renders plan/failure diagnostics without transport knowledge.

## Topology impact

| Team | Role in this RFC | Interface or charter impact | Interaction mode | Exit condition |
| --- | --- | --- | --- | --- |
| Query Experience | Sponsor and diagnostics consumer | Retry-enabled query outcome, `EXPLAIN`, terminal rendering, and `BatchStream` diagnostic consumption | Collaboration, then X-as-a-Service | Ordinary SQL and controlled demonstrations recover or fail with the promised structured evidence without adapter retry logic |
| Remote Runtime | Retry service provider | Typed attempt disposition, atomic page state, accounting, jittered wait, cancellation, and diagnostics | Collaboration, then X-as-a-Service | One independently tested service covers REST and GraphQL and Query consumes it without Runtime internals |
| Relational Semantics | Replay-obligation provider | Closed operation class, immutable policy, and attempt/page budget laws | Collaboration, then X-as-a-Service | `ScanRequest -> ScanPlan` oracles fail closed and Runtime executes without reclassifying meaning |
| Connector Experience | Replay-fact provider | Frozen v1 replay facts plus the v2 retry block compile to closed immutable classes/recommendations | Collaboration, then X-as-a-Service | V1 bytes retain one-attempt identity; opted-in package-major v2 bytes compile deterministically; consumers use only the established Connector API |

No accountability boundary or charter text changes. Engineering Enablement is
not affected merely because the ordinary freeze and verification gates change.

## Correctness, security, and lifecycle analysis

- **Relational semantics:** Predicate, residual, projection, ordering, limit,
  offset, and base-row cardinality ownership do not change. Differential tests
  compare optimized and forced-local results over duplicate-bearing fixtures.
- **Authentication/network/privacy:** Credential material is transmitted more
  than once only to the same admitted authenticator and destination. Every
  attempt revalidates network policy. Diagnostics remain content-free.
- **Resources/backpressure/cancellation:** Attempts and failed-attempt bytes are
  additive; one page remains in flight; waits and requests are interruptible;
  no deadline or counter resets.
- **Replay/duplicates:** Only an unaccepted/unexposed safe-read step can repeat.
  Runtime never deduplicates source occurrences and never replays an exposed
  page.
- **Concurrency/immutability:** Mutable retry, accounting, page, and seed state
  is per stream. Plans and credential snapshots remain immutable. Streams share
  no counters or retry state.
- **FFI/lifecycle:** No public ABI or new thread is added. Pull, cancel, close,
  destruction, and stable repeated terminal failure retain their contracts.
- **Diagnostics:** Final cause is preserved; retry exhaustion cannot hide a
  permanent error or turn partial success into exhaustion.

## Compatibility and migration

`duckdb_api/v1` remains the frozen compatibility family accepted by RFC 0013.
Existing package bytes compile to the same one-attempt behavior and need no
migration. `duckdb_api/v2` is a new exact specification identifier: it retains
the v1 shapes, adds only the closed retry recommendation in this RFC, and uses
separately copied, identity-checked schemas. A package moving from v1 to v2 or
adding/changing `retry` must increment its package major version; the spec and
source digest change, the normalized compiled descriptor records the
recommendation, compiled explanation and fixtures identify it, and reload
classifies the change as incompatible rather than reinterpreting an old
generation in place. No separate "compatibility digest" is invented: source
identity and normalized structural comparison retain their current roles.

Rollback keeps every v1 package readable and one-attempt. A v2 package is
rejected by an older extension as an unsupported exact specification rather
than silently losing retry; operators publish a new package-major v1 generation
without the block before rollback. Rollback cannot alter an active scan because
each stream retains one immutable plan and policy snapshot. Unknown spec IDs,
operation classes, retry states, dispositions, or policy fields fail before
credentials or transport.

## Evidence and bounded trials

| Question or claim | Evidence required | Method or fixture | Result and limitations |
| --- | --- | --- | --- |
| Pinned transport distinguishes no response from partial response | Structural response code/header/body facts after `curl_easy_perform` failure | Local four-case TCP transcript using macOS system libcurl 8.7.1, the repository's pinned product cell | `pre_header_close -> exit 52, code 000, headers 0, body 0`; `partial_body_close -> exit 18, code 200, headers 59, body 3`; complete `503` and `200` return normally. This proves the distinction, not the production mapping implementation. |
| HTTP permits automatic repeat only with semantic evidence | Primary protocol source | RFC 9110 sections 9.2.2 and 10.2.3 | Idempotent requests may repeat after communication failure; non-idempotent requests require independent knowledge. `Retry-After` is separate server-directed semantics and remains excluded. |
| Existing page path is atomic before exposure | Source and deterministic oracle | REST/GraphQL page flow plus page-acceptance fixtures | Existing streams install decoded rows only after continuation and resource validation; delivery must preserve this while staging pagination mutation. |
| Credential identity remains stable | Source and concurrency oracle | RFC 0023 provider/snapshot fixtures | Existing provider resolution is once per stream; delivery adds multi-attempt evidence. |
| Package identity and compatibility remain coherent | Cross-release compilation/reload oracle | Compile identical v1 bytes under old and proposed compilers; compile package-major v2 opted-in bytes under the proposed compiler; compare source identity, normalized descriptor, explanation, fixtures, fresh load, and reload | Pending delivery; v1 bytes must remain one-attempt and v2 bytes must fail closed on an old extension. |
| Retry controller is bounded and deterministic | Exhaustive state-machine oracle | Scripted transport/status sequences, injected seed/waiter, exact counters, each of `502`/`503`/`504 + Retry-After`, and a 32-page/96-attempt boundary | Pending delivery; not decision-critical after the transport trial. |

The trial establishes decision evidence only. It did not change production
source, enable retry, add syntax, or contact an external service.

## Alternatives considered

### Retain one attempt

This is simplest and avoids amplification, but does not deliver the requested
transient recovery despite accepted replay/accounting/credential foundations.

### Retry every transport failure or `5xx`

This is smaller code but unsafe: it conflates pre-response failure with partial
response, protocol/TLS/policy failures, and permanent statuses. It was rejected.

### Make replay safety alone enable retry

This avoids new syntax but reinterprets identical v1 package bytes and identity,
contradicting RFC 0013's compatibility promise. It was rejected. The explicit
v2 retry block makes the behavior source-visible and fixture-testable without
changing the frozen v1 language. Public SQL or environment tuning can be a
later goal; the initial operator profile remains a host construction policy
that can only narrow author recommendation.

### Buffer the complete scan

Whole-scan atomicity would simplify replay after a later-page failure but would
increase memory, latency, and loss of streaming/backpressure. Page atomicity is
the narrow sufficient boundary and preserves accepted streaming behavior.

### Honor `Retry-After`

This mixes transport recovery with rate-limit policy and would contradict the
explicit `429` exclusion. It remains a separate goal.

## Drawbacks and failure modes

- An eligible transient failure can produce two additional requests and can
  transmit the same credential bytes again. Hard/operator/connector minima,
  exact destination policy, and one snapshot bound that amplification.
- Explicitly opted-in recovery adds bounded latency and can consume upstream
  quota.
- Mutable APIs can return different content on a repeated read. The contract
  promises no duplicate exposure by Runtime, not remote snapshot consistency.
- Typed partial-attempt accounting and staged pagination add Runtime state
  complexity. Independent failure-path and lifecycle review is required.
- A buggy retry classifier could violate correctness or security, so unknown
  transport facts and enum values are terminal rather than guessed.

## Acceptance and verification

- **End-to-end demonstration:** Compile an explicitly opted-in operation and
  execute `503 -> pre-response connection reset
  -> valid page`; return the same fixed duplicate-bearing rows as the
  failure-free fixture, with three attempts and cumulative delay visible in the
  stream diagnostic snapshot. Demonstrate post-exposure, partial-body, `429`,
  authorization, policy, decode, schema, cancellation, and deadline failures
  terminate without another request.
- **Automated oracle:** Operation/retry classification truth tables;
  attempt-versus-page accounting; page/cursor staging; deterministic jitter
  bounds; cancellation during backoff; exact attempt/wait/deadline exhaustion;
  one credential resolution; fresh destination checks; REST/GraphQL/single-page
  transcripts; stable terminal pulls; duplicate-bag and optimized/local SQL
  differentials; and cross-release v1-bytes/v2-opted-in-bytes identity,
  explanation, fixture, fresh-load, and reload compatibility.
- **Quality gates:** Every documentation/agent and product-source command in
  `AGENTS.md`, including a fresh native product build, source identity, RFC
  evidence identity, and community gates affected by the release surface.
- **Independent review:** Query experience, transport/policy, relational
  semantics, lifecycle/concurrency, and test-oracle perspectives; at least two
  fresh adversarial reviewers after implementation.
- **Interaction exit:** Final source and test dependencies demonstrate the
  Connector -> Semantics -> Runtime -> Query direction, with focused consumers
  using bounded provider APIs rather than provider internals.

## Contract propagation

| Source of truth or artifact | Impact | Required update | Completion evidence |
| --- | --- | --- | --- |
| `docs/ARCHITECTURE.md` | Affected | Replace one-attempt/exclusion text with safe-read retry, page commitment, snapshot, policy, and diagnostic laws | Pending delivery |
| `docs/CONNECTOR_SPECIFICATIONS.md` | Affected | Define the new exact `duckdb_api/v2` identifier, its closed per-operation retry recommendation, validation, defaults, examples, versioning, and compiled mapping while leaving v1 frozen | Pending delivery |
| `docs/RUNTIME_CONTRACTS.md` | Affected | Specify IR, plan, admission, retry controller, accounting, wait, diagnostics, and lifecycle | Pending delivery |
| `docs/TEAM_TOPOLOGY.md` and active charters | Not affected | Existing responsibilities and interfaces already place the work | This RFC's routing and final exit audit |
| `docs/PRODUCT_DELIVERY.md`, `AGENTS.md`, and skills | Not affected | Existing retry, product-delivery, contract-change, and review rules govern | No edit required |
| `ROADMAP.md`, schema, release notes, freeze, examples, diagnostics, fixtures, tests | Affected | Graduate explicitly recommended automatic retry from exclusion and bind the new closed vocabularies/evidence | Pending delivery |

## Unresolved questions

None decision-critical. Public operator tuning, server-directed delay,
idempotency mechanisms, and success-side SQL metrics remain explicit follow-on
product options rather than hidden implementation work.

## Review record

| Required reviewer | Team | Result | Evidence or objection | Disposition by decision owner |
| --- | --- | --- | --- | --- |
| `retry_query_review` | Query Experience | Approved after revision | Initial objection: `EXPLAIN` could not truthfully render admission-effective policy because Runtime intersects operator policy only at `Open`. The revision makes `EXPLAIN` render the labeled planned connector recommendation and makes the structured Runtime snapshot authoritative for admission-effective policy. | Objection accepted and incorporated; delivery exit remains open until Query consumes the structured service without Runtime policy knowledge. |
| `retry_runtime_review` | Remote Runtime | Approved after revision | Initial objections: aggregate scan-attempt authority was ambiguous and `Retry-After` handling conflicted with RFC 0021. The revision defines the checked page/attempt product capped at 96 and makes every `502`/`503`/`504` carrying `Retry-After` terminal. | Objections accepted and incorporated; delivery exit remains open until the shared retry service and REST/GraphQL consumers satisfy the state, policy, and lifecycle oracles. |
| `retry_semantics_review` | Relational Semantics | Approved after revision | Initial objection: per-step attempts did not determine aggregate plan authority. The revision defines single-response equality, checked paginated multiplication, the hard cap, overflow rejection, and forbids Runtime reconstruction. | Objection accepted and incorporated; delivery exit remains open until planner laws and the 32-page/96-attempt boundary are executable evidence. |
| `retry_connector_review` | Connector Experience | Approved after revision | Initial objection: adding syntax to frozen `duckdb_api/v1` violated RFC 0013. The revision introduces exact `duckdb_api/v2`, leaves v1 one-attempt and frozen, and requires separate schemas plus cross-version rejection and identity oracles. | Objection accepted and incorporated; delivery exit remains open until v1/v2 compilation, reload, explanation, fixture, and consumer-boundary evidence passes. |

## Decision and rationale

- **Technical decision owner:** Lead agent.
- **Product approval:** Nic Galluzzo approved the automatic safe-retry outcome,
  recovery demonstration, guardrails, and explicit exclusions in the
  supplied product brief on 2026-07-23.
- **Rationale:** The bounded transport trial proves the pinned dependency can
  distinguish no-response failure from partial response without message
  parsing. RFCs 0021 and 0023 already provide closed accounting, exposure,
  replay, and credential identity foundations. The accepted direction adds the
  smallest source-visible opt-in and Runtime state machine that can recover the
  required transcript while preserving frozen v1 identity, one immutable plan
  and credential snapshot, exact resource bounds, and terminal ambiguity.
- **Material objections:** Query's admission-versus-explanation boundary,
  Runtime's aggregate attempt and `Retry-After` rules, Semantics' plan algebra,
  and Connector's frozen-v1 identity objection were all incorporated as
  recorded above. No material objection remains unresolved.
- **Superseded by:** Not applicable.

## Follow-on goals

| Outcome or objective | Accountable team | Supporting teams and interaction modes | Activation condition |
| --- | --- | --- | --- |
| Deliver bounded replay-safe scan retry | Query Experience | Remote Runtime, Relational Semantics, and Connector Experience; Collaboration then X-as-a-Service with the exits above | RFC 0024 Accepted |

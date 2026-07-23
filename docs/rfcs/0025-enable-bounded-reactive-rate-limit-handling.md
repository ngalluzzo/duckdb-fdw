# RFC 0025: Enable bounded reactive rate-limit handling

```yaml
rfc: "0025"
title: "Enable bounded reactive rate-limit handling"
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
linked_outcome_or_objective: "For DuckDB users querying rate-limited APIs, support explicit, bounded, cancellable cooperation with remote quota signals without blocking unrelated connectors or principals or turning 429 into unbounded retry behavior."
supersedes: "none"
```

## Summary

Add an exact `duckdb_api/v3` package family in which a proved replayable read
may declare rate-limit statuses, bounded response-header interpretations, a
quota identity, and one of `fail`, `wait`, or `wait_if_deadline_allows`.
Runtime handles only observed rate-limit responses, reuses RFC 0024's
unaccepted-step replay loop and aggregate attempt ledger, and coordinates waits
through a bounded executor-local service keyed by exact destination, package,
operation family, credential authority, and an optional trusted remote bucket
value. Every wait is cancellable and debits both the one scan deadline and the
aggregate waiting budget. Remaining-capacity fields are validated only on
reactive rate-limit responses; they do not pace successful requests.

## Sponsorship and context

- **RFC type:** Product. This changes connector-package syntax, successful
  query behavior after a rate-limit response, explanation and terminal
  diagnostics, resource and attempt policy, credential-scoped coordination,
  cancellation, and the shared `ScanPlan` and `BatchStream` interfaces.
- **Sponsoring team:** Query Experience. Acceptance ends with a DuckDB user
  either recovering from a declared rate-limit response within explicit
  authority or receiving a precise bounded terminal diagnostic.
- **Linked outcome or objective:** The production-resilience rate-limit goal
  supplied by the product manager on 2026-07-23.
- **Why now:** RFC 0021 supplies failure, exposure, waiting, and aggregate
  resource facts; RFC 0023 supplies opaque authority identity and one immutable
  credential snapshot; RFC 0024 supplies the only safe attempt/replay loop.
  Without all three, rate-limit handling would need mutable credentials,
  mechanism-private counters, or another interpretation of replay safety.

## Problem

Runtime currently treats `429` and every response carrying `Retry-After` as a
terminal `rate_limit/server_directed_delay` failure. Transport retains only a
boolean saying that `Retry-After` was present. It cannot interpret a delay,
distinguish malformed guidance, preserve a remote bucket identity, coordinate
two scans, or wait and retry.

Blindly sleeping in the existing retry loop would be incorrect. A host name is
not a quota identity: RFC 6585 deliberately leaves user identification and
request counting to the server, including per-resource, per-server, or
multi-server scopes. A host-wide lock could therefore let one credential
principal block another. Conversely, a per-stream sleep would ignore a shared
remote bucket, create a thundering herd, retain no fairness or queue bound, and
leave shutdown behavior undefined. Parsing arbitrary response fields without
closed connector facts would also expose unbounded remote values to plans and
diagnostics.

## Decision drivers and invariants

- **Must preserve:** A repeat requires a compiled replayable read and an
  unaccepted/unexposed traversal step; indeterminate safety is terminal.
- **Must preserve:** One immutable plan and credential snapshot, with exact
  destination, placement, replay, byte, row, response, attempt, waiting, and
  deadline authority across every attempt.
- **Must preserve:** Ordinary bind and planning are deterministic and perform
  no network, credential, clock, or scheduler work.
- **Must enable:** Declared rate-limit failures either fail immediately or wait
  only within explicit connector, operator, Runtime, attempt, queue, waiting,
  and deadline authority.
- **Must enable:** Independent credential authorities and operation families do
  not share wait state unless the package explicitly declares shared principal
  scope.
- **Must enable:** Cancellation removes one queued waiter promptly, and
  Runtime close wakes and drains every queued waiter without another request.
- **Must not introduce:** Indefinite waits, early retry before the complete
  server-directed minimum, unbounded keys or queue entries, raw remote values in
  diagnostics, proactive pacing from successful-response remaining-capacity
  fields, distributed coordination, parallel pagination, caching, or general
  admission control.

## Proposed decision

### 1. Exact v3 author contract

`duckdb_api/v1` and `duckdb_api/v2` remain byte-for-byte frozen. V3 retains the
complete v2 grammar and permits an operation additionally to declare:

```yaml
rate_limit:
  statuses: [429]
  mode: wait_if_deadline_allows
  operation_family: core_requests
  principal_scope: credential_authority
  guidance:
    - header: retry-after
      format: retry_after
    - header: x-ratelimit-reset
      format: unix_seconds
  remaining:
    header: x-ratelimit-remaining
  remote_bucket:
    header: x-ratelimit-resource
  max_attempts_per_step: 3
  max_delay_milliseconds: 30000
  max_cumulative_waiting_milliseconds_per_scan: 30000
```

The block is accepted only for `replayable_read`. It contains no credential,
URL, SQL name, arbitrary parser, or executable expression.

- `rate_limit` is optional. Absence compiles to disabled. When it is present,
  `statuses`, `mode`, `operation_family`, and `principal_scope` are required;
  none has an implicit default.
- `statuses` contains one through eight unique plain YAML decimal integers in
  `400..599`, with exactly three digits and no sign, tag, separator, or leading
  zero. Authorization statuses `401` and `403` and proxy-authentication status
  `407` are rejected so a package cannot reclassify an accepted credential
  failure or retransmit credentials under quota policy. A matching declared
  status takes precedence over ordinary gateway retry. An unlisted status
  keeps RFC 0024's classification, including its rule that `502`/`503`/`504`
  carrying `Retry-After` are terminal.
- `mode` is exactly `fail`, `wait`, or `wait_if_deadline_allows`.
- `operation_family` is a lower-case stable identifier matching
  `[a-z][a-z0-9_]{0,63}`. Operations share quota state only when this local fact
  and every other key dimension agree.
- `principal_scope` is the required `credential_authority` or `shared` spelling.
  `credential_authority` always includes opaque credential authority;
  anonymous execution uses a distinct anonymous tag. `shared` deliberately
  removes that one dimension and is the only way two credential authorities
  can share a local quota key.
- Every declared response field name is 1 through 64 ASCII bytes, already
  lower-case, and contains only the HTTP token characters; uppercase,
  whitespace, colon, non-ASCII, and empty spellings fail. `date` is reserved
  for Runtime's absolute-time context. No canonical name may appear in more
  than one `guidance`, `remaining`, or `remote_bucket` role.
- `guidance` contains one through four declarations with unique canonical
  field names. A format is exactly `retry_after`,
  `delta_seconds`, or `unix_seconds`. `retry_after` implements RFC 9110 section
  10.2.3: non-negative decimal seconds or an HTTP date. There is no heuristic
  parser or unit inference.
- `remaining` optionally names one canonical field containing a non-negative
  unsigned decimal. Runtime observes it only on a matching rate-limit response
  and retains only `absent`, `zero`, or `nonzero`; the integer and successful
  response values are neither scheduling authority nor diagnostic content.
- `remote_bucket` optionally names one canonical field. A present value must,
  after optional whitespace removal, contain 1 through 128 visible ASCII bytes
  and no control. It can only narrow the already complete local key. It cannot
  erase destination, package, operation, or principal dimensions and is never
  rendered.
- `wait` and `wait_if_deadline_allows` require `guidance` and all three positive
  maxima; they permit optional `remaining` and `remote_bucket`. `fail` forbids
  `guidance`, `remaining`, `remote_bucket`, and all three maxima, performs no
  response-field parsing, and always reports `policy_fail` for a complete
  matching response.
  Attempts for a waiting mode are `2..3`; one delay and cumulative rate-limit
  waiting are each `1..30000` milliseconds.

Unknown, partial, duplicate, differently cased, or contradictory fields fail
schema or compilation. Adding or changing `rate_limit`, including changing
from `fail` to a waiting mode, requires a package-major version because it can
change requests, latency, failure behavior, and shared scheduling identity.

V3 reuses the exact `fixture-index-v1` schema. An author transcript remains the
base single-attempt request/response and may contain the declared bounded
response fields, but it cannot assert timer movement, queue order, transport
failure shape, or that another attempt occurred. Connector's typed coverage
derivation adds closed v3 policy keys for disabled/fail/wait modes, declared
statuses and formats, optional remaining/bucket roles, and safe explanation.
Those keys select project-owned variants that inject matching responses,
controlled wall/steady clocks, waits, and follow-up attempts ahead of or around
the validated base transcript. No fixture field supplies a clock or scheduler
hook. As in v2, the runner must prove derived keys equal claimed keys equal
actually executed production-path variants exactly; a listed but unexecuted
rate-limit key is uncovered.

### 2. Closed compiled and planned facts

Connector compiles the block into immutable closed values: the sorted status
set, mode, operation family, principal scope, ordered unique guidance fields
and formats, optional remaining and bucket fields, and exact maxima. Header
names are canonical structural facts; no header value is compiled or planned.

Semantics copies those facts field-for-field into `ScanPlan`, validates that
the selected operation remains `replayable_read`, and performs only checked
budget algebra. It does not parse HTTP, construct a quota key, consult a clock,
or decide whether a response is rate-limited.

For one step, planned attempt authority is the maximum of the enabled ordinary
retry and rate-limit maxima, not their sum. Each mechanism still applies its
own ordinal ceiling. The aggregate scan-attempt ceiling is the checked product
of maximum reachable pages and that combined per-step ceiling, capped by the
existing 96-attempt hard maximum. Thus ordinary retry followed by rate-limit
handling cannot obtain a second attempt pool.

The aggregate scan waiting ceiling is the checked sum of planned ordinary
retry and rate-limit cumulative maxima, capped by Runtime's 30000-millisecond
total-wait hard maximum. The ledger is still one authority. Mechanism-specific
counters prevent either kind of waiting from exceeding its own admitted
maximum even when aggregate authority remains.

Runtime admission intersects every planned rate-limit maximum with its hard
and construction-time operator profile. A zero operator ceiling disables
waiting; zero never means unlimited. Unknown status, mode, format, scope,
counter, or diagnostic values fail before provider resolution or transport.

### 3. Authority-isolated quota identity

Runtime builds a non-renderable `QuotaBucketKey` only after complete plan and
credential admission. It contains:

```text
exact scheme + host + explicit port
+ connector ID + package major
+ operation_family
+ credential authority | anonymous | explicit shared tag
+ exact trusted remote bucket value | no-remote-bucket tag
```

The credential dimension uses RFC 0023's opaque authority identity, never the
secret name, credential bytes, revision hash, or a renderable surrogate.
Replacement retains authority and therefore quota identity while the active
scan keeps its original revision snapshot. Drop and recreation mint a new
authority and therefore a separate key. Environment rotation retains its
authority and changes only its revision. `shared` is source-visible consent to
cross-authority coordination; it is not inferred from equal credential bytes,
host, or remote bucket text.

A remote bucket value is trusted only as a bounded sub-key because it arrived
from the already admitted exact destination. It cannot widen destination or
credential authority. Missing optional bucket metadata uses the local key;
malformed or duplicate bucket fields terminate without enqueueing. The first
complete matching response for one traversal step freezes either its exact
remote value or the no-remote-bucket tag for every later rate-limit response in
that step. A later value change, appearance, or disappearance produces
`bucket_changed`, releases any old-key permit only after the attempt reaches
terminal cleanup, and never transfers or re-enqueues across keys. A later page
is a new traversal step and may freeze another bucket.

### 4. Targeted response observation and guidance

Transport does not expose a general response-header map. For one admitted
request it retains only field-values named by the immutable rate-limit plan,
plus `Date` when an absolute interpretation needs it. Receipt order, duplicate
count, and retained bytes are bounded and charged to the existing response
metadata and header limits. Values remain Runtime-private and are discarded
after producing typed observations.

Rate-limit dispatch is possible only for a transport-successful, fully received
`HttpResponse`. A response that closes with partial headers or body remains the
terminal non-retryable transport failure required by RFC 0024 even when its
observed status and partial fields would otherwise match this policy. On a
matching status from a complete response:

1. every declared guidance field that is present exactly once is parsed under
   its declared format;
2. duplicate occurrences of one declared singleton, invalid decimal/date
   grammar, overflow, an invalid present `Date`, malformed remaining data, or
   malformed bucket data produce `malformed_guidance` and no wait;
3. at least one valid guidance value is required for a waiting mode;
4. multiple different declared guidance fields are combined by taking the
   latest implied eligible time, so conflicting minimums can never cause an
   earlier request;
5. a value beyond the effective one-delay maximum produces
   `guidance_exceeds_policy`; it is not clamped to an earlier retry; and
6. zero or past guidance permits one immediate repeat for the current step.
   A second consecutive immediate rate-limit response for that step produces
   `repeated_immediate` and no further request, even if an attempt remains.

After the first matching response, remote-bucket stability is checked before
guidance can extend an embargo or enqueue another ticket. `A -> B`,
`A -> absent`, and `absent -> A` within one step all fail as `bucket_changed`;
the old key is not extended and the new key is not created.

Delta values are converted to a steady-clock target at response receipt. For
absolute values, a valid response `Date` is the wall-clock reference; when
`Date` is absent, the injected local wall clock is captured once. The resulting
duration is converted immediately to the paired steady clock. Later wall-clock
movement cannot lengthen, shorten, or revive the wait. A past absolute value is
the zero-duration case above.

The current IETF RateLimit header work remains an active Internet-Draft as of
2026-05-23 and has changed field shape during its lifetime. V3 therefore does
not claim that draft as a compatibility surface. A connector may bind its
current wire format using the closed generic declarations above. Adopting a
future stable standard vocabulary or proactive successful-response pacing
requires another RFC.

### 5. One reactive attempt loop

RFC 0024's protocol-neutral attempt loop becomes the sole resilience loop; a
second rate-limit retry implementation is forbidden. For each response it
applies this order:

1. commit all observed attempt bytes and the attempt ordinal;
2. if the status matches the admitted rate-limit set, apply this RFC;
3. otherwise apply RFC 0024's ordinary retry matrix;
4. otherwise return the response for existing terminal status handling.

A waiting mode proceeds only when the current operation is replayable, the
current step remains unaccepted/unexposed, its mechanism-specific attempt
ceiling and the aggregate 96-attempt ledger permit another attempt, the full
request body can still be debited, and guidance is valid. The next attempt
rebuilds the exact request from the same plan and credential snapshot on a
fresh connection, repeats DNS resolution, and reapplies destination, redirect,
proxy, TLS, byte, and cancellation policy. A destination-policy failure on the
next attempt is terminal and cannot return to the quota queue.

`fail` returns the existing redacted remote-status error with structured
`policy_fail` rate-limit facts. `wait` enqueues for the complete eligible time
and remains interruptible by the one deadline or any budget. It does not retry
early. `wait_if_deadline_allows` additionally rejects before enqueueing when
the then-known eligible time cannot fit the remaining scan deadline and
rate-limit/aggregate waiting budgets. A later embargo extension by another
same-bucket response can still make a queued waiter reach its deadline; that
terminates rather than weakening the shared minimum.

### 6. Bounded reactive coordinator

Each concrete Runtime executor owns one `RateLimitCoordinator` shared by its
streams. The installed composition constructs exactly one executor for one
DuckDB `DatabaseInstance`, so this is the complete coordination domain.
Separately constructed executors, another `DatabaseInstance`, and another
process are deliberately independent operator domains. It has no worker thread
and no state outside its executor. A matching rate-limit response supplies a
key and steady eligible time; successful responses and remaining-capacity
fields never create a queue or pace an initial request.

The coordinator maintains a FIFO ticket queue and at most one move-only
in-flight permit per exact key. A later response can extend but never shorten
that key's current embargo. At eligibility only the queue head receives the
permit; later tickets cannot start transport merely because the same timer
expired. The permit is completed exactly once as success, another matching
rate-limit response, a different terminal result, cancellation, or destruction.
A matching rate-limit result extends the embargo, releases the permit, and
re-enqueues that stream at the tail only if its attempt and waiting policy still
authorizes another repeat and the step's frozen remote-bucket fact still
matches. Bucket drift is a terminal completion under the old permit and never
performs a cross-key transfer. Success or another terminal result releases the
permit and wakes the next head without inventing proactive pacing. A dropped
permit has the same non-throwing terminal release behavior. Thus observed
same-bucket recovery attempts are serialized without making a success a quota
guarantee; initial requests remain reactive and can run concurrently until they
observe a declared rate limit.

Distinct keys use independent condition state and never wait behind one
another. The hard limits are 64 queued-or-permitted waiters per key and 4096
across the executor; construction-time operator profiles may only narrow them.
Saturation fails the new waiter immediately with `queue_saturated`; it never
evicts another principal, allocates unbounded state, or blocks an unrelated
key. Empty, non-permitted key state is erased.

Waiters sleep in at most five-millisecond interruptible slices, debit the
rate-limit-specific and aggregate waiting ledgers, and observe the same steady
deadline used by transport and decode. Cancellation atomically removes a ticket
and wakes the next eligible waiter. Cancellation during an in-flight transport
does not release its permit early: transport first reaches terminal cleanup,
then the permit guard releases and wakes the next head. The executor
destructor calls `Close` before releasing its coordinator. `Close` marks the
coordinator closed, wakes every waiter, drains all queued tickets, invalidates
future permit/enqueue operations, and makes them fail with
`scheduler_closed`; it does not wait for or cancel an already executing
transport call, whose stream/host cancellation contract remains authoritative.
Streams retain the closed coordinator long enough to observe that result.
Close, cancellation, ticket/permit release, and destruction are non-throwing
and idempotent. No queue entry or permit retains a credential snapshot,
request, response, plan, body, URL, raw header map, or DuckDB object.

### 7. Diagnostics and explanation

`FailureClass::RATE_LIMIT` remains the primary class. An additive closed
`RateLimitReason` distinguishes:

```text
none | policy_fail | guidance_missing | malformed_guidance
| guidance_exceeds_policy | deadline_insufficient | waiting_exhausted
| attempts_exhausted | queue_saturated | scheduler_closed
| repeated_immediate | bucket_changed
```

`FailureProperties` and the content-free `ExecutionSnapshot` separately carry
ordinary retry delay, rate-limit waiting, cumulative remote transport time,
rate-limit event count, rate-limit wait count, the last reason, and whether the
stream is presently in a rate-limit wait. Existing primary failure, replay,
exposure, attempt, row, and terminating-budget facts remain authoritative.
Retry exhaustion remains an ordinary retry failure; a rate-limit attempt or
waiting ceiling remains a rate-limit failure with the corresponding reason.

Pre-execution `EXPLAIN` and Connector's safe compiled explanation render
`rate_limit=disabled` or every normalized behavioral fact: mode, sorted status
set, operation family, principal scope, ordered guidance field/format pairs,
optional remaining and remote-bucket field roles, and maxima. They are labeled
as planned rather than admission-effective. Runtime diagnostics carry the
effective minima and observed counters. No diagnostic or explanation contains
credential authority or revision, any received guidance/remaining/bucket
value, absolute timestamp, URL, body, row, cursor, or dependency message.

### Public behavior

Existing v1 and v2 packages retain their identities and terminal rate-limit
behavior. A package-major v3 generation may explicitly fail or recover a
proved read-only REST or GraphQL operation under the closed policy above. No
new SQL function, table-function argument, setting, environment input, or
automatic successful-response pacing is added. Missing or malformed guidance,
excessive delay, insufficient budgets, queue saturation, cancellation, close,
or unsafe replay fails with a redacted structured diagnostic and no
unauthorized request.

### Shared interfaces

- **Connector Experience:** v3 schema and compiler own the author declaration,
  closed field formats, stable operation family, principal scope, validation,
  normalized compatibility, explanation facts, and deterministic fixture path.
- **Relational Semantics:** `ScanPlan` carries exact copied policy and checked
  combined attempt/wait authority. It supplies no HTTP parsing, clock, key, or
  scheduling decision.
- **Remote Runtime:** targeted metadata capture, guidance parsing, opaque quota
  identity, admission, the shared resilience loop, bounded coordinator,
  accounting, cancellation, diagnostics, and shutdown are one reusable
  service.
- **Query Experience:** consumes the existing `BatchStream` pull/cancel/close
  boundary and renders plan/failure diagnostics without header, identity, or
  scheduler knowledge.

### Operational behavior

Rate-limit waits are opt-in source behavior and occur only after a matching
response. Every remote attempt remains sequential and policy-checked. Quota
coordination is in-memory and executor/DatabaseInstance-local, bounded by
explicit queue and time limits, and contains no durable or distributed state.
One credential
authority, connector package, or operation family cannot block another except
through an explicit `shared` principal scope and otherwise identical key.

## Topology impact

| Team | Role in this RFC | Interface or charter impact | Interaction mode | Exit condition |
| --- | --- | --- | --- | --- |
| Query Experience | Sponsor and diagnostics consumer | Rate-limited SQL outcome, cancellation, explanation, terminal rendering, and `BatchStream` diagnostic consumption | Collaboration, then X-as-a-Service | Ordinary SQL and controlled demonstrations fail or recover with the promised bounded facts and no adapter quota logic |
| Remote Runtime | Quota service provider | Typed observations, shared resilience loop, opaque-key coordinator, resource accounting, fairness, cancellation, and shutdown | Collaboration, then X-as-a-Service | One independently tested service covers REST and GraphQL; Query consumes it without Runtime internals |
| Relational Semantics | Immutable plan provider | Closed copied rate-limit facts plus combined attempt/wait budget algebra | X-as-a-Service | `ScanRequest -> ScanPlan` oracles fail closed and Runtime consumes the plan without reconstructing package syntax or relational meaning |
| Connector Experience | Declarative fact provider | V3 schema, validation, compiled policy, explanation, fixtures, and compatibility | Collaboration, then X-as-a-Service | V1/v2 identities remain frozen; v3 declarations compile and diagnose deterministically; consumers use only the bounded Connector API |

No accountability boundary or charter text changes. Query Experience remains
accountable because acceptance ends with a DuckDB query result or diagnostic.
The only added cognitive load is the bounded policy fact at Connector and
Semantics interfaces; transport, timing, credentials, queues, and lifecycle
remain behind Remote Runtime's service.

## Correctness, security, and lifecycle analysis

- **Relational semantics:** Predicate, residual, projection, ordering, limit,
  offset, and cardinality ownership do not change. A replay repeats only the
  exact current unaccepted step and never deduplicates source occurrences.
- **Authentication/network/privacy:** Opaque authority, not credential bytes or
  names, partitions the default key. Remote values only narrow a fully local
  key and never render. Every attempt repeats exact authentication and network
  admission with the same snapshot.
- **Resources/backpressure/cancellation:** Attempts, bytes, waits, remote time,
  queue entries, metadata, and key values are bounded. One page remains in
  flight; waits are pull-thread backpressure and are cancelable in bounded
  slices.
- **Replay/duplicates:** The one RFC 0024 attempt loop and atomic page boundary
  remain authoritative. A rate-limit response has installed and exposed no
  page, and the next attempt does not advance pagination state.
- **Concurrency/immutability:** Mutable embargo and FIFO state is executor-owned
  and synchronized; scan plans and credential snapshots remain immutable.
  Per-key isolation prevents head-of-line blocking between distinct keys.
- **FFI/lifecycle:** No new DuckDB callback or public ABI is added. Cancel,
  close, destruction, repeated terminal pull, and Runtime shutdown drain the
  coordinator without a background thread or escaped exception.
- **Diagnostics:** Only closed codes and counts cross the Runtime boundary.
  Guidance, remaining values, bucket values, identities, destinations, and
  timestamps remain private.

## Compatibility and migration

V1 remains frozen and one-attempt. V2 remains its retry-only additive family
and never waits on rate limits. V3 is a separate exact specification: it
retains v2 syntax and behavior when `rate_limit` is absent and adds only this
block. Existing bytes compile to the same identity and behavior; no existing
package migrates implicitly.

Every v1- or v2-to-v3 package migration requires a package-major increment even
when no operation declares `rate_limit`, because the exact package
specification identifier and source identity change. Adding, removing, or
changing a block within v3 also requires a package-major increment. Older
extensions reject v3 rather than ignoring its wait policy. A rollback publishes
a new package-major v2 generation without the block before rolling back the
extension. Active scans remain unaffected because each owns one immutable plan,
policy, credential, and coordinator ticket. Unknown policy facts fail closed
before side effects.

## Evidence and bounded trials

| Question or claim | Evidence required | Method or fixture | Result and limitations |
| --- | --- | --- | --- |
| `Retry-After` has a stable closed grammar | Primary protocol source | RFC 9110 section 10.2.3 and HTTP-date grammar | It is either HTTP-date or non-negative decimal delay-seconds; production still needs a bounded parser and mutation oracle |
| 429 does not define quota identity | Primary protocol source | RFC 6585 section 4 | Confirmed: identity/counting may be per credential, resource, server, or server set, motivating explicit local dimensions |
| General RateLimit fields are not yet a stable RFC surface | Primary working-group source | `draft-ietf-httpapi-ratelimit-headers-11`, 2026-05-23 | Active Internet-Draft; it must not become v3's implicit grammar or proactive contract |
| Opaque credential identity can partition state without secret retention | Source and identity laws | RFC 0023 plus replacement/drop/recreation/environment fixtures | Existing authority equality/hash is non-renderable; delivery must preserve it when authorization moves into a stream |
| The existing attempt loop is the safe replay unit | Source and deterministic retry oracles | RFC 0024 REST/GraphQL atomic-page, credential, retry, and destination tests | Existing loop supplies the boundary; delivery must generalize it rather than nest another loop |
| FIFO isolation, one in-flight recovery, cancellation, and close can be bounded without a worker thread | Deterministic concurrency oracle | Injected steady/wall clock and waiter; two keys, two authorities, shared scope, one-permit ordering, tail re-enqueue, saturation, cancellation races, executor destruction, and close transcripts | Delivered by the independently testable Runtime resilience service and its focused coordinator, controller, and production-executor oracles; the executor owns the service and closes it without a background thread |

## Alternatives considered

### Keep rate limits terminal

This preserves current simplicity and avoids wait coordination, but fails the
requested bounded cooperation even though replay, credential, and accounting
foundations now exist.

### Sleep independently in each stream

This would reuse the pull thread but cannot coordinate a declared shared
bucket, prove FIFO fairness, bound aggregate waiters, or drain shared state on
shutdown. It also tempts a second retry loop. Rejected.

### Key only by host or origin

This is compact but violates RFC 6585's deliberately unspecified user/resource
scope and the product guardrail against cross-principal interference. Rejected.

### Key by credential revision or secret name

Revision would fragment one principal across ordinary rotation; secret name is
Query-local, renderable, and not an authority fact; credential bytes would be
a severe privacy violation. Opaque authority identity is the accepted boundary.

### Clamp excessive guidance and retry early

Clamping a server's minimum to a smaller local delay would create extra load
before the server-authorized time. V3 instead treats the maximum as a trust and
latency boundary: excessive guidance fails without waiting.

### Adopt the current IETF RateLimit draft directly

This would improve apparent interoperability but would freeze an active draft,
couple reactive handling to proactive policy fields, and make later standards
changes a package migration. Closed connector-declared mappings are smaller and
remain compatible with current vendor fields.

### Use a background scheduling thread or distributed coordinator

A thread can centralize timers, but adds shutdown, fork, initialization, and
failure-containment obligations without improving the initial reactive FIFO
contract. Distributed state further adds cost and consistency policy. Both are
out of scope.

## Drawbacks and failure modes

- A waiting v3 operation can add up to two requests and 30 seconds of latency
  per scan. Connector, operator, Runtime, aggregate-attempt, and deadline
  minima bound that amplification.
- A malicious or defective admitted endpoint can create many distinct remote
  bucket values. Value length and total queue bounds make this a prompt
  `queue_saturated` failure rather than unbounded memory.
- `shared` principal scope can intentionally create cross-credential waiting.
  It is explicit package-major behavior and remains scoped by destination,
  package, operation family, and optional remote bucket.
- FIFO fairness does not promise service success or distributed fairness. The
  endpoint may still reject the next request; all attempts remain bounded.
- Absolute dates depend on one parsed server or local wall-clock sample. Pairing
  it immediately with the steady clock prevents later clock movement, but
  cannot repair a server that emits internally inconsistent dates; the latest
  valid minimum wins, and malformed data fails.
- Queue saturation can make a scan fail because other same-executor waiters
  consumed the hard memory allowance. It never makes the scan wait behind an
  unrelated key, and the diagnostic is explicit.

## Acceptance and verification

- **End-to-end demonstration:** Compile v3 fail and bounded-wait REST and
  GraphQL operations. Run controlled matching-status sequences with delta,
  absolute, malformed, duplicate, conflicting, excessive, past, and repeated
  immediate guidance. Show exact fail/recovery behavior and separately visible
  retry delay, rate-limit wait, remote time, attempt, and reason facts.
- **Automated oracle:** V1/v2/v3 schema and identity migration; status and
  format truth tables; header bounds and duplicates; absolute/date/skew and
  wall-clock movement; latest-minimum conflict law; immediate-repeat bound;
  combined attempt/wait algebra; credential replacement/drop/recreation;
  operation/principal/remote-bucket key isolation and frozen-step bucket drift;
  explicit shared scope; FIFO
  fairness with one in-flight permit and tail re-enqueue; per-key/global
  saturation; cancellation/ticket/permit races; multi-executor and
  multi-DatabaseInstance isolation; executor-destruction shutdown drain;
  complete-versus-partial response classification; stable repeated terminal
  pulls; fresh destination denial after wait; REST/GraphQL and single/paginated
  transcripts; `A -> B`, `A -> absent`, and `absent -> A` drift transcripts;
  optimized/local duplicate-bag differentials; exact derived,
  claimed, and executed v3 fixture coverage plus safe-explanation
  differentials.
- **Quality gates:** Every documentation/agent and product-source command in
  `AGENTS.md`, including a fresh native product build, source identities, RFC
  evidence identity, package compatibility, community installation, and
  community enablement where the release surface changes.
- **Independent review:** Query experience, connector compatibility,
  transport/policy, credentials, scheduling/fairness, resources,
  lifecycle/concurrency, relational replay, and test-oracle perspectives; at
  least two fresh adversarial reviewers after implementation.
- **Interaction exit:** Final source, target, and test dependencies show
  Connector -> Semantics -> Runtime -> Query direction. The coordinator and
  parser have independent Runtime oracles; Query and Connector consumers do
  not import Runtime-private scheduling or credential state.

## Contract propagation

| Source of truth or artifact | Impact | Required update | Completion evidence |
| --- | --- | --- | --- |
| `docs/ARCHITECTURE.md` | Affected | Add reactive quota identity, replay reuse, wait/resource/cancellation, and diagnostic laws; replace rate-limit-disabled exclusion | Delivered with the closed quota-key, replay, accounting, cancellation, and diagnostic invariants synchronized to implementation tests |
| `docs/CONNECTOR_SPECIFICATIONS.md` | Affected | Define exact v3 syntax, validation, examples, compatibility, defaults, and compiled mapping while leaving v1/v2 frozen | Delivered with exact v3 schema assets, compiler and migration mutations, deterministic coverage, and a complete example package |
| `docs/RUNTIME_CONTRACTS.md` | Affected | Specify compiled/plan facts, admission, targeted metadata, guidance parsing, coordinator, identity, accounting, diagnostics, and lifecycle | Delivered with focused parser/coordinator/accounting tests and REST, GraphQL, curl, cancellation, shutdown, and actual-DuckDB product oracles |
| `docs/TEAM_TOPOLOGY.md` and active charters | Not affected | Existing responsibilities and interfaces place the work | Final audit found the resilience provider behind a bounded Runtime target, Semantics behind `ScanPlan`, Connector behind compiled declarations, and Query consuming only the documented plan/stream interfaces |
| `docs/PRODUCT_DELIVERY.md`, `AGENTS.md`, and skills | Not affected | Existing product, contract-change, topology, and review practices govern | No edit required |
| `ROADMAP.md`, schemas, release/freeze inventory, examples, diagnostics, fixtures, and tests | Affected | Add the next production-resilience capability and bind every new closed vocabulary and compatibility surface | Delivered; public-surface, contract-freeze, source-identity, package fixture, native-product, and Community gates bind the resulting surface |

## Unresolved questions

None decision-critical. Proactive pacing from successful responses, a future
standard RateLimit grammar, distributed quota state, per-query/operator SQL
tuning, queue priorities, and public metrics exporters remain separate product
options rather than hidden implementation work.

## Review record

| Required reviewer | Team | Result | Evidence or objection | Disposition by decision owner |
| --- | --- | --- | --- | --- |
| `rate_limit_query_review` | Query Experience | Approved | The proposal keeps parsing, identity, clocks, and scheduling behind Runtime while preserving offline planning, bounded pull/cancel/close, planned-versus-effective explanation, redaction, and the ordinary SQL success/failure narrative. | No objection; delivery exit remains open until controlled REST/GraphQL, cancellation, shutdown, repeated-terminal, and source-dependency evidence passes. |
| `rate_limit_runtime_review` | Remote Runtime | Approved after revision | Initial objections identified unsafe partial-response dispatch, `403` reclassification, missing one-in-flight release semantics, and undefined coordinator cardinality/lifetime. A second pass identified remote-bucket drift. The accepted revision requires a complete response, preserves `401`/`403`/`407`, defines one permit with tail re-enqueue and executor/DatabaseInstance ownership, freezes the step bucket, makes drift terminal, and retains permits through attempt cleanup. | Objections accepted and incorporated; delivery exit remains open until complete/partial, one-permit FIFO, bucket-drift, credential-isolation, cancellation-race, multi-DatabaseInstance, and shutdown oracles pass. |
| `rate_limit_semantics_review` | Relational Semantics | Approved | Field-for-field immutable plan facts, maximum rather than additive attempt pools, checked page-product/96 cap, checked wait sum/30000 cap, and RFC 0024's unaccepted-step replay boundary preserve offline planning and occurrence-bag meaning without Runtime reclassification. | No objection; delivery exit remains open until planner, overflow, request-body-authority, and Runtime-consumption oracles pass. |
| `rate_limit_connector_review` | Connector Experience | Approved after revision | Initial objections identified ambiguous defaults/grammars/cross-role fields, fail-mode precedence, incomplete explanation, unspecified fixture clocks/variants, and ambiguous v2-to-v3 migration. The accepted revision closes every grammar and mode, renders all safe facts, retains the v1 fixture schema with project-owned variants and exact coverage equality, and requires a package major for every v1/v2-to-v3 migration and v3 policy change. | Objections accepted and incorporated; delivery exit remains open until v1/v2 preservation, v3 mutation/migration, compiled-IR, explanation, fixture-coverage, and consumer-boundary evidence passes. |

## Decision and rationale

- **Technical decision owner:** Lead agent.
- **Product approval:** Nic Galluzzo supplied and approved the bounded reactive
  rate-limit outcome, explicit wait/fail behavior, authority isolation,
  cancellation/resource guardrails, acceptance boundaries, and proactive
  pacing exclusion on 2026-07-23.
- **Rationale:** RFCs 0021, 0023, and 0024 already provide the complete
  failure/resource, opaque-authority, and atomic replay foundations. RFC 9110
  supplies stable `Retry-After` grammar while RFC 6585 confirms that quota
  identity cannot safely be inferred from a host. The accepted direction adds
  the smallest exact v3 declaration and executor-local reactive service that
  can honor bounded guidance without changing frozen v1/v2 behavior, parsing
  successful-response capacity, creating another replay loop, or sharing state
  across undeclared principals. Complete-response dispatch, one in-flight
  permit, step-frozen bucket identity, and one aggregate ledger close the
  correctness and lifecycle gaps found during review.
- **Material objections:** Remote Runtime's partial-response, authorization,
  permit, coordinator-lifetime, and bucket-drift objections and Connector
  Experience's grammar, explanation, fixture, and migration objections were
  accepted and incorporated as recorded above. No material objection remains.
- **Superseded by:** Not applicable.

## Follow-on goals

| Outcome or objective | Accountable team | Supporting teams and interaction modes | Activation condition |
| --- | --- | --- | --- |
| Deliver explicit bounded authority-isolated reactive rate-limit handling | Query Experience | Remote Runtime and Connector Experience — Collaboration then X-as-a-Service; Relational Semantics — X-as-a-Service | RFC 0025 Accepted with every required review disposition recorded |

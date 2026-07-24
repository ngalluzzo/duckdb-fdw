# RFC 0026: Bound Runtime admission and bulkhead isolation

```yaml
rfc: "0026"
title: "Bound Runtime admission and bulkhead isolation"
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
affected_teams:
  - "Query Experience"
  - "Remote Runtime"
linked_outcome_or_objective: "For operators running concurrent remote scans, prevent one slow, failing, or throttled connector, destination, or credential principal from exhausting shared Runtime workers, sockets, memory, queues, or waiting capacity."
supersedes: "none"
```

## Summary

Add one executor-local admission service that atomically bounds active scans,
in-flight transport attempts, retry and rate-limit waiting, buffered bytes, and
decoded rows across global, connector, destination, opaque-principal, and exact
bulkhead dimensions. Same-bulkhead work is FIFO; an ineligible saturated
bulkhead cannot head-of-line block an independently eligible key. Queueing is
finite, deadline-aware, cancellable, and drained by explicit executor close.
Local saturation and queue timeout acquire a new redacted `local_admission`
failure class and closed reason/scope facts; they are never classified as a
remote timeout or silently retried. This decision adds no connector syntax,
SQL setting, planning interpretation, distributed state, or circuit breaker.

## Sponsorship and context

- **RFC type:** Product. This changes successful and failing concurrent query
  behavior, the shared `ScanExecutor`/`BatchStream` diagnostic and lifecycle
  boundary, resource enforcement, retry/rate-limit composition, and shutdown.
- **Sponsoring team:** Query Experience. Acceptance ends with a DuckDB user
  receiving a bounded result or exact local-capacity diagnostic while an
  unrelated connector continues to make progress under sustained failure.
- **Linked outcome or objective:** The first of the two production-resilience
  outcomes approved by the product manager on 2026-07-23: bounded admission
  and bulkhead isolation. Circuit breaking is the separately approved
  follow-on after this admission model is proven.
- **Why now:** RFC 0021 supplies one scan ledger and failure taxonomy, RFC 0023
  supplies opaque credential authority, RFC 0024 supplies the sole replay-safe
  attempt loop, and RFC 0025 supplies bounded quota waiting. Those mechanisms
  still share no executor-wide capacity authority, so many individually valid
  scans can exhaust process-level workers, curl handles, response buffers, or
  waiting threads.

## Problem

Every current `ScanResourceAccounting` instance is scan-local. One stream is
bounded and sequential, but an arbitrary number of streams may concurrently
open, allocate their declared response/page buffers, enter retry delay, queue
for rate-limit recovery, and create curl easy handles. The v3
`RateLimitCoordinator` activates only for a matching quota policy and isolates
one exact quota key; it is not general request admission and cannot enforce
overlapping global, destination, and principal ceilings.

For example, 64 failing scans against destination A can each remain valid under
their own immutable plan and deadline while occupying DuckDB workers and
transport state. A healthy scan against destination B has no reserved or
independently admissible capacity. Treating each wait or allocation as only a
per-scan concern therefore satisfies local bounds while violating the
process-level outcome.

Independent nested semaphores are not sufficient. Acquiring global, then
destination, then principal authority can retain a scarce outer permit while
waiting for an inner one and can deadlock or starve another key. A single
author-chosen pool name is also unsafe: it permits unbounded key cardinality
and lets a connector widen or merge security/resource authority.

## Decision drivers and invariants

- **Must preserve:** Ordinary bind and planning remain deterministic and free
  of network, credential, clock, or admission-service work.
- **Must preserve:** Plans and credential snapshots remain immutable; one
  provider resolution occurs only after complete plan/profile admission and
  before transport.
- **Must preserve:** The scan ledger, one replay-safe attempt loop, sequential
  pagination, one page in flight, backpressure, and the no-reset rule remain
  authoritative.
- **Must enable:** One executor/DatabaseInstance has finite active-scan,
  request, waiting, buffered-byte, and buffered-row authority with global and
  isolation dimensions.
- **Must enable:** One connector, destination, or credential authority cannot
  consume all shared capacity or prevent an independently eligible key from
  progressing.
- **Must enable:** Queue saturation, queue timeout, cancellation, Runtime
  close, and buffer rejection are distinct stable local outcomes.
- **Must enable:** Cancellation removes a queued entry promptly and every
  permit/reservation releases exactly once on failure, close, or destruction.
- **Must not introduce:** Connector-authored pool names, new package syntax,
  `ScanPlan` policy, SQL configuration, secret/identity rendering, unbounded
  maps/tickets, a second retry loop, automatic retry after local rejection,
  distributed coordination, caching, single-flight, parallel pagination, or
  circuit state.

## Proposed decision

### 1. Existing facts form one closed admission identity

Runtime constructs identity only after complete protocol/profile admission.
The identity uses facts already present in the immutable plan or provider
snapshot:

```text
connector   = exact connector ID
operation   = admitted connector relation ID + selected protocol + admitted operation ID
destination = exact scheme + DNS host + explicit port
principal   = anonymous | provider-minted opaque credential authority |
              conservative direct-capability tag
bulkhead    = connector + operation + destination + principal
```

The connector-wide dimension deliberately excludes package version, relation,
and selected operation, so one connector cannot evade its ceiling by publishing
or retaining multiple generations, relations, or operations. The operation
component remains only in the exact bulkhead and uses the plan's existing
validated bounded connector relation ID, protocol enum, and relation-local
package-authored operation ID. Relation ID is required because operation IDs
are unique only inside one relation. Package version is deliberately absent:
successive or concurrently retained generations of the same
connector/relation/protocol/operation share one host bulkhead instead of
creating cardinality through release churn. These are existing plan facts, not
a new opaque/Semantics value or author pool declaration. Connector,
destination, principal, queue, and global ceilings remain authoritative even
if an author uses every permitted relation and operation ID. Credential
revision, secret name/value, environment variable, response value, remote
bucket, URL query, generated SQL name, generation source, and arbitrary
connector-authored pool name are absent.

Provider-backed execution uses `CredentialAuthorityIdentity` equality and
hashing through a non-renderable Runtime adapter. Replacement retains the same
principal pool; drop and recreation mint a different authority. The standalone
principal counter applies only to provider-backed opaque authorities. Anonymous
and compatibility-only direct-authorization execution use explicit tags only
inside their complete exact bulkhead; they do not consume an executor-global
principal counter that could couple unrelated connectors or destinations. The
installed authenticated Query path always uses the provider-backed form.

Maps compare exact values after hashing, so a collision cannot merge
identities. A controller entry exists only while a permit, reservation, or
queue entry retains it; exact-key state is erased when all counts reach zero.
The global queue and active limits therefore bound identity cardinality without
an eviction policy.

### 2. One atomic multi-dimensional admission service

Each concrete HTTP executor owns one `AdmissionController` alongside its
`RateLimitCoordinator`. Both are shared by that executor's streams and by no
other executor, DatabaseInstance, or process. The admission controller owns no
worker thread, request, response, plan, credential bytes, authorization,
DuckDB object, or wall-clock time.

The installed hard profile is:

| Authority | Global | Per connector | Per destination | Per principal | Per exact bulkhead |
| --- | ---: | ---: | ---: | ---: | ---: |
| Credential resolutions | 16 | 8 | 8 | — | — |
| Queued credential resolutions | 64 | 16 | 16 | — | — |
| Active scans | 64 | 16 | 16 | 8 | 4 |
| In-flight requests | 32 | 8 | 8 | 4 | 2 |
| Queued scan admissions | 256 | 64 | 64 | 32 | 16 |
| Queued request admissions | 256 | 64 | 64 | 32 | 16 |
| Ordinary retry waiters | 32 | 16 | 16 | 8 | 4 |
| Rate-limit waiters | 32 | 16 | 16 | 8 | 4 |
| Buffered bytes | 256 MiB | 128 MiB | 128 MiB | 64 MiB | 32 MiB |
| Buffered decoded rows | 6,400 | 3,200 | 3,200 | 1,600 | 800 |

Construction-time test/operator profiles may narrow any value. Zero disables
that class; it never means unlimited. A profile cannot exceed these hard
limits. No SQL setting, environment variable, package field, or per-scan input
widens or mutates the profile.

Credential-resolution, scan, request, waiting, byte, and row admission each
submit one complete applicable dimension vector under one mutex. The connector
counter uses only exact connector ID; the destination counter uses only exact
destination; the principal counter is present only for a provider-backed
opaque authority; and the exact-bulkhead counter uses the complete
connector/operation/destination/principal-tag tuple. A ticket or reservation is
granted only when every applicable global, connector, destination, principal,
and bulkhead counter fits, then all counters are incremented atomically. No
caller holds a partial dimension while waiting for another. A move-only permit
or reservation is the sole release authority.

Provider-resolution, scan, and request queues have independent ticket domains
because their capacity classes do not substitute for one another. Within one
exact bulkhead, scan/request tickets are FIFO; provider tickets are FIFO within
one connector/destination pair. For each queue class, on capacity release the
scheduler examines tickets in ordinal order and grants the oldest ticket whose
complete applicable vector fits. An older ticket blocked by its connector,
destination, principal, or exact-bulkhead ceiling does not block a younger
independently eligible ticket. Because each ticket requests one unit and every
dimension has a positive fixed ceiling, capacity release plus bounded queue
residency prevents starvation among continuously eligible tickets. Provider
and scan/request saturation, eligible bypass, repeated-arrival starvation,
cancellation, and timeout oracles bind the same law. Ticket increment uses
checked arithmetic; exhaustion closes admission with a stable local failure
rather than wrapping.

### 3. Exact admission order

`ScanExecutor::Open*` performs:

1. cancellation check;
2. complete immutable plan/profile admission;
3. for authenticated execution, acquire one bounded provider-resolution permit
   using the already admitted global/connector/destination facts;
4. provider resolution, followed by unconditional release of that permit;
5. construction of the complete closed admission identity;
6. atomic principal-aware active-scan admission; and
7. stream construction with the move-only scan permit.

Provider resolution remains after plan admission and before transport. It may
precede principal-aware scan admission because opaque principal identity does
not exist earlier, but it never precedes shared Runtime admission altogether.
Provider-resolution admission queues at most 64 calls globally, 16 per
connector, and 16 per destination and permits at most 16/8/8 respectively.
It retains no credential and releases before scan admission, so no caller holds
partial active-scan authority while waiting for a principal-aware vector. A
resolved snapshot retained by the later scan queue remains bounded by every
scan-queue dimension and is released on timeout, cancellation, close, or
construction failure.

Provider-resolution and scan-admission queues each wait at most 1,000
milliseconds in five-millisecond cancellation slices. They run before the scan
wall deadline starts and therefore do not consume remote execution time. Queue
saturation fails immediately.

For every initial request, ordinary retry, or rate-limit repeat, the sole
resilience loop performs:

1. obtain any v3 exact quota permit and wait until eligible;
2. acquire the general request permit without constructing a credentialed
   request;
3. reserve the declared worst-case co-live request/response buffer authority;
4. begin and debit the transport-attempt ordinal;
5. construct/authorize the exact request and execute transport;
6. release in-flight authority when transport completes or terminates;
7. retain only the buffer reservation needed by the complete response/decode
   state; and
8. release every remaining reservation with page-handoff or terminal cleanup.

When a completed response selects an ordinary retry or a rate-limit repeat,
Runtime first copies only the already bounded retry/quota decision facts and
safe terminal facts, then destroys the complete response and releases its byte
reservation. Only after the charged response-byte count is zero may it acquire
the retry/rate waiter reservation or enter `RateLimitCoordinator`. The current
implementation's response variable spanning recovery waiting is therefore a
required refactor, not an allowed ownership pattern. A focused counter oracle
observes zero charged response bytes throughout both kinds of wait.

Before the first quota/request wait, `ScanResourceAccounting` starts one
immutable deadline and a pending traversal-step ordinal through a split
`BeginStep`; it increments neither pages nor attempts. The first
`BeginAttempt` after request and buffer admission atomically commits that one
page and the first transport attempt. A retry for the same step increments only
attempts; it can never increment pages again. A locally rejected pending step
aborts without a page or attempt increment and remains terminal rather than
replayable. `CommitDecodedPage` retains its existing record/memory and
acceptance duties. Boundary, retry, cancellation, and local-rejection oracles
prove one page per remotely attempted step and zero pages/attempts when no
transport authority was granted.

The request queue waits for the minimum of 1,000 milliseconds, the remaining
5,000-millisecond aggregate admission-wait allowance, and the scan deadline,
checking cancellation every five milliseconds. Queue time debits elapsed scan
time and a new cumulative admission-wait counter, never ordinary retry or
rate-limit waiting. An attempt is counted only after request and buffer
admission succeed; locally rejected work never consumes replay authority or
enters retry classification.

Queue outcomes are linearized, not inferred from which sleeping caller wakes
first. Controller close, controller ticket exhaustion, and a capacity grant
linearize under the controller mutex. On each pre-grant evaluation, the fixed
cause order is: an already-linearized controller terminal cause; observed
cancellation; exhausted scan wall deadline; exhausted aggregate admission-wait
allowance; exhausted per-queue residence; then a capacity grant. Provider and
scan queues, which precede the scan wall deadline, omit the two scan-budget
entries. A wall-deadline outcome remains the existing terminating wall-budget
failure, not `local_admission`; aggregate and residence outcomes are
`admission_waiting_exhausted` and the applicable `*_queue_timeout` respectively.
If capacity was released after a deadline but before the waiter reacquires the
mutex, the deadline still wins and no permit is granted. If a grant already
linearized, the caller owns the permit; a post-grant cancellation check releases
it before provider, request construction, allocation, or transport and returns
the existing cancellation outcome. Close/cancel/grant and coincident-deadline
oracles bind these rules.

Holding an eligible exact quota permit while waiting for general request
capacity preserves same-quota FIFO and one-in-flight semantics without holding
a general request slot during a remote embargo. Timeout or cancellation
releases the quota permit before returning the local diagnostic.

### 4. Recovery waiting is separately finite and fail-fast

Before ordinary retry delay or rate-limit coordinator queueing, the resilience
loop atomically acquires the corresponding complete global, connector,
destination, provider-principal, and exact-bulkhead waiting vector. Waiting
reservations do not queue: saturation fails immediately as
`retry_wait_saturated` or `rate_limit_wait_saturated`. This prevents a retry or
throttling storm from turning secondary recovery into another retained-worker
queue.

The reservation remains live only for the delay/queue interval. It releases
before request admission for the next attempt. Existing retry/rate-limit
attempt and cumulative-wait ceilings still apply; admission is an additional
host minimum and grants no replay, waiting, deadline, or remote authority.

Before either reservation is acquired, Runtime extracts the bounded recovery
decision and destroys the complete response as specified above. No request or
response byte reservation is charged while an ordinary retry delay or
rate-limit queue wait is live.

V3's coordinator retains its exact quota key and one-permit/FIFO rules. The
new bulkhead key deliberately omits the optional remote bucket: untrusted
response values may narrow quota coordination but cannot create a new general
capacity pool. The admission waiter limits reduce the effective coordinator
population below its existing hard 64-per-key/4,096-executor storage ceiling.

### 5. Buffered bytes and rows use reservations, not observation after allocation

Before transport, Runtime reserves the checked worst-case co-live envelope for
the admitted attempt:

```text
materialized request target, headers, credential placement, and body capacity
+ retained response-header and targeted metadata capacity
+ raw wire/chunk-framed response capacity
+ simultaneously retained dechunked/decompressed response capacity
```

Request capacity is derived from admitted structural request bounds plus the
existing hard credential-placement ceiling without inspecting credential
bytes. Response terms use the distinct admitted header, wire-response, and
decompressed-response maxima; no two co-live buffers are collapsed into one
logical length. Before decode, Runtime additionally reserves the admitted
decoded-memory and decoded-row ceilings while the complete response remains
live. Every reservation atomically checks all applicable global, connector,
destination, provider-principal, and exact-bulkhead dimensions.

Buffer admission is fail-fast. Runtime never waits while retaining a response,
request permit, decoded page, or credentialed request. A reservation may be
narrowed to actual retained capacity only after the smaller value is known;
it can never grow past the admitted maximum without a new atomic reservation.
Runtime buffer implementations allocate inside that capacity: raw/dechunked
response and generated-request builders use bounded fixed-capacity storage or
reconcile a proven pinned-allocator growth envelope before allocation. A
post-allocation logical-size check is not admission evidence. Standard-
container payload capacity and retained metadata capacity are charged;
allocator bookkeeping outside the payload is bounded separately by the active
object/count ceilings. The move-only reservation releases on successful page
drain, failure, cancellation, close, and destruction.

The byte total is checked before arithmetic or allocation. A single admitted
page whose declared maximum exceeds any connector, destination, principal,
bulkhead, or global host ceiling fails before transport as
`buffered_bytes_exhausted`; a connector declaration therefore cannot force an
oversized allocation merely because its per-scan envelope is otherwise valid.
Decoded-row reservation likewise precedes the decoder's row allocation.
Controlled chunked-response and GraphQL request-body oracles exercise maximum,
one-over, simultaneous raw/decoded retention, and allocated-capacity—not merely
logical-size—accounting.

The stream retains the complete decoded-page byte/row reservation after `Next`
returns a `TypedBatch` and across every later batch produced from that same
page. Moving rows out of the decoded-page vector does not narrow or release
that conservative page charge. Before each returned batch, Runtime also
reserves the distinct handoff capacity needed for the destination row/column
containers and any payload not already covered by the still-live page charge.
Query consumes that batch synchronously into its DuckDB `DataChunk`; the next
`Next` call proves the prior chunk was consumed, so its first action releases
only the prior batch-handoff charge. The decoded-page reservation releases only
after the page is fully drained, immediately before a later page reservation,
or on clean exhaustion, terminal failure, cancellation, close, or stream
destruction. At a continuing GraphQL boundary, the page-byte reservation
atomically narrows to exact retained cursor capacity and grows before the next
decode; Link state retains no dynamic continuation collection after exact
transition validation. This conservative handoff closes the interval between
Runtime's return and Query's copy without adding a Runtime-private lease to
`TypedBatch` or teaching Query about admission state. A multi-batch
REST/GraphQL page oracle
observes the page and handoff byte/row counters after each pull, immediately
before page drain, and before the next page allocation.

### 6. Closed local diagnostics

The primary failure taxonomy adds `local_admission`. Its stage is `resource`
at the coarse Query prefix and its phase is `admit` for scan admission or
`request` for request/wait/buffer admission. It never uses the reserved remote
`timeout` class. `terminating_budget` remains `none` because these are shared
executor ceilings rather than the immutable scan-plan budget.

`AdmissionReason` is closed at:

```text
none
credential_resolution_queue_saturated
credential_resolution_queue_timeout
scan_queue_saturated
scan_queue_timeout
request_queue_saturated
request_queue_timeout
admission_waiting_exhausted
retry_wait_saturated
rate_limit_wait_saturated
buffered_bytes_exhausted
buffered_rows_exhausted
runtime_closed
ticket_exhausted
```

`AdmissionScope` is closed at `none`, `global`, `connector`, `destination`,
`principal`, and `bulkhead`. When more than one applicable dimension is
simultaneously unavailable, every grant-ineligibility, queue-saturation,
wait-reservation, and byte/row check selects the first failing scope in the
fixed order `global -> connector -> destination -> principal -> bulkhead`;
anonymous/direct vectors skip `principal`. Container order and hash layout are
never diagnostic authority. Boundary, simultaneous-failure, and mutation
oracles bind this precedence.

Terminal properties and the best-effort execution snapshot carry only reason,
scope, effective limit, observed checked count before the request, requested
unit/byte/row count, cumulative admission-wait milliseconds, and current
admission-wait flag. A failure means `observed + requested > limit` under
checked arithmetic. Timeout recomputes those facts after every prior grant at
the terminal instant. If the originally blocking capacity became available
after its deadline, the timeout retains the last scope/limit/observed/requested
vector that made the ticket ineligible; it does not turn a deadline into a
grant or fabricate a now-failing scope. A timeout caused only by queue
residence while its vector was eligible uses `scope=none`, zero limit/observed,
and the requested unit. Terminal controller causes also use `scope=none` and
zero counts. No admission identity component, hash, destination, operation, URL,
credential, queue key, ticket, or timestamp is newly rendered. Query preserves
its existing independently approved safe connector/relation prefix and appends
only the closed admission facts.

Repeated pulls preserve the same terminal properties. A scan-admission failure
occurs before a stream exists and crosses the same one-time Query translation
boundary. Query never inspects scheduler state or reconstructs a scope.

### 7. Explicit close drains every queue

`ScanExecutor` gains an idempotent non-throwing `Close() const noexcept`. Its
compatibility default is a no-op so existing immutable `shared_ptr<const
ScanExecutor>` consumers remain source-compatible. The HTTP executor atomically
marks the Runtime closing, closes provider/scan/request admission, wakes every
queued caller as `runtime_closed`, closes rate-limit coordination, and rejects
future admission without provider or transport work. Destruction calls
`Close`.

The executor, every HTTP stream, and every move-only permit, reservation, and
queued handle retain the controller's reference-counted shared state rather
than a raw controller/executor pointer. Executor destruction first calls
`Close` and then releases only its own reference. A live stream or outstanding
handle therefore observes the terminal state and can release exactly once
without dereferencing a destroyed owner. Executor-destruction-with-live-stream,
queued-handle, permit, and reservation oracles bind this lifetime law.

If close wakes a stream already inside `RateLimitCoordinator`, the terminal
primary class is normalized to `local_admission` with
`admission_reason=runtime_closed`; additive
`rate_limit_reason=scheduler_closed` may record where wakeup was observed but
does not replace the executor-close cause. A close-versus-quota-wait race oracle
binds this precedence.

Every admission ticket ordinal uses checked increment. Before wrap, the
controller atomically enters terminal `ticket_exhausted`, wakes all existing
provider/scan/request queues with that same cause, rejects every future
acquisition likewise, and leaves outstanding permits release-only. Later
`Close` does not rewrite the first terminal cause. RFC 0026 also makes the
existing rate-limit coordinator ordinal checked; its independent exhaustion
wakes queued/future quota callers with additive
`rate_limit_reason=ticket_exhausted` rather than `scheduler_closed`. Injected
near-maximum oracles cover queued, future, release, and close races.

Query's DatabaseInstance lifecycle sentry closes Query publication admission,
then Runtime generation admission, then the shared executor. DuckDB 1.5.4
retains the DatabaseInstance through active queries, so the actual lifecycle
callback runs only after those queries are quiescent; it is an ordered final
close, not an active-query interruption hook. Focused Runtime tests invoke
explicit executor close while calls are queued to prove wakeup/drain behavior.
Close does not wait for an in-flight transport or destroy a live stream.
Existing permits and reservations remain valid only for release; a live stream
cannot acquire new request or wait authority after close. Active Query
cancellation/connection teardown continues to own transport interruption
through the existing stream boundary.

An active-scan permit releases as soon as `BatchStream::Next` returns clean
exhaustion or a terminal failure, not only when the stream object is later
closed or destroyed. Early `Close`, cancellation, construction failure, and
destruction release through the same idempotent path. Retaining an exhausted
stream can therefore never retain scan capacity.

### Public behavior

Existing v1/v2/v3 connector bytes, compiled identities, plans, SQL functions,
arguments, and successful single-scan results remain unchanged. Concurrent
scans now queue or fail within fixed local ceilings. Local saturation and queue
timeout produce the additive redacted facts above; no remote request is made
for rejected work and no retry consumes the rejection.

There is no new setting or author opt-in. Resource isolation is a host safety
floor applied to every supported package generation. A later public operator
configuration surface would require its own product decision and RFC.

Circuit breaking, failure history, open/half-open state, and suppression based
on a prior scan are explicitly excluded and remain disabled until the approved
follow-on goal is separately governed.

### Shared interfaces

- **Remote Runtime:** provides one executor-owned admission service, closed
  identity adapter, move-only scan/request/wait/buffer authority, explicit
  executor close, structured diagnostic facts, and deterministic fixtures.
- **Query Experience:** consumes `ScanExecutor::Close`, the existing
  `Open*`/`BatchStream` boundary, and additive redacted diagnostics. It has no
  identity, queue, scheduling, or policy logic.
- **Connector Experience:** not affected. Existing bounded connector/operation
  provenance is consumed after compilation; syntax, validation, compiled IR,
  examples, and compatibility stay byte-for-byte unchanged.
- **Relational Semantics:** not affected. Runtime consumes existing immutable
  operation/destination/resource facts; `ScanRequest`, `ScanPlan`, explanation,
  and relational meaning do not change.

### Operational behavior

All state is in-memory and executor-local. The controller has one mutex and no
worker. Queue entries, identity maps, tickets, permits, waiters, bytes, and rows
are bounded. Queueing supplies pull-thread backpressure and bounded
cancellation checkpoints. No background cleanup, durable state, distributed
coordination, or process-global admission is claimed.

DuckDB 1.5.4 invokes this synchronous single-threaded table source on each
connection's invoking thread; the adapter does not submit a blocking `Open` or
`Next` callback to DuckDB's shared execution-worker pool. The controlled RFC
trial configured `threads=1`, held sixteen real adapter callbacks in `Next`,
and still completed an authenticated relation on a seventeenth connection
within two seconds before canceling and releasing every slow stream. Admission
queues therefore retain only their already-invoking connection threads, never
an AdmissionController or DuckDB worker. The controller adds no worker and
cannot govern an embedding application's decision to withhold the distinct
caller needed to invoke an unrelated query; the product progress law begins
when concurrent calls enter DuckDB, as every multi-connection acceptance
fixture does.

## Topology impact

| Team | Role in this RFC | Interface or charter impact | Interaction mode | Exit condition |
| --- | --- | --- | --- | --- |
| Query Experience | Sponsor and lifecycle/diagnostic consumer | Concurrent SQL outcome, scan initialization, cancellation, close ordering, and rendering of additive safe facts | Collaboration, then X-as-a-Service | Controlled DuckDB scans prove healthy-key progress and stable local failures while Query consumes only documented Runtime interfaces |
| Remote Runtime | Admission service provider | Identity, atomic multi-dimensional scheduling, resource reservations, retry/rate-limit composition, accounting, cancellation, and shutdown | Collaboration, then X-as-a-Service | One independently tested service covers REST and GraphQL and Query imports no Runtime-private scheduler or identity state |

No accountability boundary or charter text changes. Connector Experience and
Relational Semantics are not affected because no author syntax, compiled fact,
planning policy, or semantic interpretation changes. Remote concurrency and
resource cognitive load remain behind the Runtime service.

## Correctness, security, and lifecycle analysis

- **Relational semantics:** Predicate, residual, projection, ordering, limit,
  offset, occurrence cardinality, and conservative fallback are unchanged.
  Admission occurs after a complete plan and cannot select another operation.
- **Authentication/network/privacy:** Credential resolution retains its
  admission-before-provider and snapshot-once laws. Opaque authority partitions
  principal capacity; bytes, names, revisions, and hashes never render. Every
  admitted attempt still rechecks exact destination policy.
- **Resources/backpressure/cancellation:** Every count and byte addition is
  checked before mutation. Atomic dimension acquisition avoids hold-and-wait.
  Queue entries and waits are finite and canceled in five-millisecond slices.
- **Ownership/lifetime:** Controller state is reference-counted by all live
  handles. Clean exhaustion, terminal failure, early close, cancellation, and
  destruction release exactly once; executor destruction closes but cannot
  invalidate a later release.
- **Replay/duplicates:** Local rejection occurs before `BeginAttempt` and is
  never retryable. Existing unaccepted-step and exposure boundaries remain the
  only replay authority.
- **Concurrency/immutability:** One mutex linearizes grants/releases/close;
  move-only handles release once. Immutable plans and snapshots are referenced
  only long enough to construct closed keys and requests.
- **FFI/lifecycle:** No new DuckDB callback or ABI promise. Query invokes the
  new compatibility-default `Close` from its existing DatabaseInstance sentry.
  Queued calls wake; active streams release without a blocking destructor.
- **Diagnostics/redaction:** Only closed codes and counts cross the public
  boundary. A local queue timeout is never remote `timeout`; remote failures do
  not mutate admission classification.

## Compatibility and migration

No package, SQL, secret, or stored-data migration is required. V1 remains
one-attempt, v2 retry-only, and v3 reactive-rate-limit capable under the same
exact schemas. The project extension adds a safety policy to existing
concurrent execution; a workload that previously exceeded shared capacity may
now queue for at most the declared local interval or receive
`local_admission`.

Private Runtime construction profiles gain additive admission ceilings and may
narrow defaults. Existing test executors inherit the no-op `ScanExecutor::Close`
and remain source-compatible. Unknown reason/scope values fail closed at the
name/render boundary. Rollback removes local isolation and is therefore not a
safe overload mitigation; operators must first drain work to the earlier
release's proven capacity.

## Evidence and bounded trials

| Question or claim | Evidence required | Method or fixture | Result and limitations |
| --- | --- | --- | --- |
| Existing facts suffice without Connector or Semantics change | Source trace from plan/provider through Runtime | Inspect `ScanPlan`, admitted REST/GraphQL profiles, `CredentialAuthorityIdentity`, and the v3 quota-key adapter | Confirmed: connector/package/operation, exact origin, resource envelopes, and opaque authority already reach Runtime after complete admission; direct authorization needs the conservative tag recorded above |
| Current accounting is only scan-local | Source and concurrency counterexample | Inspect `ScanResourceAccounting`, `HttpScanExecutor`, and curl transport construction; open controlled streams concurrently | Confirmed by source: every stream owns its own ledger and every attempt may construct a curl handle; only v3 same-quota work has shared coordination |
| Synchronous adapter waits do not exhaust DuckDB's shared execution workers | Pinned finite-worker DuckDB trial | Build and run `duckdb_api_rfc0026_worker_isolation_trial` with DuckDB 1.5.4, `threads=1`, sixteen blocking anonymous adapter scans, and one authenticated healthy scan; source SHA-256 `6afaa60c4d848324a1e816594c15fd00a882086b01da25ac6e4bd6a65ee62148` | Passed 2026-07-23: the healthy scan completed inside the two-second bound; all sixteen slow scans then canceled and all seventeen streams released exactly once. The adapter retains invoking connection threads, not a shared DuckDB worker; embedding-application caller scheduling remains outside Runtime authority |
| Atomic eligible-ticket scheduling avoids cross-key head-of-line blocking | Deterministic scheduler transcript | Injected steady clock/control; saturate A's exact/destination/principal dimensions, queue A2 then independently eligible B, release and compare grant order | Passed: focused controller oracles bind exact-key FIFO, eligible-key bypass, repeated-arrival starvation resistance, every simultaneous scope precedence, and exact equality despite forced hash collision |
| Permit ordering composes with replay and v3 quota authority | Deterministic resilience transcript | Initial, ordinary retry, and rate-limit repeat with blocked request capacity, cancellation, coincident deadlines, close, response-byte counters, and attempt-count observations | Passed: request/buffer authority precedes `BeginAttempt`; local rejection leaves page/attempt counts at zero; response charge is zero during both recovery waits; close during quota wait preserves `runtime_closed` as the primary cause |
| Shared buffer reservations precede allocation | Boundary and one-over fixture | Maximum/one-over planned response, decoded memory, and row reservations with two bulkheads; multi-batch REST/GraphQL pages inspect page and handoff counters between pulls, at page drain, and before the next page | Passed: request, GraphQL body, curl body/retained metadata, chunk/decompressed, decoded-page, row, cursor-transfer, and batch-handoff capacity is charged before growth; REST/GraphQL multi-batch oracles retain page authority until drain; actual DuckDB same-bulkhead rejection performs zero transport |
| Runtime close drains queued work and preserves handle lifetime | Lifecycle transcript | Two queued scans and requests, explicit executor close, executor destruction with live streams/handles, clean exhaustion with retained stream, repeated close, and Query DatabaseInstance shutdown | Passed: controller close drains all three queues, permits outlive the controller, clean exhaustion releases scan capacity, package composition orders registry then executor close, and actual DatabaseInstance teardown closes the executor exactly once after quiescence |

## Alternatives considered

### Keep only per-scan budgets

This preserves current simplicity but cannot prevent many individually valid
scans from exhausting shared workers, handles, or memory. It fails the outcome.

### Use nested global/destination/principal semaphores

This is easy to implement but retains outer permits while waiting for inner
ones, creates acquisition-order coupling, and cannot prove independent-key
progress. Rejected in favor of one atomic dimension vector.

### Reuse only the rate-limit coordinator

Its key and one-permit rule represent remote quota semantics, including an
optional response-derived bucket. General capacity has overlapping global
dimensions and must protect initial/non-v3 requests and buffers. Merging the
two would let untrusted remote values partition host capacity and would force
unrelated requests through quota serialization. Rejected.

### Queue every shortage

Queueing buffers or secondary recovery while retaining response or recovery
state increases the resource under pressure. Buffer and retry/rate-wait
shortages therefore fail fast; only scan/request work that retains no remote
response queues.

### Fail every admission shortage immediately

This minimizes retained threads but turns brief ordinary contention into user
failure and provides no same-key fairness. Bounded scan/request queues preserve
short-burst usability while fixed queue and deadline ceilings prevent
unbounded waiting.

### Add connector or SQL tuning

An author-controlled pool or user setting would create new compatibility,
cardinality, and authority policy. The product request requires finite host
isolation and explicitly reuses existing identities. Fixed host policy is the
smallest safe surface; tuning remains a later product option.

### Implement circuit breaking in the same goal

Circuit state uses prior remote outcomes to suppress later requests and has a
separate closed/open/half-open acceptance narrative. The product manager
approved it as the follow-on only after admission evidence passes. Excluded.

## Drawbacks and failure modes

- Fixed ceilings may reject a workload that the operating system could have
  handled temporarily. Stable local diagnostics make the chosen bound visible;
  public tuning is deliberately deferred.
- Scan admission resolves a bounded local credential snapshot before queueing
  because opaque principal identity does not exist earlier. The finite scan
  queue bounds retained snapshots, but provider latency itself is not governed
  by principal admission.
- Eligibility-aware scheduling is FIFO only within an exact bulkhead. Across
  keys it deliberately bypasses a currently ineligible older ticket to protect
  isolation; it is not weighted priority or tenant scheduling.
- Conservative direct-capability grouping can create more interference in
  compatibility tests or private consumers than provider-backed execution.
  It cannot expose or merge credential data, and the installed path is exact.
- Reserving admitted maxima before allocation can reject concurrent pages whose
  actual responses would have been smaller. This is the cost of proving a hard
  pre-allocation bound with the current whole-response transport interface.
- Explicit executor close adds lifecycle coordination. Query owns the close
  call; Runtime owns idempotence, wakeup, and permit cleanup.

## Acceptance and verification

- **End-to-end demonstration:** Actual-DuckDB REST and GraphQL cases each
  saturate one connector/destination/principal and reject same-key work before
  transport while a distinct healthy connector completes. A named v3 REST
  pressure case keeps a slow scan beside a second scan that traverses ordinary
  retry and a measured rate-limit wait, with deterministic provider evidence
  that both waiter classes can coexist in one exact bulkhead. Show local
  saturation/timeout/cancellation diagnostics, zero transport for rejected
  attempts, clean DatabaseInstance shutdown, and healthy completion after the
  calls have entered DuckDB. The accepted finite-worker trial already proves
  blocked connection callbacks do not monopolize DuckDB's configured shared
  execution threads; delivery replaces its fake stream with admitted Runtime
  work.
- **Automated oracle:** Global and every per-dimension boundary/one-over;
  identical-key FIFO and cross-key eligible bypass; starvation under repeated
  A arrivals; scan/request queue saturation and timeout; cancellation before,
  during, and after grant; coincident queue/deadline/close precedence; checked
  ticket exhaustion; retry/rate waiter caps and zero response bytes while
  waiting; attempt-not-charged law; buffer byte/row boundary and rollback; credential
  replacement/drop/recreation; anonymous/direct/provider isolation; repeated
  authorization failure exclusion; REST/GraphQL and single/paginated paths;
  multi-batch page/handoff ownership; clean exhaustion with a retained stream;
  executor destruction with live streams/handles; explicit close; and actual
  DuckDB shutdown.
- **Quality gates:** Every documentation/agent and product-source command in
  `AGENTS.md`, including the public-surface and freeze mutation oracles, source
  identities, native dependency record, fresh native product build, demo,
  Community installation, and Community enablement.
- **Independent review:** Query lifecycle/diagnostic and Remote Runtime
  concurrency/resource-authority review for this RFC; at least two fresh
  adversarial transport/policy and concurrency/lifecycle/test-oracle reviewers
  after implementation.
- **Interaction exit:** Final source and target dependencies place the
  admission provider and its focused oracles behind a bounded Runtime service.
  Query consumes only `ScanExecutor`, `BatchStream`, and safe diagnostics; no
  Query target imports admission identity, counters, mutexes, or test
  constructors.

## Contract propagation

| Source of truth or artifact | Impact | Required update | Completion evidence |
| --- | --- | --- | --- |
| `docs/ARCHITECTURE.md` | Affected | Add executor-wide admission identity, bounds, ordering, diagnostics, and close laws; retain circuit exclusion | Pending delivery |
| `docs/CONNECTOR_SPECIFICATIONS.md` | Not affected | Existing syntax/resources compile unchanged; no pool declaration or v4 family | Schema/identity gates remain byte-identical |
| `docs/RUNTIME_CONTRACTS.md` | Affected | Specify service identity, atomic admission, wait/buffer reservations, attempt ordering, diagnostics, and lifecycle | Pending delivery |
| `docs/TEAM_TOPOLOGY.md` and active charters | Not affected | Existing Query consumer and Runtime provider responsibilities place the work | Final dependency/interaction audit |
| `docs/PRODUCT_DELIVERY.md`, `AGENTS.md`, and skills | Not affected | Existing split-goal, RFC, contract-change, delivery, and review rules govern | No edit required |
| `ROADMAP.md`, release/freeze inventory, README, diagnostics, fixtures, and tests | Affected | Add the bounded-admission release outcome, remove the stale no-admission limitation, and bind every new closed vocabulary/limit | Pending delivery |

## Unresolved questions

None decision-critical. Public operator tuning, process-wide or distributed
quotas, weighted priorities, public metrics, and circuit breaking are separate
product options rather than hidden delivery work.

## Review record

| Required reviewer | Team | Result | Evidence or objection | Disposition by decision owner |
| --- | --- | --- | --- | --- |
| `admission_query_review` | Query Experience | Approved | Independent re-review verified all-dimensional atomic limits, stable safe diagnostics, `Close() const noexcept` compatibility, quiescent DatabaseInstance teardown, exact permit release, and the finite-worker DuckDB trial; no material Query objection remains | Accepted. The trial decides the shared-worker model; production admission, close-race, diagnostic, and dependency oracles remain delivery exit evidence |
| `admission_runtime_review` | Remote Runtime | Approved | Independent re-review verified the finite-worker trial and the final clean-exhaustion, pre-wait response release, deadline/cancellation linearization, shared-controller lifetime, provider-resolution, and multi-batch page/handoff ownership laws; no material Runtime objection remains | Accepted. Production concurrency/resource/lifecycle oracles and the final dependency audit remain delivery exit evidence |

## Decision and rationale

- **Technical decision owner:** Lead agent.
- **Product approval:** Nic Galluzzo supplied the bounded isolation outcome and
  guardrails, then explicitly approved splitting circuit breaking into a
  follow-on goal on 2026-07-23.
- **Rationale:** Existing bounded plan and authority facts are sufficient to
  derive finite isolation without Connector or Semantics policy. One atomic
  executor-local controller prevents partial multi-dimensional acquisition;
  bounded eligible-ticket scheduling preserves same-key order without
  cross-key head-of-line blocking; pre-allocation reservations and explicit
  close make memory and lifecycle authority inspectable. The controlled
  DuckDB trial resolves the only host-worker uncertainty, and the split keeps
  history-based circuit policy out of the admission proof.
- **Material objections:** Initial Query and Runtime objections identified
  incomplete all-dimensional limits, unstable diagnostics, immutable-executor
  compatibility, quiescent DatabaseInstance teardown, worker ownership,
  clean-exhaustion release, response retention during recovery, deadline and
  cancellation races, controller lifetime, provider admission, and multi-batch
  page ownership. The final decision incorporates each required law and oracle;
  both required reviewers approved with no remaining objection. Production
  evidence is deliberately recorded as an interaction-exit requirement, not
  represented as completed RFC evidence.
- **Superseded by:** Not applicable.

## Follow-on goals

| Outcome or objective | Accountable team | Supporting teams and interaction modes | Activation condition |
| --- | --- | --- | --- |
| Deliver bounded Runtime admission and bulkhead isolation | Query Experience | Remote Runtime — Collaboration then X-as-a-Service | RFC 0026 Accepted with every required review disposition recorded |
| Suppress repeated remote failures through finite exact-key closed/open/half-open circuit state | Query Experience | Remote Runtime — Collaboration then X-as-a-Service | Bounded-admission goal completed and demonstrated; separate circuit-breaker RFC Accepted |

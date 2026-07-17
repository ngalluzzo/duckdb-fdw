# RFC 0003: Define the native scan execution boundary

```yaml
rfc: "0003"
title: "Define the native scan execution boundary"
status: "Accepted"
rfc_type: "Product"
sponsor_team: "Query Experience"
technical_decision_owner: "Lead agent"
product_approver: "none"
authors:
  - "Lead agent"
required_reviewers:
  - "Query Experience perspective"
  - "Remote Runtime perspective"
  - "Relational Semantics perspective"
affected_teams:
  - "Query Experience"
  - "Remote Runtime"
  - "Relational Semantics"
linked_outcome_or_objective: "0.1.0 — first trustworthy query"
supersedes: "none"
```

## Summary

Define a protocol-neutral native execution boundary for `0.1.0`. Query
Experience supplies a non-owning cancellation view at DuckDB callbacks and
consumes a `ScanExecutor` plus the `BatchStream` it opens. Remote Runtime owns
the cancellation checkpoints, structured cancellation marker, fixture
provider internals, and stream lifecycle. No DuckDB type crosses the runtime
API, and no public SQL, diagnostic, compatibility, or product behavior changes.

## Sponsorship and context

- **RFC type:** Product.
- **Sponsoring team:** Query Experience.
- **Linked outcome or objective:** Complete the accepted `0.1.0 — first
  trustworthy query` through the team boundaries required by RFC 0001.
- **Why now:** The existing vertical slice proves cancellation and teardown,
  but its adapter retains concrete fixture-provider state and its runtime API
  imports `duckdb::ClientContext`. The accepted structural correction cannot
  reach the Query Experience or Remote Runtime interaction exits without
  deciding this shared interface first.

The DuckDB user sees the same function, rows, cancellation behavior, and safe
diagnostics. This decision concerns how the adapter and runtime divide that
work internally.

## Problem

RFC 0001 requires a protocol-neutral
`CompiledConnector → ScanRequest → ScanPlan → BatchStream` path and says the
DuckDB adapter consumes the stream boundary instead of fixture or protocol
internals. The current implementation does not satisfy that team contract:

- `BatchStream::Next` and `FixtureReadBuffer` accept
  `duckdb::ClientContext`, so runtime decoding and lifecycle code depend on a
  DuckDB host object;
- the decoder polls `ClientContext::IsInterrupted()` and throws
  `duckdb::InterruptException`, so final host error translation occurs inside
  Remote Runtime rather than once at the adapter boundary;
- `RegisterDuckdbApi`, immutable function information, and bind data all expose
  and retain `FixtureFactory`; and
- the adapter calls `OpenBatchStream(plan, fixture_factory)` directly, so it
  knows the concrete provider used to produce its runtime service.

The existing synchronized-cancellation oracle also establishes an important
constraint: execution is synchronous and can remain inside fixture reading or
JSON decoding until a checkpoint observes interruption. A token that is set
only when the scan callback regains control cannot provide responsive
cancellation for this profile.

## Decision drivers and invariants

- **Must preserve:** The exact RFC 0001 SQL surface, typed rows, compatibility
  cell, redacted diagnostic categories, offline bind/planning, bounded
  execution, prompt cancellation, one-task ownership, and idempotent close.
- **Must preserve:** `ScanPlan` remains the complete semantic contract;
  execution may reject unsupported capabilities but must not reconstruct
  relational ownership or safety.
- **Must enable:** Runtime cancellation and decoding can be understood and
  tested without DuckDB registration or `ClientContext`.
- **Must enable:** The adapter can open one stream per scan without retaining a
  concrete fixture source, factory, decoder, or protocol implementation.
- **Must not introduce:** A worker thread, async runtime, public native ABI,
  general connector loader, live transport capability, new public diagnostic,
  or product-manager-reserved behavior.

## Proposed decision

### Public behavior

Not affected. `duckdb_api_scan`, its arguments and schema, the exact fixture
rows, extension identity, supported source-build cell, DuckDB interruption,
and public `decode`, `schema`, `policy`, and redacted `internal` diagnostics
remain unchanged.

### Shared interfaces

Remote Runtime provides three documented native interfaces:

1. `ExecutionControl` is a non-owning, protocol-neutral view with one
   non-throwing `IsCancellationRequested()` query. The adapter creates a
   DuckDB-backed implementation for global initialization and each scan
   callback. Runtime code may use the view only for the duration of that call;
   a stream, source, or decoder must not retain it. Its virtual destructor is
   also non-throwing.
2. `ScanExecutor` is an immutable, shareable service with
   `Open(ScanPlan, ExecutionControl) -> BatchStream`. The native fixture
   implementation owns its `FixtureFactory`, validates the executor capability
   envelope and fixture identity before opening a source, and creates fresh
   mutable stream state for every call.
3. `BatchStream` remains synchronous and pull-oriented. Its `Next` method
   accepts `ExecutionControl` rather than `duckdb::ClientContext`, and it keeps
   idempotent, non-throwing `Cancel` and `Close` operations. Runtime interface
   destructors are non-throwing. A runtime-owned
   `ExecutionCancelled` marker crosses the team boundary; the DuckDB adapter
   converts it to `duckdb::InterruptException` exactly once.

The C++ declarations are private implementation contracts, not a public ABI.
The exact naming may follow repository C++ conventions, but the provider,
consumer, ownership, lifetime, error, and dependency semantics above are
mandatory.

The DuckDB registration boundary receives an immutable `CompiledConnector`
and an immutable shared `ScanExecutor`. Registration state retains those two
provider interfaces. Bind state retains the immutable request and plan plus
the executor handle only because DuckDB global initialization occurs later.
Global state exclusively owns one `BatchStream`; rows returned by `Next` remain
scan-callback-local.

Concrete `FixtureSource` and `FixtureFactory` declarations remain internal to
Remote Runtime and its direct tests. Native example composition is the sole
production location that knows both the embedded fixture implementation and
the connector fixture digest; the DuckDB adapter knows neither.

### Operational behavior

- Global initialization builds an adapter-owned `ExecutionControl` view and
  asks `ScanExecutor` to open a stream. Cancellation is checked before the
  executor opens a source.
- Each scan callback builds a new non-owning view over the callback's
  `ClientContext` and passes it to `BatchStream::Next`.
- Runtime checkpoints combine the call-scoped cancellation view, the stream's
  idempotent canceled state, and its runtime-owned wall-time deadline. Fixture
  reading, parsing, conversion, and row emission continue to checkpoint.
- A stream reports its interruption hook at most once before propagating
  `ExecutionCancelled`. A provider hook failure cannot replace that marker.
  The adapter marks an existing stream canceled and throws DuckDB's
  interruption type. Because `Cancel` is non-throwing, cleanup cannot mask the
  interruption. Cancellation is never translated into a public
  `ExecutionError` category and is never eligible for retry.
- Plan authorization checks only the native executor's capability envelope:
  fixture executor identity, fixed method/path/extractor, fixture digest,
  disabled network/pagination/provider/retry/cache capabilities, supported
  runtime work, and hard budgets. It does not recompute which relational
  operations DuckDB owns or whether planner choices were semantically safe.
- `ScanExecutor::Open` validates the plan and factory digest before the
  concrete factory opens a source. A failed validation or pre-open
  cancellation creates no source. Each successful open owns a distinct source.
- `ScanExecutor::Open` and `BatchStream::Next` may report
  `ExecutionCancelled` or structured `ExecutionError`. The adapter catches
  those first; any other provider or runtime exception becomes the existing
  redacted internal DuckDB error. Registration rejects missing dependencies as
  a DuckDB internal error before publishing the function.
- `Cancel`, `Close`, runtime destructors, and global-state destruction are
  non-throwing. `Close` remains idempotent and is reached after success,
  failure, cancellation, early consumer close, and connection destruction.
  If a provider open hook fails after acquiring a source, the executor performs
  best-effort non-throwing source cleanup and preserves the original failure.
  Failures from interruption or close hooks are contained and cannot mask the
  primary cancellation, execution error, or unknown exception being
  translated.
- The native profile remains serialized through one DuckDB source task; the
  call-scoped control view is never shared or retained.

## Topology impact

| Team | Role in this RFC | Interface or charter impact | Interaction mode | Exit condition |
| --- | --- | --- | --- | --- |
| Query Experience | Sponsor and consumer | Owns DuckDB-backed `ExecutionControl`, callback state, one-time cancellation/error translation, and `DataChunk` output | Collaboration, then X-as-a-Service | The adapter retains only immutable connector/executor interfaces and one stream per scan, with no runtime-provider internals |
| Remote Runtime | Provider | Owns `ExecutionControl`, `ExecutionCancelled`, `ScanExecutor`, `BatchStream`, fixture internals, checkpoints, budgets, and lifecycle | Collaboration, then X-as-a-Service | Runtime and decoder tests exercise open, pull, cancel, deadline, failure, and close without DuckDB registration or types |
| Relational Semantics | `ScanPlan` contract reviewer | Ensures executor authorization consumes the plan without reclassifying planner-owned meaning | Collaboration | Runtime rejects unsupported execution capabilities without recomputing residual, ordering, limit, or ownership safety |

No accountability moves. The decision removes DuckDB cognitive load from
Remote Runtime and fixture-provider cognitive load from Query Experience.

## Correctness, security, and lifecycle analysis

- **Relational semantics and conservative fallback:** No classification or
  ownership rule changes. The executor may reject work it cannot perform but
  cannot infer or rewrite plan meaning.
- **Authentication, credentials, network policy, and privacy:** Not affected.
  The native profile still has no network, credential, or caller-selected
  fixture capability.
- **Resource budgets, backpressure, and cancellation:** Hard budgets and
  two-row pull batches remain unchanged. Cancellation polling moves behind a
  protocol-neutral interface and remains active inside every blocking loop.
- **Replay units, retries, caching, and duplicate prevention:** Not affected;
  all remain disabled.
- **Concurrency, immutability, and state ownership:** The executor is immutable
  and shareable. Each open creates one independently owned stream/source.
  Stream methods remain serialized by the one-task adapter. The non-owning
  execution-control view cannot escape its callback.
- **FFI, initialization, reload, shutdown, and failure containment:** There is
  no cross-language FFI or reload. Initialization and close preserve RAII.
  Runtime cancellation becomes a marker exception that is translated at the
  native DuckDB boundary. Non-throwing cancel, close, and destructors prevent
  cleanup from escaping connection destruction or masking the original
  callback failure; no runtime exception crosses beyond the adapter's DuckDB
  error boundary.
- **Diagnostics, redaction, metrics, and progress:** Public diagnostics and
  redaction are unchanged. Cancellation remains DuckDB interruption rather
  than a project error string. Progress remains unavailable.

## Compatibility and migration

No user, connector package, stored data, SQL, artifact, or public ABI migration
is required. The changed C++ types are private pre-release team interfaces.
All current adapter and runtime consumers migrate atomically inside the
repository.

Rollback restores the current DuckDB-coupled interface and therefore reopens
the RFC 0001 and RFC 0002 boundary failure. Rollback is acceptable only as
short-lived fault containment; it cannot satisfy the `0.1.0` interaction exits.

Unsupported future capability profiles still fail closed. This RFC does not
authorize async execution, concurrent stream methods, general transports, or
new cancellation claims.

## Evidence and bounded trials

| Question or claim | Evidence required | Method or fixture | Result and limitations |
| --- | --- | --- | --- |
| Is DuckDB coupling confined to cancellation polling? | Direct dependency inspection | Search native runtime declarations and implementations for `ClientContext`, `IsInterrupted`, and DuckDB exceptions | Confirmed: runtime use is limited to call-scoped checkpointing and interruption; no other runtime behavior requires the host object |
| Does the adapter retain provider internals? | Construction and state inspection | Trace registration, bind data, and global initialization | Confirmed: all three currently expose `FixtureFactory`, and the adapter directly calls `OpenBatchStream` |
| Can responsive synchronous cancellation use a non-owning probe? | Existing blocking-fixture execution path | Trace the synchronized cancellation oracle through `FixtureReadBuffer::Checkpoint` and decoder checkpoints | Confirmed: the path requires only a contemporaneous boolean interruption query; the control view preserves that query while removing the DuckDB type |
| Can composition supply independent streams without adapter fixture knowledge? | Existing factory lifecycle and concurrency evidence | Trace factory digest validation, per-open source creation, and the two concurrent-scan oracle | Confirmed: moving the factory behind an immutable executor preserves one fresh source per open; implementation evidence must retain the existing counters |
| Does this alter public behavior? | Public inventory and behavior oracles | Existing SQLLogicTest, artifact inventory, failure, cancellation, and lifecycle suites | No intended change; byte-stable behavior evidence remains required during delivery |
| Can cleanup preserve the primary failure? | Explicit exception disposition plus failure injection | Exercise throwing source open/read/interruption/close hooks through executor, adapter, early close, and connection destruction | The required containment is expressible with non-throwing cancel/close/destructors and catch-before-translation ordering; the current adapter already catches unknown open/pull exceptions, but delivery must add the missing hook and destruction oracles |

No separate feasibility trial is needed: the existing synchronized path already
executes the required control query at every relevant checkpoint. Delivery must
add direct runtime oracles so this conclusion no longer depends on adapter
integration.

## Alternatives considered

### Retain `duckdb::ClientContext` in runtime APIs

This requires the least code movement and preserves current tests. It keeps
Remote Runtime coupled to DuckDB headers, exceptions, and callback lifetime,
prevents independent runtime tests, and fails the accepted protocol-neutral
boundary. Rejected.

### Use an adapter-owned atomic token set between scan callbacks

This gives runtime a simple owned token. The current synchronous fixture read
can remain inside one scan callback while another thread calls
`Connection::Interrupt`; the adapter does not regain control to update the
token. The blocking cancellation oracle would hang until the wall-time budget.
Rejected for this profile.

### Pass `std::function<bool()>` directly

This is mechanically small and could wrap `ClientContext::IsInterrupted`.
It leaves ownership, non-retention, exception, and thread-safety expectations
implicit in a generic callable and makes the cross-team contract harder to
document and fake directly. A named minimal interface is clearer. Rejected.

### Let the adapter retain `FixtureFactory` but hide its declarations

This changes includes without changing responsibility. Query Experience would
still own provider identity, factory lifetime, and source construction. It
would fail RFC 0001's consumer boundary. Rejected.

### Remove `BatchStream::Cancel` and rely only on the control view

The probe covers host interruption while a callback is active, but explicit
cancel state is still required for teardown and a uniform idempotent lifecycle.
Rejected.

## Drawbacks and failure modes

- A virtual cancellation query runs at frequent decode checkpoints. Fixture
  size is bounded, and the cost is negligible relative to correctness.
- A non-owning view can dangle if retained. The interface documentation,
  private storage, direct tests, and review must prohibit retention; an owned
  future async token requires a later RFC rather than weakening this lifetime.
- The executor adds one native team API. It replaces the adapter's concrete
  provider dependency and mirrors the accepted portable `ScanExecutor`
  contract, but it must remain narrow and private.
- Translating cancellation in more than one callback can duplicate state
  transitions. Adapter helpers must centralize translation, and stream cancel
  plus interruption reporting must be idempotent.
- Provider lifecycle hooks can fail while another error is already in flight.
  Runtime teardown must contain those failures, preserve the primary error, and
  keep cancel, close, and destruction non-throwing.
- Over-broad plan validation could preserve the current semantic leak under a
  new filename. Runtime tests and Relational Semantics review must distinguish
  capability rejection from relational reclassification.

## Acceptance and verification

- **End-to-end demonstration:** The unchanged accepted SQL returns the exact
  rows; the blocking fixture is interrupted through DuckDB; early close,
  connection destruction, and concurrent scans retain their current counters.
- **Automated oracle:** Direct executor/stream tests cover cancellation before
  source open, cancellation during fixture read and decoding, wall-time
  exhaustion, distinct sources, plan capability rejection before side effects,
  idempotent non-throwing cancel/close, and one-time interruption reporting.
  Fault-injection tests cover throwing factory open, source-open, read,
  interruption, and close hooks; preserve `ExecutionCancelled` or the original
  execution failure; and exercise adapter init/scan, early close, and
  connection destruction. Adapter tests also cover DuckDB translation and the
  absence of bind-time source opens.
- **Quality gates:** Repository validation, source identities, every migrated
  native C++ target in the product runner, every production target under the
  sanitizer verifier, a fresh native debug product cell, and the unchanged
  public behavior digest. Release completion still requires RFC 0001's native
  Linux sanitizer and clean-tag evidence cells.
- **Independent review:** Query adapter/lifecycle, Remote Runtime
  cancellation/resource/close, Relational Semantics plan ownership, and at
  least two fresh adversarial lifecycle/test-oracle perspectives over the
  implementation diff.
- **Interaction exit:** Query Experience consumes the documented executor and
  stream without fixture knowledge; Remote Runtime builds and tests without
  DuckDB adapter registration or `ClientContext`; runtime consumes `ScanPlan`
  without reclassifying relational meaning.

## Contract propagation

| Source of truth or artifact | Impact | Required update | Completion evidence |
| --- | --- | --- | --- |
| `docs/ARCHITECTURE.md` | Affected | Record the protocol-neutral native cancellation and executor boundary | Updated in the accepted native preview profile in this decision change |
| `docs/CONNECTOR_SPECIFICATIONS.md` | Not affected | No author syntax, package, schema meaning, or preview evidence boundary changes | No update required |
| `docs/RUNTIME_CONTRACTS.md` | Affected | Record native `ExecutionControl`, `ScanExecutor`, cancellation translation, ownership, and close behavior | Updated in the native mapping, lifecycle, executor, stream, and cancellation contracts in this decision change |
| `docs/TEAM_TOPOLOGY.md` and active charters | Not affected | Existing producer-consumer boundaries and documentation expectations already govern the decision | No update required |
| `docs/PRODUCT_DELIVERY.md`, `AGENTS.md`, and skills | Not affected | Existing RFC, contract-change, delivery, and review practices cover implementation | No update required |
| Examples, diagnostics, fixtures, and tests | Affected | Preserve public behavior and split direct runtime from adapter lifecycle evidence | Pending implementation after acceptance |

The active `0.1.0` goal records this RFC gate and remains the implementation
working record.

## Unresolved questions

None. Async cancellation tokens, worker ownership, and broader execution
profiles remain outside `0.1.0` and require their own evidence and decision.

## Review record

| Required reviewer | Team | Result | Evidence or objection | Disposition by decision owner |
| --- | --- | --- | --- | --- |
| Query Experience perspective | Query Experience | Approved | Focused re-review confirmed explicit non-throwing teardown, primary-error preservation, callback catch order, registration failure, and fault-injection evidence across init, scan, early close, and destruction | Initial lifecycle objection resolved by making cancel/close/destructors non-throwing and specifying failure precedence and oracles |
| Remote Runtime perspective | Remote Runtime | Approved | The call-scoped control preserves synchronous cancellation; the executor, runtime deadlines, state-before-hook transitions, scoped cleanup, and primary-failure preservation are implementable in C++11 | No action required for acceptance; direct runtime lifecycle evidence remains an implementation exit |
| Relational Semantics perspective | Relational Semantics | Approved | Executor authorization may reject assigned work it cannot execute while ignoring planner-only ownership/proof fields; current full-plan equality is explicitly not acceptable | Split planner semantic oracles from executor capability oracles during implementation |

Query Experience used a new fresh review context. Environment thread capacity
required the exact Remote Runtime and Relational Semantics reviews to resume
their dedicated charter contexts from the preceding implementation-design
consultation. Those initial reviews were independent; no cross-team finding was
shared until the focused lifecycle revision required re-review.

## Decision and rationale

- **Technical decision owner:** Lead agent.
- **Product approval:** Not required. The accepted interface changes no public
  SQL, diagnostics, compatibility, security policy, external cost, licensing,
  or approved product outcome.
- **Rationale:** Accept. The source evidence shows that a call-scoped
  cancellation query is sufficient for the synchronous preview, while the
  named control and executor interfaces remove DuckDB and fixture-provider
  coupling without adding a worker or changing observable behavior. All
  affected charter perspectives approve the exact revised proposal.
- **Material objections:** Query Experience objected that idempotence alone did
  not prevent cancel, close, provider hooks, or destructors from throwing and
  masking cancellation or terminating connection destruction. The decision now
  requires non-throwing cancel/close/runtime destructors, primary-error
  preservation, explicit adapter exception disposition, best-effort open
  cleanup, and fault-injection evidence. Query Experience and Remote Runtime
  approved the focused revision. No unresolved objection remains.
- **Superseded by:** Not applicable.

## Follow-on goals

| Outcome or objective | Accountable team | Supporting teams and interaction modes | Activation condition |
| --- | --- | --- | --- |
| Complete the topology-aligned `0.1.0` structural correction | Query Experience | Remote Runtime and Relational Semantics through bounded Collaboration; Connector Experience for the separately accepted schema restoration; Engineering Enablement through existing Facilitation | RFC 0003 accepted; responsibility map and contract propagation complete |

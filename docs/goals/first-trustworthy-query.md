# Goal: First trustworthy query

## PM brief

### Outcome

For a DuckDB user, enable querying one static REST-backed relation so that API
data is demonstrably usable as a correct, bounded DuckDB relation.

### Why now

This is the first outcome in the approved roadmap and moves the project from
theoretical contracts to executable product evidence. It tests the central
product claim before investment in connector-authoring breadth,
authentication, pagination, or GraphQL.

### Product guardrails

- Must: return correct typed rows from a deterministic REST fixture.
- Must: perform no network I/O during ordinary bind or planning.
- Must: execute through bounded, cancelable, safely closed work.
- Must: expose a meaningful, redacted representative failure.
- Must not: imply support for live authentication, pagination, GraphQL, public
  distribution, or a stable connector specification.

### Success signals

- A user can build and load the extension on the declared DuckDB and platform
  target.
- An ordinary SQL query over the example relation returns the expected rows.
- The user can cancel or close execution without hanging or leaking work.
- Invalid fixture data produces an actionable error without exposing sensitive
  values.

## Agent commitment

### Observable interpretation

From a clean checkout, a user builds the native `duckdb_api` extension, starts
the supported DuckDB host with unsigned extension loading enabled, directly
loads the local artifact, and runs:

```sql
SELECT id, name, active
FROM duckdb_api_scan(
    connector := 'example',
    relation := 'items'
)
ORDER BY id;
```

The query returns exactly `(1, 'alpha', true)`, `(2, 'beta', false)`, and
`(3, 'gamma', true)` with the accepted `BIGINT`, `VARCHAR`, and `BOOLEAN`
schema. Ordinary bind and planning remain offline. The same production
pipeline supports bounded execution, synchronized cancellation, cleanup, and
distinct redacted `decode` and `schema` failures exercised through private
test-only fixture scenarios.

The supported product cell is DuckDB 1.5.4 at commit `08e34c447b` on Apple
Silicon macOS arm64, built from source with the toolchain recorded by RFC 0001
and loaded locally as an unsigned extension. No binary distribution or broader
compatibility promise is part of this goal.

### Acceptance evidence

- Demonstration: clean source build, direct local load, extension identity,
  exact SQL result, synchronized cancellation, cleanup, and meaningful
  redacted failures.
- Automated oracle: expected typed rows; connector and plan snapshots; a
  bind-time network sentinel; bounded-batch, cancellation, teardown,
  strict-conversion, diagnostic-stage, and public-inventory checks.
- Quality gates: the clean-tag product cell, pinned
  `linux_amd64-sanitized` evidence cell, machine-readable build and artifact
  manifest, checksum verification, mismatch and tamper canaries, repository
  validation, and two cache-empty workspace reproductions required by RFC
  0001.
- Independent review: adversarial review of relational correctness, resource
  bounds, concurrency, native ABI and lifecycle behavior, diagnostics, and
  release evidence.

### Contract and invariant impact

- Affected sources: `docs/ARCHITECTURE.md`,
  `docs/CONNECTOR_SPECIFICATIONS.md`, `docs/RUNTIME_CONTRACTS.md`, the public
  example and diagnostics, build and release gates, and user-facing build and
  query instructions.
- Affected interfaces: the internal example connector subset,
  `CompiledConnector`, `ScanRequest`, `ScanPlan`, fixture execution,
  `BatchStream`, diagnostics, and the native DuckDB adapter boundary.
- Preserved invariants: offline deterministic bind and planning, conservative
  relational semantics, strict conversion, immutable plans, bounded
  backpressure, prompt cancellation, deterministic teardown, redaction, and
  no native-boundary unwinding.

### Team and RFC routing

- Accountable stream: Query Experience.
- Connector Experience — Collaboration: align the internal static connector
  declaration and immutable snapshot with the query result. Exit when one
  fixture proves the author-to-query path without creating a public authoring
  contract.
- Remote Runtime — Collaboration: provide bounded fixture-backed REST-shaped
  execution, strict conversion, cancellation, and teardown. Exit when Query
  Experience consumes it only through the shared stream boundary.
- Relational Semantics — Collaboration: preserve conservative
  `ScanRequest → ScanPlan` ownership. Exit when golden plans prove fallback,
  residual ownership, projection closure, empty ordering, and unset bounds.
- Engineering Enablement — Facilitation: establish the pinned build,
  sanitizer, manifest, compatibility, tamper-canary, and reproduction gates.
  Exit when Query Experience owns and independently runs the documented
  release path.
- RFC 0001, `Establish the first trustworthy query contract`, governs public
  behavior; RFC 0002, `Establish production design and code documentation
  practice`, governs the correction; and RFC 0003, `Define the native scan
  execution boundary`, is Accepted for cancellation and stream acquisition.

### Unknowns and first trial

- Unknown: None identified that changes the approved outcome or public
  boundary. Internal JSON and fixture-storage choices remain delegated to
  delivery.
- Trial: Completed. The native DuckDB boundary experiment proved direct load,
  typed multi-chunk output, synchronized cancellation, and cleanup on the
  supported host; see `experiments/native-extension/RESULTS.md`.

### Delivery path

1. Propagate RFC 0001 through architecture, connector, runtime, examples, and
   executable contract oracles.
2. Deliver the native end-to-end success, cancellation, failure, and teardown
   narrative through the accepted shared boundaries.
3. Establish the exact product and sanitizer evidence cells, negative
   canaries, manifest, checksum, and self-service runbook.
4. Reproduce the release candidate from two cache-empty workspaces, complete
   independent review, and record the completion evidence.

### Structural correction before release completion

RFC 0002's responsibility pass found that the behavioral slice is green but
its production design has not reached the team-interaction exits claimed by
RFC 0001. This correction is part of `0.1.0`: do not tag it or begin the next
roadmap outcome while the source and test dependencies still require routine
cross-boundary knowledge.

#### Responsibility map

| Responsibility | Producer and consumers | Production home | Primary reason to change |
| --- | --- | --- | --- |
| Internal example connector metadata | Connector Experience provides an immutable `CompiledConnector` to planning, the DuckDB adapter, and composition | `connector.hpp` and `connector.cpp` | The internal `example.items` identifiers, complete schema/nullability/extractors, operation declaration, fixture provenance, or snapshot changes |
| Adapter request contract | Query Experience creates `AdapterCapabilities` and `ScanRequest`; Relational Semantics consumes them | `scan_request.hpp` and a small request-construction implementation at the adapter edge | DuckDB capabilities or their conservative unavailable values change |
| Relational planning | Relational Semantics consumes connector metadata and a request, then provides an immutable `ScanPlan` to execution | `scan_plan.hpp` and `scan_planner.cpp` | Operation selection, ownership, projection closure, conservative fallback, budgets in the accepted plan, or plan explanation changes |
| Fixture decoding | Remote Runtime converts the accepted fixture representation into strict typed rows for streaming | private decoder declarations and `fixture_decoder.cpp` | JSON validity, extraction, lossless conversion, schema failure classification, or decode resource checks change |
| Bounded stream runtime | Remote Runtime consumes an authorized plan and provides `BatchStream` | `batch_stream.hpp` and `fixture_batch_stream.cpp` | Source opening, resource authority, batching, deadlines, cancellation, concurrency, error staging, or deterministic close changes |
| DuckDB adapter | Query Experience consumes connector, planner, and stream-provider APIs | `duckdb_adapter.cpp` behind `duckdb_api_extension.hpp` | Registration, bind/init/scan state, `DataChunk` output, DuckDB error translation, exception containment, or host compatibility changes |
| Native example composition | Query Experience wires the repository-owned example connector and embedded fixture provider into the adapter | `embedded_example.cpp` plus the existing content-addressed asset declaration | The native example asset or composition changes; this is the only production location allowed to know both connector identity and the concrete embedded fixture provider |

The filenames are implementation targets, not a metric. Co-location remains
valid where code shares the same invariants and reason to change; changing a
name does not relax the producer-consumer boundaries above.

#### Dependency direction

```text
native example composition
  ├──> connector metadata API
  ├──> stream-provider API
  └──> DuckDB adapter registration

DuckDB adapter
  ├──> immutable CompiledConnector
  ├──> ScanRequest -> planner API
  └──> BatchStream

relational planner
  ├──> immutable CompiledConnector
  └──> ScanRequest
       └──produces──> immutable ScanPlan

fixture runtime
  └──> immutable ScanPlan
       └──produces──> BatchStream
```

- Connector metadata has no planner, runtime, fixture-lifecycle, or DuckDB
  callback dependency.
- Planning performs no I/O and has no DuckDB object, runtime implementation, or
  execution-state dependency.
- Runtime validates its authorization envelope but does not reconstruct
  residual ownership, ordering, limit, or other relational meaning.
- The adapter neither declares nor retains a concrete fixture source or
  factory. Example composition owns that concrete wiring.
- Production never depends on test scenarios or lifecycle probes.

#### Governance checkpoint

Moving declarations and implementations while preserving observable behavior
is an internal refactor. Completing the following RFC 0001 requirements is a
strict contract restoration and uses `$contract-change` during implementation:

- make `CompiledConnector` actually contain the declared static schema,
  nullability, and extractor metadata rather than synthesizing them only in a
  snapshot string; and
- make the DuckDB adapter consume the runtime through its stream boundary
  rather than constructing or retaining `FixtureFactory` internals.

Before changing `BatchStream::Next`, cancellation ownership, or the way the
adapter obtains a stream, open and accept a focused RFC for that shared runtime
interface. In particular, removing `duckdb::ClientContext` from runtime APIs
would improve protocol neutrality but changes a mandatory team interface and
cannot be hidden inside the file split. The RFC must decide how the adapter
translates DuckDB interruption into runtime-owned execution control and how
composition supplies a stream without exposing provider internals.

That focused RFC must specify the runtime-owned cancellation/control object,
its polling or synchronization model, lifetime through scan and connection
shutdown, cancellation error translation, source-open timing, provider and
stream ownership, and open/close failure behavior. Query Experience sponsors
the decision for this active product outcome; Remote Runtime and Query
Experience are required reviewers, with Relational Semantics included if plan
consumption changes. Product-manager approval is required only if the proposal
changes a reserved public, compatibility, security, or outcome decision.

#### Oracle and test homes

| Oracle family | Test home and independence requirement |
| --- | --- |
| Connector identity, full schema/nullability/extractors, provenance, immutability, and snapshot | `connector_contract_tests.cpp`; builds with connector implementation only |
| Conservative request, complete plan snapshots, mutation rejection, and relational ownership laws | `scan_planner_tests.cpp`; builds without adapter or runtime implementation |
| JSON validity, Unicode, lossless `BIGINT`, extraction, strict conversion, budgets, and decode/schema staging | `fixture_decoder_tests.cpp`; uses runtime-owned test inputs without DuckDB registration |
| Source-open ordering, bounded batches, deadline, cancellation, idempotent close, failure cleanup, and independent concurrent scans | `batch_stream_tests.cpp`; uses reusable scenarios and lifecycle probes from `test/cpp/support/` without adapter registration |
| Registration, offline bind, immutable bound state, one-time error translation, chunk output, early close, and connection shutdown | `duckdb_adapter_tests.cpp`; contains only cross-layer behavior that requires DuckDB |
| Accepted SQL, schema, DuckDB-local filter/order/limit ownership, and bind diagnostics | Existing SQL suite remains the public relational oracle |
| Loadable artifact identity and public inventory | Existing Python artifact suite remains unchanged |

Separate CMake targets must demonstrate provider independence. Shared test
support may contain assertions, scenario sources, and lifecycle probes, but no
production behavior. Do not replace this design evidence with file-size,
comment-density, or architecture-lint thresholds.

#### Code-documentation obligations

- Connector declarations explain internal-preview compatibility, identifier
  stability, complete schema and nullability, extractor meaning, provenance,
  immutability, and what consumers may rely on.
- Request and plan declarations identify their producer, consumer, semantic
  ownership, conservative fallback, absence of I/O authority, budgets, and the
  proof behind local ownership of unsupported operations.
- Decoder and runtime declarations explain accepted input, lossless conversion,
  validation-before-side-effect ordering, resource authority, lifecycle state,
  concurrency, deadlines, cancellation checkpoints, close idempotence, error
  ownership, and redaction.
- Adapter code gives a readable registration → bind → plan → init → scan flow
  and documents callback state ownership, supported DuckDB coupling,
  `DataChunk` constraints, exception containment, cancellation translation,
  close behavior, and final diagnostic translation. Registration state retains
  immutable connector data plus only the accepted abstract provider handle;
  bind state retains immutable request and plan data and that handle only when
  DuckDB lifecycle requires it; global state exclusively owns one stream; and
  scan-local rows do not escape the callback.
- Example composition identifies the content-addressed asset provenance and
  states why it is internal evidence rather than a public connector-loading or
  authoring surface.

These are interface and rationale obligations, not a request to comment every
line. Ordinary mechanics should remain readable through names, small cohesive
functions, and focused modules.

#### Green, dependency-ordered work packages

Query Experience owns the CMake test-target inventory and its native and
sanitizer runner integration. Every package that introduces or replaces a C++
target must update `run-native-product-tests.sh`,
`run-linux-sanitized.sh`, and `verify-sanitizer-flags.py` in the same green
commit so all new production objects and tests remain covered. Remove an old
runner or verifier reference in the same commit that removes its target; no
intermediate commit may silently narrow either gate.

1. Decide the runtime cancellation and stream-acquisition boundary through the
   focused RFC; record the exact contract propagation before crossing it.
2. Move and document connector metadata plus request/planning contracts; add
   independently linked connector and planner targets while preserving the
   accepted snapshots and exact public behavior.
3. Move and document strict decoding plus bounded stream execution; split
   decoder and lifecycle oracles, preserve all budgets, error categories,
   cancellation, concurrency, and teardown evidence.
4. Separate native example composition from DuckDB registration; make adapter
   bind/init state retain only immutable connector/request/plan data and the
   accepted runtime-provider boundary.
5. Split the remaining adapter integration suite and remove
   `contracts.hpp`, `duckdb_api_core.cpp`, and the monolithic contract-test
   target once no source, build, runner, or verifier dependency refers to them.
6. Run a charter-backed implementation exit audit over declarations, includes,
   construction points, CMake targets, tests, and adjacent documentation; keep
   any unproved interaction Open.

Every package must leave the build and existing product oracles green and form
a coherent Conventional Commit. No package may introduce a compatibility shim,
duplicate production path, disabled test, or new public behavior merely to make
the move easier.

#### Topology design review

| Perspective | Design disposition | Current interaction status and exit evidence |
| --- | --- | --- |
| Query Experience | Approved after making adapter callback ownership and runner migration explicit | Open: the adapter still retains provider internals, the focused stream/cancellation RFC is unresolved, and release evidence is incomplete |
| Connector Experience | Approved; structured schema/provenance and an independently linked connector oracle restore RFC 0001 without creating public package compatibility | Open: current schema and extractors remain duplicated in snapshot/bind code and connector tests still link the whole product |
| Remote Runtime | Approved; runtime control, stream acquisition, cancellation, resource, error, and lifecycle decisions are correctly gated by a focused RFC | Open: runtime APIs still expose `ClientContext`, the adapter retains `FixtureFactory`, and runtime tests require adapter registration |
| Relational Semantics | Approved; planner ownership, budget recording, independent oracles, and the ban on runtime reclassification match the accepted contract | Open: planning shares the catch-all module and runtime still rechecks planner-owned relational meaning |
| Engineering Enablement | Approved after requiring atomic migration of every native and sanitizer target/runner reference | Open: Query Experience has not yet implemented and independently owned the migrated gates |

Approval means the correction map is ready to execute; it does not satisfy an
interaction exit. Each row remains Open until the implementation and evidence
named here exist.

#### Correction acceptance evidence

- The accepted SQL rows, schema, failures, public inventory, and product
  behavior digest remain byte-for-byte unchanged unless a separately approved
  contract says otherwise.
- Connector and planner targets build and pass without runtime or adapter
  implementation; decoder and runtime targets pass without DuckDB registration.
- The native product and Linux sanitizer gates run every replacement C++ test
  target, and sanitizer-flag verification covers each target that contains
  production code.
- The adapter contains no concrete fixture source/factory and the runtime does
  not reinterpret planner-owned relational meaning.
- Every cross-team and lifecycle-sensitive declaration has the documentation
  required by its charter, and a technically literate reader can trace the
  end-to-end path without reading the integration tests.
- Independent review covers relational semantics, resource bounds,
  concurrency, native lifecycle, diagnostics, and the final dependency audit.
- All five topology interactions remain Open until their recorded source,
  test, release-evidence, and self-sufficiency conditions are actually proven.

This tracked brief is the approved working record for the `0.1.0` goal. Stable
behavior is authoritative only after it is propagated to the contracts and
executable evidence named above.

### Delivery status

The native vertical slice, contract propagation, private scenario/lifecycle
suite, public SQL and inventory oracles, pinned product-cell runner, release
manifest/canaries, sanitizer launcher, and runbook are implemented. Fresh
debug and release-profile product builds pass on the declared macOS arm64 cell.

RFCs 0002 and 0003 are accepted and the responsibility map above is the active
correction plan. The process, skill, and shared execution-boundary decisions
are complete; product code has not yet been restructured, so the topology exits
remain Open.

The goal remains active until the same clean source commit passes the native
Linux amd64 ASan/UBSan cell, receives durable evidence custody, is tagged once
as `v0.1.0`, and completes the tagged product and two-workspace evidence gates.

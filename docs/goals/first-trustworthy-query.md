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
- RFC: RFC 0001, `Establish the first trustworthy query contract`, is Accepted
  and governs delivery.

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

This tracked brief is the approved working record for the `0.1.0` goal. Stable
behavior is authoritative only after it is propagated to the contracts and
executable evidence named above.

### Delivery status

The native vertical slice, contract propagation, private scenario/lifecycle
suite, public SQL and inventory oracles, pinned product-cell runner, release
manifest/canaries, sanitizer launcher, and runbook are implemented. Fresh
debug and release-profile product builds pass on the declared macOS arm64 cell.

The goal remains active until the same clean source commit passes the native
Linux amd64 ASan/UBSan cell, receives durable evidence custody, is tagged once
as `v0.1.0`, and completes the tagged product and two-workspace evidence gates.

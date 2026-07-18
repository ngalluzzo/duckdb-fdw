# Goal: Live REST product proof

## PM brief

### Outcome

For a DuckDB user, enable a source-built extension to query an actual REST API
as typed relational data so that the project proves its central product
mechanism before investing further in authoring or distribution.

### Why now

The loadable extension currently executes only an embedded fixture. That proves
DuckDB integration and lifecycle behavior, but it does not establish whether a
remote API can travel through planning, transport, decoding, and bounded scan
execution as a useful DuckDB relation. This uncertainty is now the highest
priority.

### Product guardrails

- Must: prove real network execution through a loadable DuckDB extension.
- Must: keep ordinary bind and planning offline.
- Must: return strictly typed rows and meaningful safe failures.
- Preserve: bounded work, cancellation, immutable plans, and deterministic
  cleanup.
- Must not: spend this goal on YAML, connector packages, registries, signing,
  Community publication, pagination, authentication, retries, caching, or
  GraphQL.

### Success signals

- A user can source-build and load the extension, run one ordinary SQL query,
  and receive typed rows fetched from a real REST endpoint.
- The same execution path is proven deterministically against a controlled
  HTTP endpoint rather than relying on a public service for correctness.
- Network, HTTP status, response-size, decode, cancellation, and teardown
  boundaries fail predictably without exposing response contents.

## Agent commitment

### Observable interpretation

A source-built loadable extension performs no network work during bind. Scan
initialization turns one immutable native connector plan into an unauthenticated
HTTP GET, enforces a fixed destination and hard response/time limits, decodes
the JSON response with strict schema conversion, and returns bounded typed
DuckDB chunks. A controlled local HTTP service is the correctness oracle; an
opt-in request to the selected real service demonstrates current upstream
compatibility.

This is a decision-value product trial. Its connector identity, endpoint, and
SQL spelling are not compatibility promises and cannot be promoted as the
next public release contract without the product and RFC checkpoints required
by `AGENTS.md`.

### Acceptance evidence

- Demonstration: directly load the trial artifact and query rows that were
  served over HTTP after bind completed.
- Automated oracle: a controlled server records the request and supplies
  success, malformed JSON, non-success status, oversized body, blocking, and
  disconnect scenarios.
- Quality gates: focused native tests, a direct-load SQL/Python integration
  oracle, format and repository validation, and the existing source identity
  gate where applicable.
- Independent review: Query Experience and Remote Runtime lifecycle/security
  review plus an adversarial relational/network review before promotion.

### Contract and invariant impact

- The bounded trial may exercise the native adapter, immutable connector and
  plan, transport, decoder, and stream interfaces without declaring them
  stable.
- Bind and planning remain deterministic and offline; execution alone receives
  network authority. Destination authority is fixed, redirects, credentials,
  retries, pagination, and proxies are disabled, response and time budgets are
  hard, conversions are strict, cancellation is observed, and errors are
  redacted.
- The declarative connector specification is unaffected because no package is
  parsed or loaded.

### Team and RFC routing

- Accountable stream: Query Experience.
- Remote Runtime — Collaboration: provide the bounded HTTP-to-batch service.
  Exit when the adapter consumes a documented runtime interface without
  transport or decoder internals.
- Relational Semantics — Collaboration: preserve an immutable offline
  `ScanRequest -> ScanPlan` handoff. Exit when the runtime can execute the plan
  without reclassifying DuckDB meaning.
- Connector Experience is not an affected implementation team: the native
  connector is compiled evidence, not an authoring or package surface.
- RFC: a bounded evidence trial is authorized before deciding the durable
  network, SQL, and shared runtime contracts. Trial behavior must not be
  represented as a release compatibility promise.

### Unknowns and first trial

- Unknown: whether DuckDB's native HTTP capability can be used from the
  loadable extension with bounded body delivery, cancellation, safe failure
  translation, and deterministic local-service testing.
- Trial: one fixed unauthenticated GET and static schema through the complete
  bind, plan, HTTP, JSON, batch, and DuckDB output path.

### Delivery path

1. Prove the thin end-to-end path with a controlled HTTP oracle and directly
   loaded artifact.
2. Review the evidence and decide the smallest production contract worth
   accepting; only then graduate the trial into team-owned product modules.
3. Correct the roadmap so connector authoring and distribution follow a
   functioning remote query product rather than precede it.

## Completion record

### Delivered

Pending.

### Evidence

Pending.

### Material decisions and deviations

- Connector YAML and package distribution were removed from the active path.
  They depend on a working remote query mechanism and are not prerequisites
  for proving it.

### Product options discovered

Pending.

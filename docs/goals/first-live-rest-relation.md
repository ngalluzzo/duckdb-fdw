# Goal: First live REST relation

## PM brief

### Outcome

For a DuckDB user, enable the permanent source-built `duckdb_api` extension to
query one actual HTTPS REST relation as strictly typed relational data so that
the project delivers its central remote-query mechanism before investing in
connector authoring or distribution.

### Why now

The permanent extension still executes only an embedded fixture. A bounded
trial proved the complete network path, but trial completion is decision
evidence rather than product delivery. The highest-value next outcome is to
graduate that mechanism into topology-owned production modules and make it the
ordinary extension behavior.

### Product guardrails

- Must: deliver the live relation through permanent `src/` product code and the
  existing native DuckDB extension, not a second proof extension.
- Must: keep bind and planning offline, return strict typed rows, and provide
  bounded, redacted failure behavior.
- Preserve: immutable plans, conservative relational ownership, cancellation,
  deterministic cleanup, reproducible source builds, and topology-aligned
  module ownership.
- Must not: spend this goal on YAML, connector packages, registries, signing,
  publication, authentication, pagination, retries, caching, or GraphQL.

### Success signals

- A user can build and load the permanent extension, execute the accepted SQL,
  and receive typed rows fetched from the fixed public HTTPS relation.
- The same permanent path passes a deterministic controlled-service oracle for
  exact request, rows, failures, cancellation, and teardown.
- Product source and tests expose stable team interfaces without requiring two
  teams to co-own a monolithic module.

## Agent commitment

### Observable interpretation

The permanent `duckdb_api_scan` table function binds one compiled-in live
relation without I/O, freezes an immutable execution plan, and performs exactly
one authorized unauthenticated HTTPS request when scanning begins. Remote
Runtime enforces destination and resource policy, strictly decodes the fixed
JSON shape, and returns bounded typed batches. DuckDB retains filtering,
ordering, limit, and offset. A controlled service is the correctness oracle;
the public GitHub request is compatibility evidence only.

RFC 0005 accepts the precise preview inventory: replace the fixture relation
with `duckdb_api_scan(connector := 'github', relation :=
'duckdb_login_search_page')`, returning `id BIGINT`, `login VARCHAR`, and
`site_admin BOOLEAN`. Its complete base-row domain is the zero-to-three records
in one fixed GitHub search response page, not all GitHub users or all matching
results.

### Acceptance evidence

- Demonstration: clean-build and directly load the permanent artifact, then
  execute the live SQL against GitHub and observe three strictly typed rows.
- Automated oracle: a private non-installable composition and controlled HTTP
  service prove zero bind/prepare requests, one execution request, exact
  request metadata and rows, immutable prepared authority, DuckDB-owned
  relational operators, bounded status, redirect, malformed, oversized,
  disconnect, timeout, interrupt, recovery, and teardown behavior. Artifact
  canaries prove the installed extension contains no loopback or authority
  override seam.
- Quality gates: focused connector, planner, decoder, transport, executor, and
  adapter tests; product SQL and artifact inventories; source identity checks;
  and a fresh native product build.
- Independent review: fresh relational-correctness, transport/security,
  DuckDB-lifecycle, and test-oracle reviewers, followed by re-review of any
  material correction.

### Contract and invariant impact

- `CompiledConnector`, `ScanRequest`, `ScanPlan`, `BatchStream`, structured
  diagnostics, production composition, and the DuckDB adapter are affected.
  `docs/ARCHITECTURE.md`, `docs/RUNTIME_CONTRACTS.md`, public examples, tests,
  and the preview inventory must agree before completion.
- Bind and planning remain deterministic and offline. The plan grants only one
  fixed HTTPS GET, one attempt, strict schema conversion, and hard response,
  decode, batch, time, and concurrency budgets. It grants no credentials,
  redirects, ambient proxy behavior, pagination, retry, cache, or provider
  capability.
- Runtime validates executable capability facts but never reconstructs the
  planner's semantic proof or takes ownership of filters, ordering, limits, or
  offsets.
- The declarative connector specification remains outside the delivered path;
  the compiled-in connector is an immutable native product snapshot, not a
  package-authoring promise.

### Team and RFC routing

- Accountable stream: Query Experience.
- Connector Experience — X-as-a-Service: provide one documented immutable
  compiled connector snapshot. Exit when consumers can plan and explain it
  without YAML, runtime, or DuckDB knowledge.
- Relational Semantics — Collaboration, then X-as-a-Service: define the offline
  immutable live plan and conservative ownership proof. Exit when Query and
  Runtime consume it without duplicating semantic decisions.
- Remote Runtime — Collaboration, then X-as-a-Service: provide the bounded
  HTTP-to-batch executor. Exit when Query consumes only the protocol-neutral
  executor/stream and focused runtime oracles need no DuckDB adapter.
- Engineering Enablement — Facilitation: transfer the constrained platform
  HTTPS dependency, controlled-service, source-identity, and fresh-build
  practice. Exit when the owning teams run and maintain those gates without
  Enablement approval.
- RFC: RFC 0005 is Accepted and authorizes the public preview, shared
  interfaces, network and resource policy, lifecycle, and dependency decision.

### Unknowns and first trial

- Unknown: no mechanism-level feasibility unknown remains. Production
  dependency custody and team-interface acceptance are decision and delivery
  work covered by RFC 0005.
- Trial: completed. The live REST product proof established the fixed GitHub
  HTTPS path and controlled success/failure/lifecycle evidence on the recorded
  DuckDB 1.5.4 `osx_arm64` cell.

### Delivery path

1. Accept RFC 0005 with the preview inventory, dependency, lifecycle, and team
   interfaces explicit.
2. Land provider interfaces in dependency order, then execute Connector,
   Semantics, Runtime, and Query workstreams with disjoint file ownership.
3. Integrate the permanent composition and build graph, prove the controlled
   and public end-to-end paths, and remove the fixture-only product claim.
4. Run independent adversarial review, propagate every affected contract, pass
   the fresh product gates, and commit the coherent `0.3.0` delivery history.

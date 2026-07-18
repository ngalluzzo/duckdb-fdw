# Goal: First live REST relation

Status: **Satisfied on `main`**. The permanent `0.3.0` source-built extension,
controlled correctness oracle, public compatibility path, and topology-owned
provider graph are integrated at `f834eb0`; final Query diagnostics are included
through `ba389a9`.

## PM brief

### Outcome

For a DuckDB user, enable the permanent source-built `duckdb_api` extension to
query one actual HTTPS REST relation as strictly typed relational data so that
the project delivers its central remote-query mechanism before investing in
connector authoring or distribution.

### Why now

At activation, the permanent extension executed only an embedded fixture. A
bounded trial had proved the complete network path, but trial completion was
decision evidence rather than product delivery. The highest-value next outcome
was to graduate that mechanism into topology-owned production modules and make
it the ordinary extension behavior.

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
  The integrated `docs/ARCHITECTURE.md`, `docs/RUNTIME_CONTRACTS.md`, public
  examples, tests, and preview inventory now agree with those interfaces.
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
- Connector Experience — **Satisfied; X-as-a-Service:** the documented
  immutable native snapshot is consumed without YAML, runtime, or DuckDB
  knowledge leaking into Connector's provider API.
- Relational Semantics — **Satisfied; X-as-a-Service:** Query and Runtime
  consume the immutable offline plan without constructing or duplicating its
  relational decisions.
- Remote Runtime — **Satisfied; X-as-a-Service:** Query consumes the
  protocol-neutral executor, stream, typed batches, control, and errors, while
  focused Runtime oracles remain DuckDB-free.
- Engineering Enablement — **Satisfied; facilitation ended:** the owning teams
  run and maintain the constrained HTTPS dependency, controlled-service,
  source-identity, artifact, and fresh-build gates without Enablement approval.
- RFC: RFC 0005 is Accepted and authorizes the public preview, shared
  interfaces, network and resource policy, lifecycle, and dependency decision.

### Unknowns and first trial

- Unknown: no mechanism-level feasibility unknown remains. Production
  dependency custody and team-interface acceptance are decision and delivery
  work covered by RFC 0005.
- Trial: completed. The live REST product proof established the fixed GitHub
  HTTPS path and controlled success/failure/lifecycle evidence on the recorded
  DuckDB 1.5.4 `osx_arm64` cell.

### Delivered path

1. RFC 0005 was accepted with the preview inventory, dependency, lifecycle,
   and team interfaces explicit.
2. Provider interfaces landed in dependency order under disjoint Connector,
   Semantics, Runtime, and Query responsibility.
3. The permanent composition and build graph integrated those providers,
   proved controlled and public end-to-end paths, and retired the fixture-only
   product.
4. Independent review corrections landed, every affected contract propagated,
   and the coherent `0.3.0` graph passed reusable and fresh product gates.

## Completion record

### Delivered

The permanent source-built `duckdb_api` extension now binds and plans the fixed
`github.duckdb_login_search_page` relation without I/O, performs one bounded
authorized HTTPS request during scan execution, strictly returns `id BIGINT`,
`login VARCHAR`, and `site_admin BOOLEAN`, and leaves filtering, ordering,
limit, and offset in DuckDB. The installed artifact contains only the public
GitHub authority; a separate non-installable controlled artifact exercises the
same adapter boundary for deterministic correctness and lifecycle evidence.

### Evidence

- Fresh `make test` during the closure audit passed every focused provider and
  adapter target, 25 SQLLogicTest assertions, installed-artifact inventory, the
  controlled product's 20-request relational/lifecycle oracle, and the public
  GitHub compatibility query.
- Fresh `scripts/verify-source-identities.py` reported native Connector source
  SHA-256
  `d9cf66acedb97b0325ca9c9883afceaa91a491fe48e2f6d5d3744137f8d13e86`
  and public-contract SHA-256
  `f5d9a5c14ef603fef34bf7154ad2272e86742fec0af994aacfbfec4afe84c8e9`;
  all 11 deterministic native-dependency counterexamples passed.
- The recorded fresh `make verify PROFILE=debug` rebuilt 618 targets without
  developer-cache reuse from exact product tree
  `f9f11018fa4671faa213ff9999adc9c7c72e9689`, repeated the focused,
  controlled, SQL, artifact, dependency, TLS, and public evidence, and produced
  public artifact SHA-256
  `55371437224cee67a71f3b548643de35ce149c9f94626c25fc071a44c61f9182`
  and controlled artifact SHA-256
  `8813dff1d2a815a27bacf74c6c08012262f6126e57176e998318c88bcdb2663e`.
- Independent review corrections include final authority, lifecycle, source-
  identity, fixture-retirement, and public binder-context fixes through
  `ba389a9`; the final responsibility and interaction audit found no remaining
  provider-boundary dependency violation.

### Material decisions and deviations

- No accepted scope changed. RFC 0005's one-cell, one-relation, fixed-authority
  preview and its explicit authoring, authentication, pagination, retry, cache,
  GraphQL, publication, and multi-cell exclusions remain intact.
- Public GitHub rows remain compatibility evidence only. The controlled service
  remains the correctness oracle, and connection close remains bounded by the
  accepted five-second execution deadline rather than a stronger prompt-close
  promise.

### Product options discovered

- Declarative package authoring, additional supported platform cells,
  authentication, pagination, and broader relation inventory remain separate
  product outcomes; none is implied or activated by this completion.

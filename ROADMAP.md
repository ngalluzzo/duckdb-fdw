# Product roadmap

This roadmap describes how duckdb-fdw intends to unlock useful product
outcomes from the first executable preview through a stable `1.0.0` release.
It is the living home for release sequence and product intent; architecture,
connector, and runtime documents define durable behavior without carrying
release sequencing.

The roadmap does not activate delivery work or establish a compatibility
promise by itself. Each release outcome is shaped and activated through
`docs/PRODUCT_DELIVERY.md`, assigned through `docs/TEAM_TOPOLOGY.md`, and routed
through `docs/RFC_PROCESS.md` when it introduces or changes a durable public or
shared contract.

## Planning rules

- Releases unlock coherent user or connector-author value, not percentages of
  component completion.
- Versions are evidence gates, not dates. A release ships only when its
  observable outcome and relevant correctness, security, lifecycle, and
  compatibility evidence pass.
- The ordered releases are the intended progression. Product learning may
  revise an unreleased outcome through the normal product process; published
  versions and artifacts never change.
- Patch releases may occur between the releases shown here. The roadmap names
  planned product outcomes, not every maintenance release.
- The roadmap does not use `MVP` labels. Each release states what becomes
  meaningfully usable and the evidence required to make that claim.
- Scope not explicitly promised by a release remains uncommitted, even when a
  design document explores it.

## Versioning model

The project follows Semantic Versioning 2.0.0 and always writes complete
versions such as `0.1.0` and `1.0.0`.

During initial development:

- `0.Y.0` introduces a coherent preview outcome or an incompatible change to a
  previously published public surface.
- `0.Y.Z` contains backward-compatible correctness or security fixes only.
- Every incompatible change includes release notes and migration guidance,
  even though the project has not reached `1.0.0`.
- Only the latest `0.Y` release line is supported unless release notes
  explicitly state otherwise.

Beginning with `1.0.0`:

- a patch release contains backward-compatible fixes;
- a minor release contains backward-compatible additions or deprecations; and
- a major release contains an incompatible public change or removal.

The project uses `1.0.0-rc.N` for immutable release-candidate artifacts and
`vX.Y.Z` for Git tags. A release has one authoritative project version shared
by source metadata, extension metadata, its tag, artifact manifest, and release
notes. Conventional Commits inform release notes but do not determine version
changes without compatibility review.

Four version domains remain distinct:

| Domain | Contract |
| --- | --- |
| Project and extension version | SemVer for the documented duckdb-fdw product surface and observable behavior |
| Connector specification version | Compatibility identifier for the declarative language, currently `duckdb_api/draft` |
| Connector package version | Independent SemVer for one connector's relations, inputs, policies, schemas, and upstream adaptations |
| DuckDB compatibility | Tested matrix of DuckDB release, integration profile, platform, architecture, and installation mode |

DuckDB compatibility metadata is recorded in the artifact manifest and support
matrix. It is not encoded as a substitute for the project version. Any future
public native, WASM, Rust, or columnar ABI requires its own explicitly versioned
contract; internal Rust interfaces are not public merely because the project
reaches `1.0.0`.

## Release progression

### `0.1.0` — first trustworthy query

A DuckDB user can build and locally load the extension, query one static REST
relation backed by a deterministic fixture, and receive correct bounded
results. Binding performs no network I/O. Execution can be canceled and closed
safely, and at least one representative failure is meaningful and redacted.

Release evidence includes a clean build and load on one declared DuckDB and
platform target, an end-to-end query oracle, bounded execution, cancellation
and teardown checks, version introspection, an inventory of the preview public
surface, and immutable release artifacts.

### `0.2.0` — reproducible source build

A contributor can build a byte-reproducible extension artifact in a clean
environment, install it into an isolated DuckDB home, load it by name, identify
its version, and execute the `0.1.0` query on the first declared compatibility
target. An incompatible DuckDB host, platform, or artifact fails with an
actionable diagnostic.

This release proves artifact reproducibility and compatibility enforcement; it
does not require publication through an ordinary-user channel. Local unsigned
loading remains explicitly a contributor or controlled-preview path. The
selected ordinary-user distribution, signature, and trust path is exercised
only after the remote-query product has earned a public surface.

Release evidence includes an immutable source ref, artifact checksums and
provenance, exact DuckDB and platform identities, clean installation, restart,
load-by-name, version, query, byte-reproduction, and incompatible-cell tests on
every claimed row. Before `1.0`, fixes move forward without a backport
commitment. Project releases are immutable and support is best-effort through
GitHub Issues.

### `0.3.0` — first live REST relation

A DuckDB user can query one native, compiled-in, unauthenticated HTTPS REST
relation and receive strictly typed rows through the complete offline-bind,
plan, transport, JSON-decode, bounded-batch, and DuckDB-output path. This
release proves the central remote-query mechanism before declarative authoring
or package distribution becomes product work.

Release evidence includes a deterministic controlled HTTP oracle, one current
public-service compatibility demonstration, exact request and typed-row
checks, immutable prepared plans, post-DNS destination policy, hard response
and time budgets, cancellation, teardown, redacted status/network/decode
failures, and proof that bind and planning perform no network I/O. The native
connector identity and SQL spelling remain preview surfaces until the public
API candidate.

### `0.4.0` — safe authenticated APIs

A connector can access an authenticated HTTPS API without granting ambient
credential or network authority. Users can configure approved connections and
secrets, while connectors narrow rather than widen host policy. Failures do not
disclose secret material.

Release evidence covers authentication placement, allowed hosts, redirects,
DNS and resolved-address policy, response and decompression limits, memory and
deadline budgets, secret redaction, and deterministic denial and exhaustion
paths.

### `0.5.0` — bounded real-world traversal

A user can query multi-page remote relations under ordinary latency and rate
limits without unbounded buffering or runaway work. Pagination is sequential
unless independence is proven. Retries occur only when replay is declared safe
and no part of the replay unit has been committed.

Release evidence covers multi-page fixtures, backpressure, cancellation,
deadlines, rate limiting, resource accounting, replay-safe retry, partial
failure, and scan closure.

### `0.6.0` — relational trust

DuckDB users can apply projections, supported predicates, ordering, and limits
without changing query meaning. Pushdown distinguishes exact, superset, and
unsupported behavior; every residual has exactly one owner; unavailable
adapter capabilities fall back conservatively; and explain output truthfully
describes remote and local work.

Release evidence includes DuckDB-equivalence and property tests, strict schema
and value conversion, operation-selection oracles, residual ownership checks,
safe ordering and limit behavior, and capability-profile fallbacks.

### `0.7.0` — protocol breadth

The declarative connector model supports the retained product claim across
REST and GraphQL without bypassing the shared planning, policy, lifecycle, or
runtime boundaries. If evidence shows that this cannot provide a coherent
product experience, the product claim is narrowed explicitly before release.

Release evidence includes representative REST and GraphQL connectors, a
deterministic corpus of protocol and error variations, consistent diagnostics,
and proof that neither protocol requires bespoke authority or lifecycle paths.

### `0.8.0` — production-shaped workflows

Users can employ remote relations in ordinary analytical workflows such as
joins, materialization, export, and repeated scans. Operators can register and
reload connectors without changing in-flight snapshots, and extension
lifecycle paths terminate cleanly under success, cancellation, and failure.

Release evidence covers analytical workflow narratives, registration
collisions, atomic reload, immutable in-flight plans, queue draining, worker
joining, shutdown, FFI panic containment, and leak and resource-exhaustion
checks across the declared compatibility matrix.

### `0.9.0` — public connector authoring and API candidate

A connector author can validate, compile, explain, fixture-test, and load a
versioned declarative connector package against the product mechanisms proven
in earlier releases. Equivalent inputs produce equivalent compiled output and
diagnostics. Unsupported connector-spec versions or extractor dialects fail
explicitly rather than being silently reinterpreted.

At the same time, the intended `1.0.0` public contract is enumerated and frozen
for compatibility testing. It includes normative SQL and extension naming,
configuration, diagnostics, explain and version surfaces, a stable
connector-spec candidate, migration and deprecation rules, supported
compatibility cells, the chosen distribution path, and explicit exclusions.
No intentional public break remains planned.

Release evidence includes a schema-backed validator, source-located
diagnostics, deterministic compile and explain oracles, offline fixtures,
package digest identity, connector package SemVer guidance, the public API
inventory, connector-spec migration fixtures, prior-package and prior-SQL
compatibility tests, a green declared DuckDB support matrix, installation and
upgrade narratives, release and support policies, and reproducible artifacts.

### `1.0.0-rc.N` — compatibility rehearsal

Each release candidate is an immutable build of the frozen `1.0.0` contract.
The exact artifacts complete clean-install, upgrade, migration, relational,
security, lifecycle, failure-path, and support or backport rehearsals. A later
candidate is a new version; an existing candidate is never rebuilt or relabeled
as final.

### `1.0.0` — narrow stable contract

The declared public surface is stable and supportable. Every promised
compatibility-matrix row passes the complete release gate, unsupported
combinations fail clearly, and installable artifacts, release notes, migration
guidance, checksums, and provenance are reproducible and immutable.

The intended stable boundary is:

- the portable DuckDB integration profile using the proven stable C Extension
  API subset;
- documented SQL functions, arguments, relation schemas, configuration,
  connection and secret behavior, diagnostics, explain, version
  introspection, and cancellation semantics;
- a non-draft static-schema connector specification with explicit validation
  and compatibility rules;
- REST and GraphQL support if the product claim survives `0.7.0` evidence;
- observable relational, security, resource, replay, and lifecycle guarantees;
  and
- installation and updates through a supported, trust-qualified distribution
  path on a published DuckDB, profile, and platform matrix.

Unless separately accepted and proven, the `1.0.0` guarantee excludes:

- the deep C++ catalog and optimizer integration profile;
- public Rust, native plugin, WASM, custom-protocol, custom-pagination, or
  columnar binary ABIs;
- internal types and traits such as `CompiledConnector`, `ScanRequest`,
  `ScanPlan`, `BatchStream`, protocol service interfaces, runtime queues, and
  cache layout;
- connector registries, dependency resolution, lockfiles, package signing, and
  package trust infrastructure;
- dynamic schemas, write-back, transactions, and continuous streams; and
- compatibility with arbitrary upstream API drift.

## Release gate

Every published release must have:

1. an activated product goal with observable acceptance evidence;
2. accepted RFCs and propagated contracts for every triggered durable
   decision;
3. a reviewed public-surface change classification and the corresponding
   version increment;
4. deterministic correctness, security, lifecycle, and failure-path evidence
   proportional to the change;
5. green results for every compatibility-matrix cell called supported;
6. curated release notes and migration guidance for every incompatible change;
7. a clean, reproducible build tied to one immutable source commit and tag;
8. artifact identity, checksums, provenance, and a published support status;
   and
9. no unresolved design question inside the release's stated public outcome.

Release automation must reject a moving or reused tag, conflicting version
sources, replacement of an existing artifact, an unsupported matrix claim, or
missing compatibility and migration evidence.

## Decisions resolved through the release-governance RFC

Before the roadmap can establish compatibility policy rather than planning
intent, the release-governance RFC must decide:

- the exact inventory of public surfaces governed by project SemVer;
- the stable successor to `duckdb_api/draft` and its relationship to project
  and connector-package versions;
- the rolling DuckDB release, integration-profile, platform, architecture, and
  minimum-Rust-version support policy;
- which support removals are incompatible and which follow a published rolling
  window;
- the distribution, signing, trust, licensing, and update path;
- the deprecation, migration, security-response, maintenance, and backport
  policies; and
- the final `1.0.0` inclusions and experimental exclusions.

Engineering Enablement sponsors that non-product RFC. Connector Experience,
Query Experience, and Remote Runtime review their affected surfaces.
Relational Semantics supplies correctness arguments and release oracles when a
release changes query behavior. Product-manager approval is required for the
public compatibility boundary, support promise, distribution direction, and
licensing choices.

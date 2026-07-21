# 1.0.0 public contract freeze (candidate)

This directory holds the enumerated `1.0.0` public contract, frozen by the
`0.9.0` release for compatibility rehearsal (`1.0.0-rc.N`) and publication
(`1.0.0`). `release/1.0.0/freeze.json` is the machine-checkable declaration;
this document is the readable enumeration. A connector author or reviewer can
read either without inspecting product source.

> **Candidate revision history.** The candidate was produced by the `0.9.0`
> release (2026-07-21). After publication, accepted RFCs may add entries under
> `accepted_candidate_revisions` (see that section below) without re-cutting
> the snapshot; the schema-closed sets and explicit exclusions do not move
> until a later release graduates a candidate. The first such revision was
> [RFC 0016](../docs/rfcs/0016-decide-body-signaled-rest-pagination.md)
> (Accepted 2026-07-21), adding `response_next` REST pagination pending
> `0.10.0` implementation.

The `1.0.0` contract is not a single document. It is a layered set in which
each layer draws authority from the one above it:

> `AGENTS.md` invariants → system-design contracts (current truth) → accepted
> RFCs (rationale) → `release/public-surface/inventory.json` (SQL oracle) →
> per-release `pins.json` / `public_contract.json` (identity and evidence).

An RFC records why a direction was chosen; it never becomes the sole source of
current behavior. This freeze cross-references the authoritative sources
rather than duplicating them.

## Authority

- `ROADMAP.md` section *1.0.0 - narrow stable contract* states the outcome.
- **RFC 0009** sets the intended stable boundary: the native C++ table-function
  profile, the six SemVer-governed categories, the narrow stable connector-
  package subset, the local-versus-central registry distinction, and the
  dependency-ordered release path. It carries forward RFC 0004's durable
  choices (MIT license, DuckDB Community Extensions as the ordinary-user
  channel, source build as the contributor path, immutable releases, the
  latest-stable DuckDB requirement, best-effort GitHub Issues support).
- RFCs 0010, 0011, 0012, 0013, and 0014 bind specific decisions propagated
  into `docs/ARCHITECTURE.md`, `docs/CONNECTOR_SPECIFICATIONS.md`, and
  `docs/RUNTIME_CONTRACTS.md`.

## Four version domains (must remain distinct)

| Domain | Contract | Authority |
| --- | --- | --- |
| Project and extension | SemVer for the documented product surface and observable behavior | `ROADMAP.md` Versioning model |
| Connector specification | Compatibility identifier for the declarative language: `duckdb_api/v1` | RFC 0013 |
| Connector package | Independent SemVer for one connector's relations, inputs, policies, schemas, upstream adaptations | RFC 0013 |
| DuckDB compatibility | Tested matrix of DuckDB release, profile, platform, architecture, installation mode | `ROADMAP.md`; evidence-derived at `0.9.0` |

Project SemVer never silently absorbs the connector-spec identifier, a package
version, or a DuckDB release. Internal Rust/C++ types are not public merely
because the project reaches `1.0.0`.

## The six SemVer-governed categories (RFC 0009)

### 1. Extension identity, installation, and version introspection

- Frozen: extension name `duckdb_api`; installation and update through the
  supported distribution channel; version introspection surfaces.
- Authority: `docs/ARCHITECTURE.md` Product surface and Compatibility and
  support boundary; `ROADMAP.md` Versioning model.
- Machine evidence: `extension_config.cmake`; per-release
  `release/<version>/pins.json` `project` block;
  `scripts/verify-source-identities.py`.

### 2. SQL functions, arguments, relation schemas, and query-visible lifecycle

- Frozen: nine active `system.main` table functions after `0.9.0` removes the
  deprecated `duckdb_api_scan` dispatcher; generated `<connector_id>_<relation_id>`
  naming; the two management calls (`duckdb_api_load_connector`,
  `duckdb_api_reload_connector`); the three read-only introspection functions;
  relation inputs as named arguments; the reserved required `secret VARCHAR`
  argument for authenticated relations; bind/describe/explain/prepare offline;
  cancellation and publication lifecycle.
- Authority: `docs/ARCHITECTURE.md` Product surface, Query binding and
  planning, Atomic catalog publication; RFC 0012.
- Machine oracle: `release/public-surface/inventory.json` (content-addressed
  shapes and per-release lifecycle), cross-checked by
  `release/public-surface/query-contract.json`, verified by
  `scripts/verify-public-surface-inventory.py`.

### 3. Diagnostics, safe fields, and plan explanation

- Frozen: the closed `DUCKDB_API_*` diagnostic code vocabulary; package-
  relative source coordinates; the redaction rule (absolute roots, source
  scalars, credentials, generated documents, request/response bodies, rows,
  cursors, and remote messages are never diagnostic fields); explain fields
  derived from typed immutable facts.
- Authority: `docs/ARCHITECTURE.md` Diagnostics and explanation;
  `docs/CONNECTOR_SPECIFICATIONS.md` Diagnostics; RFCs 0010 and 0013.

### 4. Connector specification and stable package subset

- Frozen: identifier `duckdb_api/v1`; the closed failsafe YAML subset and
  byte-copied JSON schemas; static `BOOLEAN`/`BIGINT`/`VARCHAR` typing; the
  `sha256-length-prefixed-path-and-bytes-v1` package digest; REST `GET` and
  structured GraphQL Relay profiles; anonymous and capability-scoped bearer
  authentication; deny-only network policy; positive resource ceilings;
  reload compatibility rules; offline fixture identity; the closed pagination
  strategy sets (REST `{disabled, link_next}`, GraphQL `{relay_forward}`).
- Authority: `docs/CONNECTOR_SPECIFICATIONS.md`; RFC 0013.
- Machine oracle: `src/connector/package/assets/connector-package-v1.schema.json`;
  `release/<version>/pins.json` `identities.repository_connector_package`.

### 5. Observable relational, security, resource, replay, and lifecycle guarantees

- Frozen: DuckDB owns correctness; safe pushdown requires `D -> R` and exact
  pushdown additionally requires `R -> D` over the occurrence bag; every
  residual has exactly one owner; limit and offset apply only after required
  filtering and ordering; ordinary bind and planning perform no network I/O;
  connector policy narrows host policy and never widens it; sequential
  pagination unless independence and consistency are proven; one attempt with
  replay safety declared; checked unsigned resource arithmetic; immutable
  plans and snapshots for active scans; strict lossless conversion; atomic
  catalog publication with `Runtime lease -> Query catalog` lock order;
  cancelable, idempotent close; dynamic DSO unload unsupported.
- Authority: `docs/ARCHITECTURE.md` Relational correctness, Execution and
  authorization, Bounded streaming lifecycle; `docs/RUNTIME_CONTRACTS.md`;
  RFCs 0006, 0007, 0008, 0010, 0011.

### 6. Compatibility matrix, distribution, support, migration, and exclusions

- Frozen policy (rows evidence-derived at `0.9.0`, see *Not yet frozen*
  below): DuckDB Community Extensions is the ordinary-user channel; source
  build is the contributor path; the initial stable release supports the
  latest stable DuckDB release and only exact passing Community platform rows;
  MIT license; best-effort, latest-release-only support with no LTS and no
  backports; breaking changes require a project-SemVer MAJOR increment with
  same-release curated notes and migration guidance.
- Authority: RFC 0009 (carry-forward of RFC 0004); RFC 0014; `SECURITY.md`;
  `ROADMAP.md` Release gate and Release-governance decision.

## Explicit exclusions

The following are outside `1.0.0` unless a later accepted contract and
pre-freeze evidence add them. Implementations reject unsupported declarations
rather than silently ignoring them.

- Deep native catalog and optimizer integration profile; any Rust or stable-C
  API replatform; public Rust, native plugin, WASM, custom-protocol,
  custom-pagination, or columnar binary ABIs.
- Internal C++ types and traits (`CompiledConnector`, `ScanRequest`,
  `ScanPlan`, `BatchStream`, protocol service interfaces, runtime queues,
  cache layout) as a public surface.
- Central connector discovery or distribution registry; Git fetching;
  dependency resolution and lockfiles; connector-package signing or trust
  infrastructure (accountability exists via RFC 0015; capability is post-v1).
- Tier 2 JQ-compatible transforms; Tier 3 native or WASM custom code; column
  providers; partitions; automatic retry or rate-limit waiting;
  author-configurable cache or single-flight behavior.
- OpenAPI or GraphQL introspection importers; authenticators beyond anonymous
  and capability-scoped bearer; dynamic schemas; write-back, transactions,
  continuous streams; raw GraphQL documents; author remote projection, order,
  or limit pushdown declarations.
- **Pagination strategies outside the closed sets:** response-body-embedded
  next URLs (for example `info.next`), numeric offset or page-number
  traversal, cursor-in-body strategies, and reverse or bidirectional
  traversal. Admitting any such strategy requires a later accepted RFC and
  pre-freeze evidence; the compiler rejects the declaration today as
  `DUCKDB_API_UNSUPPORTED_DECLARATION` in the schema phase. **Carve-out
  accepted by [RFC 0016](../docs/rfcs/0016-decide-body-signaled-rest-pagination.md):**
  the narrow body-URL reconstruct-and-verify shape named `response_next` is
  now an accepted candidate revision pending `0.10.0` implementation (see the
  *Accepted candidate revisions pending implementation* section below). The
  broader category — offset/page-number traversal, cursor-in-body strategies,
  and reverse or bidirectional traversal — remains a permanent exclusion.
- Compatibility with arbitrary upstream API drift.

## Known evidence limitations recorded during this freeze

- **End-to-end fixture execution is not wired.** `PackageFixtureExecutionService`
  has no concrete Semantics/Runtime integration. Fixtures today prove schema
  validation, exact coverage-key agreement, and payload-digest agreement for
  both repository packages, but do not execute expected rows or diagnostics
  against real compiled behavior. The freeze proceeds on the coverage and
  digest evidence basis; real end-to-end fixture execution is a recorded
  post-freeze increment, not a `1.0.0` blocker. See the
  `docs/goals/public-connector-authoring-candidate.md` completion record.

## Accepted candidate revisions pending implementation

This section lists decisions accepted by an RFC after `0.9.0` produced the
candidate freeze but before their implementation graduates into the
schema-closed contract surface. It is distinct from the categories around it:

- `exclusions` are permanent — features outside the project's scope that the
  boundary definition itself rejects.
- `fast_follows` are discovered gaps — work the freeze surfaced but deliberately
  deferred without blocking the freeze.
- `not_yet_frozen` are evidence-derived rows — items that will be enumerated
  from passing evidence rather than decided (e.g. the compatibility matrix).
- **`accepted_candidate_revisions` are decided futures** — a contract change
  an accepted RFC has authorized for a named target release. The decision is
  locked in (no longer exclusion, no longer open); only the implementation and
  its evidence remain. Each entry records the authority, the target release,
  the rule for graduating it into the closed set, and — crucially — whether
  the broader exclusion category it partially overlaps remains in force.

`release/1.0.0/freeze.json` mirrors this section under
`accepted_candidate_revisions`. The freeze gate
(`scripts/verify-contract-freeze.py` plus `test/python/contract_freeze_tests.py`)
asserts each entry has the required structure and fails closed if the section
is removed, an entry is dropped, or the schema-closed set is widened without a
graduation.

### `response_next` REST pagination (target: `0.10.0`)

Accepted [RFC 0016](../docs/rfcs/0016-decide-body-signaled-rest-pagination.md)
(2026-07-21) adds a third REST pagination strategy, `response_next`,
architecturally identical to `link_next` except its continuation signal is read
from a declared JSON body path (`next_url_path`) rather than an HTTP `Link`
header, reusing the same reconstruct-and-verify safety model. The product
manager (Nic Galluzzo) directed this acceptance after re-prioritizing `1.0.0`
to require ≥10 supported API providers (two exist as of `0.9.0`), dissolving
the freeze-pressure rationale that originally deferred the design to a
post-`1.0.0` `MINOR`. The five-team topology re-consultation produced five
`Approved` dispositions.

Until `0.10.0` ships:

- `pagination_strategies.rest` stays at `{disabled, link_next}` (this is what
  the JSON schema's closed `oneOf` actually enforces).
- A package declaring `response_next` fails closed at the `SCHEMA` phase with
  `DUCKDB_API_UNSUPPORTED_DECLARATION`, exactly as the freeze's
  `rejected_diagnostic` advertises.
- The broader exclusion `pagination_body_url_offset_or_cursor_in_body_strategies`
  **remains mandatory** — only the body-URL reconstruct-and-verify shape is
  accepted by RFC 0016. Numeric offset/page-number traversal, cursor-in-body
  strategies, and reverse or bidirectional traversal still require their own
  later accepted RFC.

When `0.10.0` ships, the entry graduates: a new freeze snapshot for `0.10.0`'s
release view moves `response_next` into `pagination_strategies.rest`, the
schema's `oneOf` adds the corresponding branch with live decoder/IR/switch
coverage, and the candidate-revision entry is removed because the strategy is
now part of the closed set proper.

## Not yet frozen

These are `1.0.0` inputs that `0.9.0` derives from evidence rather than
decides:

- **The supported DuckDB / profile / platform / architecture / installation
  matrix rows.** Evidence-derived at `0.9.0` per the `ROADMAP.md` Release-
  governance decision; not enumerable here.
- **`release/1.0.0/pins.json` and `release/1.0.0/public_contract.json`.**
  Produced at `1.0.0` shipment after matrix derivation and `1.0.0-rc.N`
  rehearsal. The detailed per-release contract shape is exemplified by
  `release/0.8.0/public_contract.json`.

## How this freeze is verified

- `release/1.0.0/freeze.json` is the machine-checkable declaration.
- `test/python/contract_freeze_tests.py` cross-checks it against the
  authoritative sources: the SQL surface matches the inventory's `0.9.0`
  release view exactly; the connector-spec identifier matches the schema
  const; the pagination strategy sets match the schema's closed `oneOf`; the
  RFC citations resolve to accepted decisions; the
  `accepted_candidate_revisions` entries each carry the required structure
  (id, scope, status, authority, target_release, graduation_rule) and fail
  closed if removed or structurally weakened; and mutation cases prove a
  removed, added, or altered declaration fails closed.
- The existing `scripts/verify-public-surface-inventory.py` gate remains the
  SQL-surface compatibility oracle. This freeze references it; it does not
  replace it.

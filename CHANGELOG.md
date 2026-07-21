# Changelog

This file records user-visible changes to duckdb-fdw.

## Unreleased

## 0.9.0 — 2026-07-21

### Removed

- `duckdb_api_scan(connector := ..., relation := ...)`, the deprecated
  migration surface for the four `0.7.0` built-in GitHub relations. Accepted
  RFC 0012 scheduled this removal for `0.9.0`, before the public API
  candidate freezes. Load a package and use its generated functions instead;
  see the [0.8.0 release notes](docs/releases/0.8.0-notes.md#migration-from-070)
  for the mapping from each removed call.

### Added

- A second, independently authored `duckdb_api/v1` connector package
  (test-only, not distributed with the extension) proving the compiler,
  planner, and generated-function surface depend only on the v1 contract
  with no cross-package coupling.
- The intended `1.0.0` public API candidate — SQL surface, diagnostics
  vocabulary, connector-package contract, and compatibility boundary — is
  enumerated and frozen in `release/1.0.0/freeze.json` for compatibility
  testing.

## 0.8.0 — 2026-07-20

### Added

- Explicit local package loading through
  `duckdb_api_load_connector(package_root := ...)`. An accepted
  `duckdb_api/v1` package is compiled into an immutable generation and
  atomically publishes one typed table function per declared relation.
- Generated relation names use `<connector_id>_<relation_id>`, with declared
  relation inputs as named arguments and the reserved `secret VARCHAR`
  argument on authenticated relations.
- `duckdb_api_reload_connector(connector := ...)`, which recompiles the active
  connector from its retained canonical root and publishes only a compatible
  changed generation.
- Read-only `duckdb_api_loaded_connectors()`,
  `duckdb_api_loaded_relations()`, and `duckdb_api_relation_arguments()`
  introspection without exposing package roots or credentials.
- A maintained local package at `connectors/github` containing four generated
  relations across anonymous REST, authenticated REST, bounded Link
  pagination, conservative predicate restriction, and GraphQL cursor
  pagination.
- General source-neutral REST and GraphQL execution for compiler-produced
  plans, including exact origin and port admission, bounded request
  materialization, sequential pagination, cancellation, and close behavior.
- Source-located package diagnostics and deterministic rejection of malformed,
  incompatible, duplicate, unsafe, or canceled publication attempts without
  changing the active catalog generation.
- Product examples that load the permanent GitHub package before querying its
  generated functions. Authenticated repository examples retain privacy-safe
  output and interactive-only credential input.

### Changed

- The extension identity advances to `0.8.0`.
- The primary developer workflow is now extension load, connector-package
  load, then generated relation functions. Connector and relation identity are
  captured by the registered function instead of supplied on every scan.
- Package generations are retained across catalog publication and active
  scans. Identical reloads report `changed = false`; rejected reloads preserve
  the previous functions and generation.
- REST and GraphQL requests are derived from compiled package declarations and
  immutable relational plans. SQL cannot provide a URL, header, document,
  destination, pagination cursor, or credential value.
- DuckDB continues to own relational correctness. Unsupported or ambiguous
  predicate translation falls back conservatively, and any required residual
  remains DuckDB-owned.

### Deprecated

- `duckdb_api_scan(connector := ..., relation := ...)` remains available only
  for migrating the four `0.7.0` built-in GitHub relations. It is not a loaded
  package, does not appear in package introspection, and is scheduled for
  removal before the public API candidate is frozen.

### Limitations

- Packages load only from an explicit absolute local directory. Discovery,
  registries, remote installation, updates, signatures, and trust policy are
  not included.
- The extension remains an unsigned source build for the exact supported
  DuckDB 1.5.4 macOS arm64 product cell.
- Authentication remains limited to explicitly named temporary bearer secrets.
  There are no persistent or environment-backed providers, OAuth, implicit
  secret selection, retries, rate-limit waits, parallel page requests, resume
  state, or caching.

## 0.5.0 — 2026-07-18

### Added

- The fixed `github.authenticated_repositories` relation, returning required
  `id BIGINT`, `full_name VARCHAR`, `private BOOLEAN`, `fork BOOLEAN`, and
  `archived BOOLEAN` values across one bounded authenticated GitHub repository
  page chain.
- Deterministic controlled-product evidence for three-page traversal, an empty
  middle page, single-page exhaustion, duplicate preservation, DuckDB-local
  relational operators, exact request reconstruction, late status/decode/schema
  failures, hostile Link metadata, aggregate exhaustion, cancellation, early
  close, redaction, and recovery.
- A privacy-safe live example that reports schema and aggregate repository count
  without recording repository rows, Link values, or credential material.

### Changed

- The native connector snapshot and installed extension identity advance to
  `0.5.0`; both `0.4.0` relations retain their accepted behavior and resource
  narrowings.
- Remote execution now supports one closed sequential Link-pagination profile.
  It accepts only the next page of the fixed GitHub operation, reconstructs the
  request from typed plan state, reuses one scan-scoped bearer capability, and
  returns nonempty typed batches until clean exhaustion.
- Permanent source and tests are split by Connector, Relational Semantics,
  Remote Runtime, and Query responsibilities, with focused build targets for
  pagination declarations, plans, Link parsing, aggregate accounting,
  root-array decoding, retained page buffers, transport metadata, execution,
  and stream consumption.

### Limitations

- Repository rows form a duplicate-preserving bag from a mutable source. Remote
  order and snapshot consistency are not guaranteed; a local `ORDER BY` is
  required for deterministic presentation.
- Traversal is limited to 32 sequential pages and requests, 3,200 records, 64
  MiB wire/decompressed bytes, 512 KiB response headers, 2 MiB retained decoded
  page memory, one active request, and 30 seconds. A declared next page beyond a
  ceiling fails the statement rather than returning a successful partial result.
- There are no retries, rate-limit waits, parallel pages, resume state,
  caller-selected page inputs, caching, provider expansion, or declarative
  connector loading.

## 0.4.0 — 2026-07-18

### Added

- The fixed `github.authenticated_user` relation, returning one required
  `id BIGINT`, `login VARCHAR`, and `site_admin BOOLEAN` row for the current
  GitHub bearer-token principal.
- A temporary-only `duckdb_api` secret type with the `config` provider and one
  redacted, nonempty `TOKEN VARCHAR` field. The table function accepts the
  explicit logical name through `secret VARCHAR` only for the authenticated
  relation.
- Deterministic controlled-product evidence for exact bearer placement,
  offline bind/describe/explain/prepare, prepared replacement and drop,
  concurrent credential isolation, `401`/`403`, redirect denial, redaction,
  cancellation, close, and recovery.

### Changed

- The native connector snapshot and installed extension identity advance to
  `0.4.0`; the anonymous `github.duckdb_login_search_page` relation remains
  available without a secret and rejects a supplied secret.
- Each authenticated execution resolves the named secret from DuckDB's
  temporary `memory` storage. Persistent-only entries are not found and
  same-name persistent entries are ignored.
- Bearer tokens are limited to 8 KiB, and project-supplied outbound request
  fields have a 16 KiB aggregate ceiling. Over-limit secret creation,
  resolution, capability construction, or request decoration fails before
  network I/O with a redacted `resource/header_bytes` diagnostic.

### Limitations

- Authentication is limited to the fixed bearer-authenticated `GET /user`
  operation at `https://api.github.com:443`. There is no implicit secret
  selection, persistent or environment provider, token argument, OAuth,
  caller-selected URL or header, redirect, pagination, retry, or cache.
- Secret plaintext necessarily exists in the user's creation statement,
  DuckDB's temporary secret entry, and transient request buffers. The release
  does not claim hostile-process protection or secure memory zeroization.

## 0.3.0 — 2026-07-18

### Added

- A source-built live REST preview for
  `duckdb_api_scan(connector := 'github', relation :=
  'duckdb_login_search_page')` on the exact supported DuckDB 1.5.4 macOS arm64
  product cell.
- Strict `id BIGINT`, `login VARCHAR`, and `site_admin BOOLEAN` extraction from
  one fixed, unauthenticated GitHub search response page, with hard network,
  response, decode, memory, batch, time, and concurrency bounds.
- A deterministic private controlled-service oracle for request identity,
  typed rows, DuckDB-owned relational operators, failures, interruption, and
  teardown, plus a public GitHub compatibility demonstration.

### Changed

- The `example.items` fixture relation is removed and replaced by the bounded
  `github.duckdb_login_search_page` preview. Queries using the old identifiers
  now fail during bind and must migrate to the new connector, relation, and
  three-column schema.
- Bind, `DESCRIBE`, and `PREPARE` remain offline; live network authority is
  acquired only when a scan executes.

### Limitations

- This release contains one compiled-in fixed public relation, not general
  connector loading or a stable connector-authoring contract.
- Public GitHub row identity, response order, availability, and anonymous rate
  limits are outside the deterministic behavior contract.
- Authentication, pagination, retries, caching, GraphQL execution, arbitrary
  URLs, binary publication, signing, installation, and updates remain
  unsupported.

## 0.1.0 — 2026-07-17

### Added

- A source-built native `duckdb_api` preview for the exact DuckDB 1.5.4 macOS
  arm64 compatibility cell.
- The `duckdb_api_scan(connector := 'example', relation := 'items')` table
  function over an embedded deterministic fixture, returning three typed rows.
- A root `make` workflow for reusable developer builds, tests, paths, and the
  first-query demo, plus a fresh pre-tag verification target.
- A direct unsigned-load example using the pinned DuckDB Python host, with
  extension identity, schema, row, and redacted failure oracles.

### Limitations

- This is a fixture-backed native preview for one exact compatibility cell,
  not a general-purpose connector or portable binary distribution.
- There is no live HTTP, authentication, pagination, GraphQL, arbitrary
  connector loading, binary installation, signing, or update support.
- Compatibility is limited to the exact product cell documented in the
  [0.1.0 release notes](docs/releases/0.1.0-notes.md).
- Developer artifacts and `make verify` output are not substitutes for the
  clean tagged release evidence required by the
  [0.1.0 evidence runbook](docs/releases/0.1.0.md).

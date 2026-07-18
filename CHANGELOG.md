# Changelog

This file records user-visible changes to duckdb-fdw.

## Unreleased

### Added

- The project is now distributed under the MIT License.

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

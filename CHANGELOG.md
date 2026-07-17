# Changelog

This file records user-visible changes to duckdb-fdw. The project has not yet
published a release.

## Unreleased — 0.1.0 candidate

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

- This is an unreleased, fixture-backed candidate; no `v0.1.0` artifact has been
  published.
- There is no live HTTP, authentication, pagination, GraphQL, arbitrary
  connector loading, binary installation, signing, or update support.
- Compatibility is limited to the exact product cell documented in the
  [0.1.0 candidate notes](docs/releases/0.1.0-notes.md).
- Developer artifacts and `make verify` output are not substitutes for the
  clean tagged release evidence required by the
  [0.1.0 evidence runbook](docs/releases/0.1.0.md).

# Connector Experience plan: native live relation metadata

## Outcome and status

Provide the permanent product with one deterministic, immutable
`CompiledConnector` snapshot for the bounded
`github.duckdb_login_search_page` relation accepted by RFC 0005. This
workstream owns the compiled declaration and its direct oracle only. It does
not activate declarative authoring, compile YAML, grant network authority, or
implement planning, execution, DuckDB integration, or distribution.

The provider interface lands before its consumers so Relational Semantics can
compile against the exact metadata contract without reconstructing GitHub
constants.

## Responsibility map and owned files

| Artifact | Connector Experience responsibility |
| --- | --- |
| `src/include/duckdb_api/connector.hpp` | Private native `CompiledConnector` team API: stable identifiers, schema and nullability, structural REST request fields, fixed headers, response extractor, replay and feature declarations, connector policy narrowing, resource ceilings, native provenance, immutability expectations, and compatibility boundary |
| `src/connector.cpp` | Sole production construction of the canonical `native_product_metadata` snapshot and stable, complete explanation of every connector-owned field |
| `test/cpp/connector_contract_tests.cpp` | Exact field, ordering, policy, provenance, schema, constructor determinism, and snapshot oracles independent of DuckDB, planning, runtime, transport, YAML, or a live service |

No other production, test, build, fixture, release, or shared-document file is
owned by this workstream. In particular, the workstream does not edit CMake,
`ScanRequest`, `ScanPlan`, executor/stream types, decoding, HTTP policy
enforcement, composition, the DuckDB adapter, source-identity pins, or
authoritative shared contracts.

## Exact provided contract

The no-argument native builder returns exactly one connector snapshot with:

- origin `native_product_metadata`, connector `github`, metadata version
  `0.3.0`, and relation `duckdb_login_search_page`;
- required non-null columns `id BIGINT <- $.id`,
  `login VARCHAR <- $.login`, and
  `site_admin BOOLEAN <- $.site_admin` in that order;
- fallback zero-to-many REST operation
  `github_search_duckdb_login_page`, method `GET`, replay safe with retry,
  authentication, pagination, redirects, private, link-local, and loopback
  capability disabled;
- base `https://api.github.com`, path `/search/users`, ordered fixed query
  fields `q=duckdb+in%3Alogin` and `per_page=3`, and the three exact headers
  accepted by RFC 0005;
- response records extractor `$.items[*]`, exact HTTPS host narrowing to
  `api.github.com`, and connector ceilings of 65,536 response bytes, three
  records, and 256 bytes per extracted string; and
- native source provenance only, with no fixture or remote-response content
  digest, credential, caller-selected authority, filesystem path, or package
  source location.

The type exposes declarations only. Relational Semantics owns interpretation
into an applied `ScanPlan`; Remote Runtime owns host-policy intersection and
enforcement; Query Experience owns SQL exposure and DuckDB types. Consumers
may rely on stable values and ordering for the lifetime of a copied snapshot,
but the private C++ layout is not a public ABI or connector-package contract.

## Dependencies and parallel boundary

- RFC 0005 is the authoritative source for every value in the native snapshot.
- Relational Semantics consumes only `duckdb_api/connector.hpp` and the
  committed provider implementation; it must not duplicate the canonical
  metadata constructor or infer transport semantics from strings.
- Remote Runtime consumes only the later immutable `ScanPlan`; it does not
  construct or reinterpret connector declarations.
- Query Experience receives the connector through production composition and
  binds identifiers/schema without importing connector implementation details.
- Controlled loopback metadata belongs to a later private, non-installable
  integration composition. It is not part of this public production builder.

The Connector implementation includes only the C++ standard library and its
own header. The direct test may use repository test support, but no DuckDB,
runtime, planner, transport, YAML, environment, filesystem authority, or
network dependency is permitted in the production interface.

## Acceptance oracles

- Direct contract tests assert every identifier, enum, boolean, ordered query
  field, ordered header, column, extractor, policy value, and numeric ceiling.
- The expected canonical snapshot is compared byte-for-byte and two separately
  built values must produce identical snapshots.
- Provenance assertions reject the former fixture-specific shape by requiring
  the native origin and a snapshot with no fixture or response-content
  identity.
- The existing focused `connector_contract_tests` target compiles and passes.
- `git diff --check`, cached-diff whitespace validation after staging, and the
  repository agent-asset validator pass for the two workstream commits.

## Interaction exit

Connector Experience reaches its X-as-a-Service implementation exit when the
committed header, constructor, and direct oracle expose the exact RFC 0005
snapshot without YAML, runtime, planner, or DuckDB dependencies, and
Relational Semantics can consume the interface without coordinated edits to
these owned files. The broader goal exit remains open until final integration
proves that all consumers use the snapshot without duplicated connector
constants and that the installed artifact excludes test-only authority seams.

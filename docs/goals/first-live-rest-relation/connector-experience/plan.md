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
| `docs/CONNECTOR_SPECIFICATIONS.md` | Clarify the boundary between the compiled-in native product snapshot and the still-draft connector-package authoring contract |
| `fixtures/example/` | Retire the obsolete current-tree `example.items` metadata and response artifacts after the product path replaces that relation |
| `scripts/verify-source-identities.py` and `test/python/source_identity_contract.py` | Verify the current native connector source while preserving immutable `0.1.0`/`0.2.0` release records without retaining their fixture bytes as current inputs |

No Query, Runtime, Semantics, or Enablement production or build file is owned by
this workstream. In particular, the workstream does not edit CMake,
`ScanRequest`, `ScanPlan`, executor/stream types, decoding, HTTP policy
enforcement, composition, the DuckDB adapter, source-identity pins, or
immutable historical release records.

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
- Current source-identity verification succeeds with no `fixtures/example`
  source tree and rejects drift in the frozen `0.1.0`/`0.2.0` identity records.
- The existing focused `connector_contract_tests` target compiles and passes.
- `git diff --check`, cached-diff whitespace validation after staging, and the
  repository agent-asset validator pass for the two workstream commits.

## Interaction exit

Status: **Satisfied; X-as-a-Service**. The final integrated graph meets the
Connector-owned provider and consumer exit conditions:

- The committed `CompiledConnector` header, canonical constructor, explanation,
  and direct oracle expose the exact RFC 0005 snapshot using only Connector-
  owned types and the C++ standard library. Focused contract tests prove every
  field, ordering, ceiling, native provenance value, and deterministic copy
  without YAML, planner, runtime, DuckDB, filesystem, or network dependencies.
- Relational Semantics consumes the public immutable snapshot to build the
  golden `ScanPlan` and reject inconsistent or widened declarations without
  importing Connector implementation or authoring knowledge. Query's product
  composition obtains the snapshot from `BuildNativeGithubConnector`; request
  construction and bind consume only its public identity and schema values,
  with no duplicated request authority or coordinated edit to Connector-owned
  files.
- The installed artifact inventory proves that only the canonical GitHub
  authority is present and rejects loopback metadata, controlled factories,
  authority selectors, and retired `example.items` inputs. The separate private
  controlled artifact supplies its own test-only metadata through Runtime's
  documented factory and does not alter or enter the public Connector builder.
- `docs/CONNECTOR_SPECIFICATIONS.md` identifies the native metadata boundary,
  current source-identity verification consumes the canonical Connector source
  while preserving frozen `0.1.0`/`0.2.0` records, and current product
  verification no longer depends on `fixtures/example`.

The final product graph is integrated at `f834eb0`; the broader goal and Query
consumer exits are recorded as satisfied at `2d273f4`. Connector Experience
retains ownership of native metadata, provenance, direct contract tests, and
authoring-boundary documentation. Consumers receive the immutable snapshot as
a low-friction service; they do not acquire Connector syntax, validation, or
source-identity ownership, and this completion does not activate package
authoring.

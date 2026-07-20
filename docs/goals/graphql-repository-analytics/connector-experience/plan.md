# Connector Experience plan: canonical native GraphQL operation

## Outcome, authority, and topology

Status: **Planned; provider interaction remains in Collaboration**.

Connector Experience supplies RFC 0011's immutable source facts for
`github.viewer_repository_metrics`: one closed canonical GraphQL operation,
the exact typed relation schema and nullability, deterministic validation and
safe explanation, and a bounded consumer fixture. Semantics plans without
parsing the document; Query binds without learning protocol internals.

Under `docs/teams/CONNECTOR_EXPERIENCE.md`, Connector owns validated
`CompiledConnector` data and provider fixtures. Query Experience owns the
product outcome. Connector does not own `ScanRequest`, relational meaning,
`ScanPlan`, execution, DuckDB integration, lifecycle, or the v1 decision. RFC
0011 governs; the lead agent owns integration and the product manager retains
the reserved decisions in `AGENTS.md`.

### Topology routing

- Accountable team: Query Experience for the authenticated DuckDB-user result.
- Supporting team: Connector Experience provides the validated relation,
  compiled operation, snapshots, and fixture API.
- Consumers: Semantics consumes operation facts; Query consumes lookup,
  schema/nullability, and credential policy; Runtime receives only the plan.
- Interaction: bounded Collaboration with Relational Semantics to freeze the
  provider API and oracle, then X-as-a-Service through
  `duckdb_api_connector_metadata_service` and
  `duckdb_api_connector_fixture_service`.
- Decision authority: RFC 0011 and the lead agent; no package syntax or public
  native ABI is created.

## Exact scope

### In scope

- Replace the REST-shaped payload in `CompiledOperation` with an exhaustive,
  immutable REST-or-GraphQL operation value while preserving all existing REST
  facts and behavior.
- Admit exactly `GITHUB_VIEWER_REPOSITORY_METRICS_V1`: the RFC's canonical
  document bytes and deterministic content digest, exact
  `https://api.github.com:443/graphql`, non-secret fixed request metadata,
  `pageSize = 100`, nullable runtime `cursor`, and typed nodes/errors/pageInfo
  paths. Identity, bytes, and digest agree; no independent flag authorizes a
  different document.
- Declare the RFC's forward-only sequential mutable cursor profile, maximum 32
  pages, 100 records per page, 3,200 per scan, 8 KiB serialized body per
  request, and 256 KiB serialized bodies per scan. It grants no total, resume,
  snapshot, ordering, parallel-page, retry, or cache capability.
- Add native metadata version `0.7.0` relation
  `viewer_repository_metrics` with the exact ordered schema: required
  `id VARCHAR`, `full_name VARCHAR`, `owner_login VARCHAR`, `stars BIGINT`,
  nullable `primary_language VARCHAR`, and required `private BOOLEAN`,
  `archived BOOLEAN`, and `updated_at VARCHAR`, with exact RFC extractors.
- Reuse the existing required logical `token`, bearer authenticator, exact
  GitHub destination, and `Authorization` placement without carrying a secret
  name or credential value.
- Declare zero-to-many cardinality, zero conditional inputs, no predicate
  mappings or remote relational authority, fail-only GraphQL errors, disabled
  retry/cache/providers, and immutable request/response/resource facts.
- Validate the complete relation and operation against equality to the one
  reviewed profile before publication. Extend safe deterministic snapshots
  with identity, digest, response, cursor, and bound facts, excluding document
  bytes, values, cursors, credentials, rows, and execution state.
- Provide one Connector-owned non-installable canonical GraphQL catalog
  fixture with a stable relation identifier and public const access only.
- Preserve all three existing REST relations, their schema/source meaning,
  Link pagination, visibility mapping, policies, fixtures, and execution
  authority.

### Out of scope

- Package/YAML syntax, parser/compiler/loader, diagnostics, generated
  selections, arbitrary documents/variables, introspection/import, migration,
  registry/reload, or distribution; authoring remains future design.
- General GraphQL parsing; caller roots/selections/endpoints, mutations,
  subscriptions, partial data, custom scalars, or another canonical profile.
- REST-body disguises, relation-specific Runtime entry points, or an
  independently asserted query/replay classification.
- DuckDB expression work, base-domain and replay proof, operation selection,
  residual ownership, `ScanRequest`, `ScanPlan`, or semantic explanation.
- Serialization, bearer placement, transport, decode, cursor state, accounting,
  `BatchStream`, cancellation/close, nullable runtime values, DuckDB vectors,
  product composition, end-to-end services, or live compatibility.
- New SQL arguments or predicates, remote projection/order/limit/offset,
  deduplication, stable ordering/snapshot claims, native temporal conversion,
  or a v1 compatibility promise for the preview name.

## Production, test, and documentation ownership

Each module has one primary reason to change. New-file names may be adjusted
during interface freeze only if these responsibilities remain explicit.

| Artifact | Connector Experience responsibility and reason to change |
| --- | --- |
| `src/include/duckdb_api/connector_catalog.hpp` | Bounded pre-`1.0` team API: closed protocol alternative, canonical identity/digest, typed GraphQL facts, const access, lifetime, and compatibility documentation |
| Proposed `src/include/duckdb_api/internal/connector/protocol_operation_declaration.hpp` and `src/connector/protocol_operation_declaration.cpp` | Generic exhaustive REST/GraphQL value and wrong-variant/common validation; no native profile or semantic classification |
| Proposed `src/include/duckdb_api/internal/connector/graphql_operation_declaration.hpp` and `src/connector/graphql_operation_declaration.cpp` | The one canonical GraphQL declaration changes: document/digest binding, variables, response paths, cursor/body bounds, exact-profile validation, and safe rendering |
| `src/connector/catalog_model.cpp` | Relation/catalog cross-field validation and validation dispatch before publication |
| `src/connector/catalog_snapshot.cpp` | Safe deterministic explanation changes; never parsing or execution authority |
| `src/connector/native_github_composition.cpp` | Installed GitHub inventory/version changes; sole production construction of the fourth relation |
| `src/include/duckdb_api/connector.hpp` and `src/connector/README.md` | Native builder boundary and maintainer ownership, fixture, no-I/O, and inactive-package guidance |
| `src/connector/sources.cmake` and `src/connector/targets.cmake` | Connector production inventory and independently linkable metadata-service boundary |
| `test/cpp/connector/connector_catalog_contract_tests.cpp` | Generic sum, wrong-variant, immutability, cross-field, and REST-compatibility oracle |
| Proposed `test/cpp/connector/connector_graphql_contract_tests.cpp` plus runner declaration | Canonical GraphQL profile positive/negative matrix |
| `test/cpp/connector/connector_contract_tests.cpp` | Exact installed `0.7.0` inventory/schema/profile/snapshot and retained REST regressions |
| `test/cpp/connector/support/catalog_test_access.hpp` | Connector-private invalid construction only; never a consumer API |
| `test/cpp/connector/support/connector_catalog_test_fixtures.*` | Bounded canonical GraphQL fixture service and stable identifier |
| `test/cpp/connector/connector_catalog_test_fixtures_tests.cpp` | Fixture determinism, production-validation passage, const boundary, and prohibited-state oracle |
| `test/cpp/connector/sources.cmake` and `test/cpp/connector/targets.cmake` | Focused test inventory and fixture-service dependency boundary |

`duckdb_api_connector_metadata_service` remains the sole production provider;
`duckdb_api_connector_fixture_service` remains the test provider. Protocol
dispatch is generic; canonical document, variable, response, cursor, and body
validation remain one profile. `catalog_model` checks relation/auth/resource/
network agreement; native composition contains values, not validation logic.

Connector owns adjacent C++ documentation and `src/connector/README.md`. It
supplies and reviews:

- the fixed native-profile and explicitly inactive package/YAML wording for
  `docs/CONNECTOR_SPECIFICATIONS.md`;
- the compiled-operation wording for `docs/RUNTIME_CONTRACTS.md`; and
- the closed native-profile wording for `docs/ARCHITECTURE.md`.

The lead integrates those contracts and product records. Wording must not
imply arbitrary GraphQL, package compatibility, generated selections, remote
relational authority, partial data, or stable row order.

## API dependencies and execution order

| Consumer | Allowed dependency | Prohibited dependency or inference |
| --- | --- | --- |
| Query production | `duckdb_api_query_request_service` links `duckdb_api_connector_metadata_service`; exact lookup and public const schema/nullability/auth access | No private GraphQL header, profile construction, document parsing, or semantic/replay classification |
| Relational Semantics production | `duckdb_api_relational_planning_service` links the metadata service and consumes the public const operation alternative | No native composition source, internal Connector header, snapshot parsing, or document-derived authority |
| Semantics focused tests | Link `duckdb_api_connector_fixture_service`; include its public fixture header and Connector facade | No `catalog_test_access.hpp`, fixture `.cpp`, internal header, or direct Connector source list |
| Query tests | May link the fixture service for lookup/schema evidence; product composition uses the native builder | No fixture identity promoted to public SQL or private construction |
| Remote Runtime production/tests | Consume `duckdb_api_scan_plan_service` or `duckdb_api_semantics_fixture_service` only | No Connector target/header/fixture, relation-name branch, document inspection, or compiled metadata retention |

Connector consumes no Query, Semantics, Runtime, DuckDB, transport, or
authorization production API.

Delivery order is:

1. Freeze the public operation facts and stable fixture identifier with
   Semantics; Runtime confirms the completed plan needs no Connector type.
2. Implement the generic operation value, canonical validation/snapshot, and
   direct tests without changing the installed catalog.
3. Publish the non-installable fixture; Semantics then freezes its plan and
   fixture services against the bounded provider API.
4. Semantics, Runtime, and Query prove their disjoint consumer paths.
5. Lead-agent integration activates the native relation/version only after the
   permanent provider-to-plan path is buildable, then propagates shared
   contracts and runs the exit audit.

After step 1, Connector validation/tests and Semantics plan values are
parallel-safe in disjoint files. After step 3, Runtime and Query proceed in
parallel against Semantics services. Provider API, plan API, and native
activation stay serialized. No duplicate type/profile/document, snapshot
parser, REST fallback, or relation-specific bridge is allowed. The lead owns
integration and Git history.

## Positive and negative oracles

Positive Connector evidence proves:

- deterministic offline `0.7.0` construction with the three retained REST
  relations followed by `viewer_repository_metrics`;
- exact eight-column order/type/extractor/nullability and required bearer
  policy, with no predicate or relational delegation;
- exact canonical identity/bytes/recomputed digest, endpoint, variable and
  response paths, cursor declaration, and request/scan bounds;
- immutable copy behavior and safe locale-independent snapshots containing no
  document, variable value, cursor, credential, secret name, row, SQL, request
  body, or runtime state;
- the public fixture passes the same production validators, is deterministic
  and I/O-free, and exposes no construction access; and
- all existing REST catalog, pagination, predicate, proof, resource, fixture,
  and snapshot oracles retain their meaning.

Negative construction rejects at least:

- unknown identity; empty/oversized/changed document; digest mismatch;
  query-labeled mutation, subscription, extra operation, changed root,
  argument/filter/order/selection drift, or reverse-pagination fields;
- changed origin/path, credential-bearing fixed metadata, secret-bearing
  variable metadata, or destination outside connector policy;
- missing/extra/renamed/mistyped/wrong-source variables or response paths,
  overlapping row/error/pageInfo paths, wrong page size, caller cursor, or
  partial-data acceptance;
- independent/concurrent/resumable/stable cursor claims; zero, widened,
  inconsistent, or overflowing page/body/row/resource ceilings;
- missing/reordered/mistyped/mis-nullable columns, predicate/input/remote
  relational authority, retry/cache/provider enablement; and
- REST/GraphQL payload/tag mismatch, wrong-variant accessor fallback, or an
  independent query/replay relabeling.

Connector owns declaration integrity only. Semantics owns base-domain,
cardinality, predicate and replay meaning; Runtime owns admission, serialized
body, cursor, resource, security, error, and lifecycle behavior; Query owns
offline bind, nullable SQL values, and result equivalence.

## Documentation and acceptance evidence

Adjacent APIs document purpose, ownership, inputs/outputs, identity/digest,
nullability, immutable lifetime, compatibility, errors, resource authority,
prohibited state, and lack of Connector lifecycle behavior. Profile code
explains exact equality, indivisible authority, and absence of DuckDB ordering.
Fixture docs mark the service non-installable and forbid construction.

Acceptance requires focused provider/fixture tests, Semantics plan oracles,
Runtime's verified lack of Connector dependency, Query lookup/schema evidence,
REST regressions, dependency audit, contract propagation, and product evidence.

For this plan-only artifact:

```sh
ruby scripts/validate-agent-assets.rb
git diff --check
git diff --cached --check
```

Future implementation runs focused Connector executables, `make build`,
`make test`, `make demo`, identity checks, and a fresh native product cell.
The lead adds required sanitizer evidence and stages before the cached check.

## Observable Collaboration to X-as-a-Service exit

Current state: **Open; Collaboration**.

The interaction becomes **Satisfied; X-as-a-Service** only when the final tree
shows all of the following, in addition to passing provider and product tests:

- `src/connector/sources.cmake` lists Connector production once and
  `duckdb_api_connector_metadata_service` is the only focused production
  provider; no consumer target compiles a Connector `.cpp` directly.
- Query request and Semantics planning targets link that service and include
  only the public Connector facade; they do not include
  `duckdb_api/internal/connector/*`.
- The Semantics GraphQL test target links
  `duckdb_api_connector_fixture_service`, includes only
  `connector/support/connector_catalog_test_fixtures.hpp` for Connector test
  data, and imports neither `catalog_test_access.hpp` nor fixture
  implementation.
- The fixture service alone constructs the canonical test catalog, links the
  metadata service, passes production validation, and exports only a stable
  identifier plus immutable factory result.
- Runtime targets link only plan/runtime services; no Runtime source or target
  includes or links Connector metadata or fixtures.
- Semantics copies every required fact into one immutable `ScanPlan` without
  parsing the document, snapshot, name, extractor, endpoint, or digest to
  invent meaning. Query does not reclassify it, and neither Query nor Runtime
  constructs, retains, or reinterprets Connector-private profile state.

The interaction stays Open if target names hide direct provider source lists,
a consumer imports private construction, Semantics or Query duplicates the
canonical profile, Runtime branches on `viewer_repository_metrics`, snapshots
or document text are parsed for authority, or ordinary profile changes still
require coordinated edits outside the bounded Connector-to-Semantics contract.

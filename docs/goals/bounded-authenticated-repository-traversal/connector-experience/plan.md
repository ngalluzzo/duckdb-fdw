# Connector Experience plan: native repository pagination metadata

## Outcome and boundary

Status: **Planned; Collaboration open**.

Provide RFC 0007's permanent Connector Experience service: an immutable,
deterministically explainable `CompiledConnector` catalog whose native `0.5.0`
snapshot adds `github.authenticated_repositories` with an explicit closed Link
pagination declaration. The declaration gives Relational Semantics every
source fact needed to construct a conservative plan without inferring
pagination from structural query fields and gives Query exact schema and
credential requirements without exposing parser, transport, or lifecycle
internals.

This is a supporting workstream for the Query Experience outcome. Connector
Experience owns the native metadata representation, its validated construction,
safe explanation, direct catalog oracles, and the private test provider used by
consumers. It does not own the SQL surface, relational interpretation, Link
grammar or state machine, authorization execution, transport, resource
enforcement, `BatchStream`, DuckDB lifecycle, or product demonstration.

The semantic delta is:

- **Current:** the `0.4.0` catalog has two relations, every operation carries a
  pagination boolean fixed to false, and the planner rejects enabled
  pagination without an explicit strategy or request-transition contract.
- **Delivered:** the `0.5.0` catalog retains those relations and adds one exact
  many-row relation whose immutable metadata distinguishes disabled pagination
  from a sequential mutable Link profile, including typed page bindings,
  continuation constraints, and relation-owned resource narrowings.

This remains repository-owned `native_product_metadata`. It does not activate
`duckdb_api/draft` YAML, package loading, connector author tooling,
distribution compatibility, or a public native ABI.

## Permanent ownership and file boundaries

| Artifact | Connector Experience responsibility |
| --- | --- |
| `src/include/duckdb_api/connector_catalog.hpp` | Private pre-`1.0` metadata API for explicit pagination, relation resource narrowings, immutable consumer access, and adjacent ownership/compatibility documentation |
| `src/connector_catalog.cpp` | Closed-state validation, cross-field consistency checks, safe accessors, and locale-independent canonical explanation for generic catalog values |
| `src/include/duckdb_api/connector.hpp` | Documentation of the no-I/O canonical native builder and its `0.5.0` product-metadata boundary |
| `src/connector.cpp` | Sole production construction of the three stable native relations; final addition and version activation are integration-gated |
| `test/cpp/connector_catalog_contract_tests.cpp` | Representation, immutability, unavailable-state, validation-counterexample, credential-absence, and authority-separation tests independent of the native catalog |
| `test/cpp/connector_contract_tests.cpp` | Exact `0.5.0` native inventory, schemas, structural requests, policies, pagination declaration, preserved-relation regression, provenance, and canonical snapshot tests |
| `test/cpp/support/connector_catalog_test_access.hpp` | Connector-owned non-installable construction access for invalid values and consumer fixtures; never a production constructor |
| `test/cpp/support/connector_catalog_test_fixtures.hpp` and `.cpp` | A deterministic explicit-pagination catalog service that Semantics and Query tests consume through public const metadata accessors only |
| `test/cpp/connector_catalog_test_fixtures_tests.cpp` | Direct proof that the consumer fixture is stable, explicit, credential-free, and does not require request-field inference |

No new production module is planned. Generic metadata and validation continue
to change together in `connector_catalog.*`; the fixed GitHub inventory remains
separate in `connector.cpp`. The existing fixture service is extended rather
than adding a new build target, so Connector work does not require a parallel
edit to root build registration.

`docs/CONNECTOR_SPECIFICATIONS.md` is an affected authoritative contract, but
the active goal reserves authoritative contract propagation to lead-agent
integration. Connector Experience supplies and reviews the exact native
metadata wording described below; it does not independently edit architecture,
runtime contracts, roadmap, release notes, examples, CMake, source-identity
scripts, product composition, `ScanRequest`, `ScanPlan`, Runtime, or the DuckDB
adapter.

## Explicit native metadata API

### Closed pagination value

Replace `CompiledOperation::pagination_enabled` with one immutable
`CompiledPagination` value. Production construction is restricted to the
canonical native builder; non-installable construction belongs only to
`ConnectorCatalogTestAccess`. Consumers receive it through the selected
relation's const operation metadata and cannot mutate a retained catalog.

The value has exactly two supported states:

1. **Disabled** — no strategy, Link relation, page bindings, consistency claim,
   total, resume, target scope, or page budget is present.
2. **Sequential Link** — every field below is present and validated as one
   coherent declaration.

The enabled state exposes closed typed values, not free-form capability
booleans, for:

| Fact | Native `authenticated_repositories` value |
| --- | --- |
| Strategy | Link header |
| Link relation | `next` |
| Dependency | sequential |
| Consistency | mutable |
| Total support | none |
| Resume support | none |
| Page-size query binding | exact name `per_page`, typed value `100` |
| Page-number query binding | exact name `page`, first value `1`, required increment `1` |
| Continuation target scope | exact selected operation scheme, host, port, and path; only the declared page bindings may vary |
| Maximum pages per scan | `32` |

Typed numeric page facts are distinct from the operation's already encoded
initial query. Catalog validation requires the structural request to contain
exactly `per_page=100` and `page=1` in stable order for this native profile and
requires those values to agree with `CompiledPagination`. Semantics copies the
typed declaration and may reject contradictions; it does not discover a
paginator by parsing query names or values. Runtime receives only the resulting
`PaginationPlan` and never imports Connector metadata.

`CompiledPagination` contains no received Link value, response URL, parsed URL,
next-page mutable state, credential or secret reference, DuckDB object,
transport handle, deadline, counter, cancellation token, or execution method.
It declares source facts; it does not parse or follow a continuation.

### Relation resource narrowings

Extend relation-owned resource metadata so page and scan row/response limits
are unambiguous. The metadata must distinguish, at minimum:

- maximum wire-response bytes per page and per scan;
- maximum decoded records per page and per scan; and
- maximum extracted string bytes per value.

The new relation declares `8 MiB` and `64 MiB` wire-response ceilings, `100`
and `3,200` decoded-record ceilings, and `512` extracted string bytes per value.
Its pagination declaration separately carries the 32-page ceiling.

The catalog-wide network policy remains an outer narrowing for exact HTTPS
`api.github.com`; it is not sufficient by itself to preserve the two existing
relations when the new relation requires a larger response. Their relation
metadata therefore retains the effective `64 KiB` response limit, current
three-or-one record limits at both page and scan scope, and 256-byte extracted
string limit. Semantics owns the Connector/host intersection and Runtime owns
enforcement. Header, decompression, decode-memory, output-batch, wall-time,
concurrency, counter, and cancellation mechanics remain Semantics/Runtime
responsibilities rather than Connector implementation knowledge.

Validation rejects zero ceilings, scan ceilings below page ceilings,
overflowing or contradictory page/record bounds, a page size above the
per-page record ceiling, and a per-page wire narrowing that exceeds its
enclosing connector response policy. The aggregate scan byte ceiling is a
separate scope consumed by the plan. Explanation renders page and scan scope
explicitly so a consumer cannot mistake a page limit for silent whole-scan
truncation.

### Native `0.5.0` inventory

The canonical catalog has origin `NATIVE_PRODUCT_METADATA`, connector `github`,
metadata version `0.5.0`, and stable order:

1. `duckdb_login_search_page`;
2. `authenticated_user`;
3. `authenticated_repositories`.

The first two relations retain their accepted identifiers, schemas,
cardinality, paths, fixed query shape, authentication policy, response shape,
one-request behavior, and relation resource narrowings. Version-bearing
catalog and `User-Agent` metadata advance coherently to `0.5.0`; neither
relation gains pagination.

`authenticated_repositories` declares:

- columns, in order: required `id BIGINT <- $.id`,
  `full_name VARCHAR <- $.full_name`, `private BOOLEAN <- $.private`,
  `fork BOOLEAN <- $.fork`, and `archived BOOLEAN <- $.archived`;
- one fallback replay-safe `ZERO_TO_MANY` REST `GET` operation named
  `github_authenticated_repositories`;
- exact `https://api.github.com:443/user/repos`, ordered fixed query
  `per_page=100` then `page=1`, root-array records extractor `$[*]`, and only
  the accepted non-sensitive `Accept`, `User-Agent: duckdb-api/0.5.0`, and
  `X-GitHub-Api-Version: 2022-11-28` headers;
- the existing required logical `token`, bearer authenticator, exact GitHub
  destination, and `Authorization` placement policy, with no secret name or
  credential bytes; and
- the explicit sequential Link and relation resource values above, with retry,
  cache, provider, redirect, caller URL/header, parallelism, total, and resume
  absent.

The snapshot includes every safe declared distinction and native provenance.
It remains explanation, not serialization or execution authority, and excludes
received Link contents, repository data, DuckDB secret names, credential
values, package paths/digests, YAML provenance, and runtime state.

## Validation and deterministic oracles

The generic catalog oracle proves:

- the pagination value is copyable with the immutable relation/catalog but is
  not default-, partially-, or production-caller constructible;
- disabled pagination carries no enabled-state payload;
- Link pagination is rejected on exactly-one/root-object operations, on an
  operation whose fixed initial bindings disagree, or with independent,
  stable, total, resume, zero/inconsistent page, or widened-resource claims;
- query/header/authority validation still rejects duplicates, injection, a
  valued `Authorization` header, mismatched credential destination, and
  destinations outside connector policy; and
- no metadata or snapshot member can retain a response URL, Link field-value,
  secret name, credential bytes, provider handle, runtime counter, or DuckDB
  lifecycle object.

The native catalog oracle proves exact three-relation count/order and lookup,
every new schema/extractor/request/auth/pagination/resource field, canonical
snapshot bytes, locale independence, copy independence, and `0.4.0` relation
regressions. It distinguishes required extraction from DuckDB-visible
`NOT NULL` metadata and proves no credential or private live row enters source
or expected snapshots.

The Connector-owned consumer fixture provides at least one disabled operation
whose request contains page-like query data and one explicitly paginated
operation with stable service identifiers. Its direct test proves that the
pagination distinction resides in `CompiledPagination`, not a relation name,
credential requirement, request query, or constructor detail. Semantics and
Query consume only the fixture factory and public const API; they do not gain
`ConnectorCatalogTestAccess`.

## Dependencies, overlap, and disjoint parallel work

| Participant | May work in parallel on | Must not overlap or infer |
| --- | --- | --- |
| Connector Experience | The owned catalog headers/sources and direct fixture/oracle files listed above | No `ScanPlan`, Runtime, adapter, build, identity, or authoritative contract file edits |
| Relational Semantics | `scan_plan.*`, `scan_planner.cpp`, Semantics-owned test access/fixtures/oracles after the pagination API shape is frozen | Must not edit Connector files, construct `CompiledPagination`, parse fixed query fields to infer pagination, or select “the authenticated relation” by credential requirement |
| Remote Runtime | Link metadata/parser/state machine, transport capture, aggregate accounting, and DuckDB-free tests | Must consume `PaginationPlan`, not include `connector_catalog.hpp` or duplicate native declarations |
| Query Experience | Exact relation request/bind behavior, adapter lifecycle, provider-fake tests, and final SQL evidence | Must not construct catalog internals, parse pagination, or activate the native relation before the real plan/runtime path exists |
| Lead-agent integration | CMake/build registration if needed, source/version identities, authoritative contract propagation, product composition, changelog, final gates, dependency audit, and Git history | Must preserve team APIs rather than merging provider and consumer responsibilities |

The current test graph contains a material consumer overlap:
`test/cpp/support/scan_plan_contract_test_support.hpp` and
`test/cpp/support/scan_plan_test_fixtures.cpp` select a relation by requiring a
unique `CompiledCredentialRequirement::REQUIRED`. The new catalog has two such
relations. Relational Semantics must replace that assumption with exact service
identifiers or the Connector-owned paginated fixture before the three-relation
native catalog is activated; Connector Experience does not edit those files.

The product composition passes `BuildNativeGithubConnector()` directly to the
table function, and bind accepts any exact catalog relation. Consequently,
adding `authenticated_repositories` to `src/connector.cpp` is public activation,
not an isolated metadata commit. The generic `CompiledPagination` API,
validation, disabled native values, and private consumer fixture may integrate
before activation, but the third relation and `0.5.0` identity wait for the
complete Semantics, Runtime, and Query path.

Parallel writers use disjoint files or separate worktrees. A provider/consumer
API change may be prepared concurrently after the field contract is frozen,
but the lead integrates it as one buildable checkpoint; no temporary boolean,
relation-name branch, duplicate constructor, or silent page-one fallback is
added to bridge teams.

## Sequencing gates

1. **Governance gate — satisfied.** RFC 0007 is Accepted, product approval and
   affected-team reviews are recorded, and the product goal is Active.
2. **Provider-shape gate.** Connector and Semantics agree on the const
   `CompiledPagination` and scoped resource fields, ownership, validation, and
   snapshot contract. Runtime confirms the proposed plan can carry every
   required fact without importing Connector types. No public relation is
   added.
3. **Generic provider gate.** Land the closed metadata value, validation,
   explanations, disabled values for both `0.4.0` relations, direct generic
   counterexamples, and the Connector-owned paginated test service. Integrate
   the corresponding Semantics consumer compile changes without a compatibility
   shim.
4. **Planning-service gate.** Semantics maps the private fixture into an
   immutable pagination plan, rejects contradictions offline, and removes the
   unique-required-relation test assumption. Runtime can then implement and
   exercise its service against Semantics-owned plan fixtures while Query works
   on file-disjoint provider-fake lifecycle evidence.
5. **Runtime-readiness gate.** DuckDB-free Runtime evidence proves the accepted
   request sequence, denial, budgets, authorization reuse, empty-page
   same-pull behavior, cancellation, close, late failure, and redaction. No
   product catalog activation precedes this gate.
6. **Native activation gate.** Integrate the three-relation builder, exact
   native snapshots, `0.5.0` identities, real Semantics plan, Runtime executor,
   Query SQL path, and preserved-relation regressions as one coherent product
   increment. A profile that cannot execute the declaration rejects before I/O
   and never degrades to page 1.
7. **Propagation and exit gate.** Lead-agent integration updates every affected
   authoritative contract and identity artifact, runs focused and complete
   cached/fresh gates, and audits declarations, includes, construction points,
   tests, and adjacent API documentation against the interaction exits below.

## Documentation obligations

Adjacent code documentation must state:

- Connector ownership of declarations versus Semantics interpretation and
  Runtime enforcement;
- disabled/enabled pagination state invariants, parameter meanings, target
  scope, page/scan resource scope, and why consumers must not infer from
  structural request strings;
- stable catalog identifiers/order, schema/nullability and extractor meaning,
  source provenance, immutable snapshot lifetime, copy behavior, and error
  ownership;
- that logical credential and placement metadata carries no secret name,
  credential value, provider object, or response-granted authority; and
- that the C++ types and native snapshot are private pre-`1.0` team APIs, not
  public ABI or `duckdb_api/draft` package compatibility.

Contract propagation to `docs/CONNECTOR_SPECIFICATIONS.md` must describe the
fixed `0.5.0` three-relation native inventory, strict Link subset, scoped
resource declarations, deterministic explanation, and preserved inactive YAML
status. It must not copy runtime algorithms into author syntax or claim that
general Link pagination can be authored, loaded, or distributed. Architecture
and runtime-contract wording remains owned by lead integration with the
Semantics and Runtime workstreams.

## Explicit non-work

This workstream does not implement or expose:

- YAML fields, schema files, parsers, connector directories, package
  provenance, source-aware author diagnostics, package tests, loading, reload,
  distribution, or author CLI behavior;
- SQL argument handling, DuckDB secret registration/lookup, adapter callbacks,
  `DataChunk` production, query error translation, or end-to-end SQL/live
  evidence;
- base-domain meaning, predicate/order/limit/offset ownership, conservative
  fallback, plan construction, or plan resource intersection;
- Link grammar parsing, response-header capture, request reconstruction,
  pagination state, repeated transport, bearer decoration, DNS/TLS policy,
  counters, backpressure, cancellation, close, or lifecycle cleanup; or
- retries, rate-limit waiting, parallel pages, resume, cache, providers,
  GraphQL, caller URLs/headers/page values, redirects, proxy, environment or
  persistent secrets, snapshot isolation, deduplication, or public native ABI.

## Interaction exits and evidence

Current status: **Open; Collaboration**.

### Connector Experience to Relational Semantics

Exit to **X-as-a-Service** when the catalog and fixture oracles prove every
pagination/resource distinction, Semantics consumes only public const accessors
and maps them offline without request-field or relation-name inference, invalid
metadata fails before I/O, and no coordinated constructor knowledge remains.

### Connector Experience to Query Experience

Exit when exact lookup and schema/auth access expose all three relations,
unknown/case-varied names remain absent, Query neither constructs metadata nor
assumes a unique authenticated relation, the public relation activates only
with the real executable path, and existing relation SQL regressions pass.

### Connector Experience to Remote Runtime

Exit to **X-as-a-Service** when Runtime has no production include or link-time
dependency on Connector declarations, receives only the immutable Semantics
plan and Query authorization capability, and its independent fixtures exercise
pagination without a native-catalog constructor or request-field inference.

### Overall Connector workstream

The interaction is **Satisfied; X-as-a-Service** only when:

- focused `duckdb_api_connector_tests` and
  `duckdb_api_connector_catalog_fixture_tests` pass with deterministic positive,
  negative, boundary, locale, copy, provenance, and credential-absence oracles;
- the final native snapshot is exactly the three-relation `0.5.0` declaration,
  both previous relation contracts remain covered, and no live/private row or
  response Link enters source or evidence;
- final declaration, include, construction, composition, build-target, and test
  inspection proves the dependency directions above;
- `docs/CONNECTOR_SPECIFICATIONS.md` records native behavior without activating
  package compatibility; and
- the integrated `make build`, `make test`, `make demo`, source/dependency
  identity checks, fresh native product gate, agent-asset validation,
  `git diff --check`, and staged `git diff --cached --check` pass.

The exit remains **Open** if a consumer constructs pagination metadata,
discovers it from query strings or relation/auth names, imports Connector
internals into Runtime, requires routine coordinated edits outside the provider
contract, activates an unexecutable relation, widens either existing relation's
accepted authority without an explicit decision, or conflates native metadata
with connector-package authoring.

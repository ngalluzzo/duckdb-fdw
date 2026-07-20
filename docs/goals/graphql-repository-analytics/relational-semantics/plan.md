# Relational Semantics plan: GraphQL repository analytics

## Outcome, authority, and boundary

Status: **Planned; X-as-a-Service interaction remains Open**.

Relational Semantics supports Query Experience's accountable `0.7.0` outcome
by turning Connector Experience's immutable canonical GraphQL relation and
Query Experience's protocol-neutral `ScanRequest` into the only complete,
immutable, explainable `ScanPlan`. Accepted
[RFC 0011](../../../rfcs/0011-add-graphql-repository-analytics.md) fixes the
shared contract. The
[Relational Semantics charter](../../../teams/RELATIONAL_SEMANTICS.md) supplies
authority for operation eligibility, base-domain meaning, relational
ownership, conservative fallback, and the planning oracle. The lead agent
retains integration authority; public decisions remain with the product
manager under `AGENTS.md`.

### Topology routing

- **Accountable stream:** Query Experience for the DuckDB-user outcome.
- **Supporting subsystem:** Relational Semantics provides the immutable
  `ScanRequest -> ScanPlan` service and semantic oracle.
- **Provider interaction:** Connector Experience, bounded Collaboration then
  X-as-a-Service, until its canonical compiled profile and fixture compile
  through the production planner.
- **Consumer interaction:** Query Experience and Remote Runtime,
  X-as-a-Service, when they consume the finished plan without semantic or
  replay reclassification.
- **Decision authority:** RFC 0011 fixes the contract; the lead agent owns its
  implementation within the accepted boundary.

In scope is the exhaustive REST-or-GraphQL planned-operation value, exact
GraphQL base-domain and replay classification, immutable cursor and resource
plans, planned schema/nullability, deterministic validation and explanation,
bounded provider fixtures, and retained REST behavior.

Out of scope is GraphQL parsing or generation, author/package syntax, request
serialization or execution, authentication placement, response/error decode,
mutable cursor state, retries, cancellation, rows or typed runtime values,
DuckDB vectors or SQL behavior, projection/predicate/order/limit pushdown,
providers, cache, parallel pages, resume, deduplication, or snapshot claims.
Semantics treats the canonical document as identified bytes; it never parses
GraphQL and never sends or simulates a request.

## Immutable plan contract

The production entry remains the deterministic, network-free
`CompiledConnector + ScanRequest -> ScanPlan | PlanningError` service. Failure
is all-or-nothing and returns no partial plan.

| Plan concern | Required immutable result | Invalid rather than fallback |
| --- | --- | --- |
| Protocol operation | Closed `REST` or `GRAPHQL` alternative with guarded payload access. The GraphQL payload carries `GITHUB_VIEWER_REPOSITORY_METRICS_V1`, exact canonical bytes and digest, typed `https://api.github.com:443/graphql` authority, query-only kind, fixed headers and variables, typed nodes/errors/page-info paths, and fail-only partial-data policy. REST retains its current values. | Unknown protocol or identity; inactive payload access; byte/digest drift; mutation/subscription or extra operation; typed invocation, path, variable, response-path, or error-policy contradiction. |
| Base domain | Duplicate-preserving bag of every repository occurrence returned by complete sequential traversal of the exact accepted `viewer.repositories` invocation; zero-to-many cardinality; zero conditional-domain inputs; remote `TRUE`; unsupported remote predicate; DuckDB owns filter, final projection, ordering, limit, and offset. Fixed `UPDATED_AT DESC` is cursor enumeration, not SQL ordering. | Remote predicate/input, remote or Runtime relational delegation, ordering/snapshot/deduplication/total claim, provider, cache, or relation-name/document inference. |
| Replay | `SAFE` plus page replay unit derived only after the closed identity, exact bytes, recomputed digest, query-only kind, variable profile, domain, and response profile agree. Retry remains disabled. | Independent replay-safe assertion, changed operation with a matching label, mutable attempt/commit state, or any retry authority. |
| Cursor | Sequential, mutable, concurrency one, maximum 32 pages, no total/resume/stable-order claim; fixed integer `pageSize = 100`; nullable string `cursor`; typed nodes/errors/`hasNextPage`/`endCursor` paths; unchanged operation facts across pages. | Disabled/unknown or independent strategy, page-size/path contradiction, total/resume/parallel/stable claim, or current/seen cursor and response state in the plan. |
| Resources | One attempt, 100 rows, and 8 KiB serialized body per page; 32 attempts/pages, 3,200 rows, and 256 KiB serialized bodies per scan; accepted header, response, decompressed, string, nesting, decoded-memory, batch, wall, and concurrency bounds after Connector/host intersection. | Zero, widened, inverted, overflowing, page/scan-incoherent, or page-size/row-incoherent budgets. Budgets never become cardinality, limit, truncation, or retry authority. |
| Schema/nullability | Ordered exact schema: required `id VARCHAR`, `full_name VARCHAR`, `owner_login VARCHAR`, `stars BIGINT`, nullable `primary_language VARCHAR`, required `private BOOLEAN`, `archived BOOLEAN`, and `updated_at VARCHAR`, with typed extraction paths. | Missing/extra/reordered column, type/path drift, any required column made nullable, or `primary_language` made required. |

The plan contains no secret value, JSON request body, current cursor, seen
cursor set, response data, repository identity value, runtime handle, DuckDB
object, deadline clock, cancellation state, or mutable replay state. Runtime
owns those values. Planned nullability authorizes Runtime to produce a typed
null and Query to write vector validity; Semantics neither decodes nor writes
the value.

Safe explanation renders protocol, query-only kind, endpoint identity,
base-domain class, replay unit, cursor strategy, bounds, nullability, and
relational owners. It never renders or parses document bytes, variables,
cursor values, credential values, remote messages, response data, or rows.

## Production, test, and documentation ownership

Each module has one primary reason to change:

| Artifact | Relational Semantics ownership |
| --- | --- |
| `src/include/duckdb_api/planned_protocol_operation.hpp`, `src/semantics/planned_protocol_operation.cpp` | Value-only exhaustive REST/GraphQL operation sum, guarded access, protocol identity, and immutable copy/snapshot behavior. It changes when the planned protocol handoff changes. |
| `src/include/duckdb_api/scan_plan.hpp`, `src/semantics/scan_plan.cpp` | Protocol-neutral complete plan: base domain, ownership, cursor/resource plans, schema/nullability, disabled features, immutable access, and private construction. It changes when the cross-consumer plan contract changes. |
| `src/semantics/graphql_operation_planner.hpp`, `src/semantics/graphql_operation_planner.cpp` | Match Connector's typed canonical profile, require exact bytes/digest, derive replay, and map cursor/resource/schema facts. It changes with the accepted closed GraphQL planning profile and contains no parser or request builder. |
| `src/semantics/scan_planner_validation.cpp`, `src/semantics/scan_planner_internal.hpp` | Defensive request, operation, authentication/network, domain, and cross-scope validation before construction. It changes when planner admission laws or provider mappings change. |
| `src/semantics/scan_planner.cpp` | Compose validated protocol, relational, authorization, network, and resource results into one plan without duplicating validation or execution. |
| `src/semantics/scan_plan_explain.cpp` | Stable safe explanation of structured plan facts; no authority is recovered from prose. |
| `src/semantics/{README.md,sources.cmake,targets.cmake}` | Discoverable ownership, focused source inventories, target dependencies, start-here guidance, and the no-parser/no-execution rule. |

`relational_predicate.*` and `predicate_classifier.*` gain no GraphQL branch:
the new relation has no predicate mapping and uses the generic unsupported
result. The lead-agent integration owns coherent propagation through
`docs/ARCHITECTURE.md`, `docs/CONNECTOR_SPECIFICATIONS.md`, and
`docs/RUNTIME_CONTRACTS.md`; Semantics supplies and verifies the plan/domain/
replay/cursor/resource/nullability wording.

Adjacent code documentation must state producer and consumers, inputs and
outputs, private pre-`1.0` compatibility, construction authority, immutable
copy and concurrent-execution lifetime, inactive-payload failure, no-I/O
guarantee, error ownership, resource authority, and the absence of mutable
cursor/cancellation/close state. Comments must explain why byte/digest/profile
equality derives replay safety, duplicates remain occurrences, fixed ordering
is not SQL ordering, budgets grant no relational authority, and nullability is
split between planning, Runtime values, and Query validity.

## Provider fixtures and oracle ownership

### Connector provider boundary

Semantics tests consume catalogs only through the existing public
`duckdb_api_connector_fixture_service` and const `CompiledConnector` API. The
service provides one exact valid canonical profile and typed identity,
invocation, replay, cursor, resource, schema, and nullability counterexamples.
Semantics does not include `connector/support/catalog_test_access.hpp`, compile
Connector sources directly, reproduce catalog construction, or parse a
document. Connector alone owns malformed document, query-labeled mutation,
subscription, extra-operation, changed-selection/root-argument/omitted-filter,
and oversized-document validation oracles.

### Semantics provider boundary

Extend `duckdb_api_semantics_fixture_service` through
`test/cpp/semantics/support/graphql_scan_plan_test_fixtures.{hpp,cpp}`. It
publishes one closed valid GraphQL `ScanPlan` and named counterexamples that
each change exactly one operation, domain, replay, cursor, resource, ownership,
or nullability fact. Its public header exposes no builder, arbitrary setter,
Connector catalog, `ScanRequest`, or Runtime type. Its implementation links
only `duckdb_api_scan_plan_service`. Runtime consumes this service; Query
product tests call the real planner and never substitute a fixture plan.

### Positive and negative Semantics oracles

A focused `duckdb_api_graphql_semantics_tests` target owns:

| Test module | One reason to change and owned oracle |
| --- | --- |
| `test/cpp/semantics/graphql_operation_plan_tests.cpp` | Changes with the planned GraphQL operation contract; proves exact identity/bytes/digest, guarded alternative, query-only replay derivation, typed variables/paths, and their negative counterparts. |
| `test/cpp/semantics/graphql_base_domain_tests.cpp` | Changes with the GraphQL relational law; proves the exact invocation/omitted-filter profile, duplicate-preserving domain, zero inputs, unsupported predicate, and DuckDB ownership. |
| `test/cpp/semantics/graphql_cursor_resource_plan_tests.cpp` | Changes with immutable cursor or resource facts; proves the literal page/scan/body envelope and every isolated contradiction. |
| `test/cpp/semantics/graphql_nullability_plan_tests.cpp` | Changes with planned schema/nullability; proves the exact ordered profile and rejects every schema, path, type, or nullability drift. |
| `test/cpp/semantics/graphql_plan_fixture_tests.cpp` | Changes with the Semantics provider fixture; proves fixture-to-production equality, isolated counterexamples, public consumer boundary, immutable copies, ambient determinism, safe canaries, and unchanged REST snapshots. |

Together these modules require deterministic `PlanningError` with no partial
plan for unknown or mismatched protocol, identity, bytes/digest, kind,
invocation, response, replay, relational delegation, cursor, resource, schema,
or nullability facts.

The target calls `BuildConservativeScanPlan`, links the Connector fixture and
planning services, and links no Query adapter or Runtime production source. It
performs no GraphQL parsing or I/O. Runtime separately owns serialization,
transport, cursor transitions, errors/null decode, budgets, cancellation, and
terminal behavior. Query separately owns DuckDB bag/order/limit/null results.

Existing `duckdb_api_scan_planner_tests`,
`duckdb_api_scan_plan_contract_tests`,
`duckdb_api_scan_plan_pagination_contract_tests`, and
`duckdb_api_scan_plan_fixture_tests` remain mandatory REST, predicate,
immutability, Link-pagination, and provider-boundary regressions.

## Dependency direction and serialization

```text
Connector metadata + Query ScanRequest -> relational planning service
                                      -> immutable scan plan service -> Query
                                                                     -> Runtime
private Semantics construction -> Semantics fixture service -> Runtime tests
Connector fixture service -> GraphQL Semantics tests
```

- `duckdb_api_scan_plan_service` contains the planned-operation and plan-value
  sources only, links no Connector, Query, Runtime, DuckDB, curl, or fixture
  service, and its public headers include neither `connector_catalog.hpp` nor
  `scan_request.hpp`.
- `duckdb_api_relational_planning_service` may link the existing plan,
  predicate, Connector metadata, and Query request services. It links no
  Runtime target or DuckDB adapter.
- `duckdb_api_semantics_fixture_service` continues to link only
  `duckdb_api_scan_plan_service` and exposes a plan-value-only header.
- `duckdb_api_graphql_semantics_tests` links
  `duckdb_api_connector_fixture_service` and
  `duckdb_api_relational_planning_service`; it does not directly list provider
  production sources.

Connector publishes its compiled sum, canonical facts, accessors, and fixture
first. Semantics then freezes the plan headers and fixture API as serial
producer handoffs. Runtime and Query may implement in parallel only after those
services compile and do not edit the producer headers/fixtures. The lead owns
root build inventories, composition, contract/version/source-identity
propagation, integration review, and Git history.

## Observable X-as-a-Service exit

All exits remain **Open** until the final audit inspects actual declarations,
includes, source inventories, target links, fixture headers, and tests.

- **Connector -> Semantics:** `duckdb_api_relational_planning_service` consumes
  only `duckdb_api/connector_catalog.hpp` through
  `duckdb_api_connector_metadata_service`; GraphQL tests consume only
  `duckdb_api_connector_fixture_service`; neither includes Connector private
  access nor compiles Connector production sources.
- **Semantics -> Runtime:** `duckdb_api_runtime_executor_service` links
  `duckdb_api_scan_plan_service`; Runtime production includes the plan value but
  not `scan_planner.hpp`, `scan_request.hpp`, `connector_catalog.hpp`, or
  `graphql_operation_planner.hpp`; focused Runtime tests link
  `duckdb_api_semantics_fixture_service`, not Connector fixtures, planner, or
  Semantics production sources. Runtime handles both protocol alternatives
  without reading document text, source snapshots, relation names, or
  explanation to recover authority.
- **Semantics -> Query:** Query links and calls
  `duckdb_api_relational_planning_service`, constructs only protocol-neutral
  request facts, and does not compile Semantics sources or use plan fixtures for
  product construction. It contains no canonical-profile, base-domain,
  replay, cursor, budget-intersection, or nullability-reclassification logic.
- **Internal:** `duckdb_api_scan_plan_service` compiles without provider or
  consumer dependencies; `duckdb_api_semantics_fixture_service` compiles with
  only the plan service; GraphQL Semantics tests run without Query adapter or
  Runtime sources; no focused consumer target directly compiles provider
  production files.

If a consumer routinely needs provider-private construction, document parsing,
planner internals, or coordinated edits outside the value and fixture APIs,
the interaction remains Open.

## Acceptance evidence and sequencing

1. Connector publishes its provider API and fixture; Semantics does not create
   a temporary private substitute.
2. Semantics lands the operation/plan values, planner mapping, safe explanation,
   focused oracle, and fixture service while retaining all REST evidence.
3. Runtime and Query consume the frozen services; the lead propagates contracts
   and integrates the permanent product path.
4. The final exit audit verifies real includes and target inventories before
   the interaction is marked satisfied.

Completion evidence requires:

- the focused GraphQL target and all existing focused Semantics targets pass;
- plan and fixture tests prove immutability, guarded alternatives, exact
  positive/negative profiles, deterministic safe diagnostics, and absent
  document/cursor/credential/row canaries;
- the target/include audit satisfies every exit above;
- after integration, `make build`, `make test`, `make demo`, source and native
  dependency identity checks, and a fresh native product cell pass;
- `ruby scripts/validate-agent-assets.rb`, `git diff --check`, and the lead's
  staged `git diff --cached --check` pass; and
- independent Semantics review confirms domain and ownership laws, followed by
  required adversarial review of replay, cursor, resource, nullability,
  relational, and lifecycle-sensitive boundaries.

This workstream does not implement or test requests, responses, execution
state, DuckDB behavior, live GitHub compatibility, another team's production
code or plan, root integration, release records, authoritative contracts, or
goal closure. A compiling Semantics plan alone does not deliver the relation.

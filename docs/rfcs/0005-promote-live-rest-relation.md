# RFC 0005: Promote one live REST relation into the native product

```yaml
rfc: "0005"
title: "Promote one live REST relation into the native product"
status: "Accepted"
rfc_type: "Product"
sponsor_team: "Query Experience"
technical_decision_owner: "Lead agent"
product_approver: "Product manager"
authors:
  - "Lead agent"
required_reviewers:
  - "Query permanent-delivery reviewer"
  - "Connector live-contract reviewer"
  - "Relational permanent-delivery reviewer"
  - "Remote Runtime permanent-delivery reviewer"
  - "Engineering Enablement live-build reviewer"
affected_teams:
  - "Query Experience"
  - "Connector Experience"
  - "Relational Semantics"
  - "Remote Runtime"
  - "Engineering Enablement"
linked_outcome_or_objective: "First live REST relation"
supersedes: "none"
```

## Summary

Promote the proven fixed GitHub HTTPS relation into the permanent
`duckdb_api` extension as the `0.3.0` preview product. Retain the existing
`duckdb_api_scan` dispatcher, replace the fixture-only `example.items` relation
with compiled-in `github.duckdb_login_search_page`, keep bind and planning
offline, execute one bounded request through a protocol-neutral runtime, and
defer declarative authoring and distribution.

## Sponsorship and context

- **RFC type:** Product.
- **Sponsoring team:** Query Experience.
- **Linked outcome or objective:** `docs/goals/first-live-rest-relation.md`.
- **Why now:** The permanent extension proves only embedded-fixture execution.
  The live product trial removed mechanism uncertainty, so the next user value
  is the same path in team-owned permanent source.

The affected user builds the native extension, loads it in DuckDB, and queries
one live compiled-in REST relation. Repository incompleteness is not the value
claim; useful remote data through ordinary DuckDB SQL is.

## Problem

The accepted `0.1.0`/`0.2.0` source path hard-codes `example.items`, authorizes
only `fixture_rest`, embeds response bytes, and disables network execution. The
completed trial separately proves fixed HTTPS transport and strict decoding,
but its SQL, plan, runtime, and build are explicitly disposable evidence.

Observed facts:

- RFC 0001 excludes live network access, while RFC 0003 establishes only a
  protocol-neutral executor/stream lifecycle boundary.
- The permanent `CompiledConnector`, `ScanPlan`, row type, composition, SQL
  tests, and CMake graph remain fixture-specific.
- The trial passed controlled success and failure/lifecycle oracles and one
  current GitHub compatibility execution on DuckDB 1.5.4 `osx_arm64`.
- The trial used ambient system libcurl and therefore did not establish a
  production dependency or support commitment.

The decision cannot be resolved as an internal refactor: it changes preview
SQL behavior, shared team interfaces, network/resource policy, lifecycle, and
the durable dependency graph.

## Decision drivers and invariants

- **Must preserve:** deterministic offline bind/planning; immutable compiled
  metadata and plans; DuckDB-owned filter, ordering, limit, and offset;
  protocol-neutral execution; strict lossless conversion; bounded cancelable
  work; backpressure; redacted diagnostics; and native failure containment.
- **Must enable:** one ordinary permanent-extension SQL query whose scan
  executes one real HTTPS GET and returns strict typed rows.
- **Must not introduce:** YAML/package compilation, caller-selected URLs,
  credentials, ambient proxy authority, redirects, pagination, retries,
  caching, providers, GraphQL, publication work, or a public native ABI.

## Proposed decision

### Public behavior

The `0.3.0` preview keeps the named-argument table function:

```sql
SELECT id, login, site_admin
FROM duckdb_api_scan(
    connector := 'github',
    relation := 'duckdb_login_search_page'
);
```

The compiled relation declares `id`, `login`, and `site_admin` required and
non-null; missing or null values fail with a schema error. DuckDB binds the
logical types `BIGINT`, `VARCHAR`, and `BOOLEAN`. No DuckDB-visible `NOT NULL`
metadata is claimed. Execution issues one fixed unauthenticated GET
to `https://api.github.com/search/users?q=duckdb+in%3Alogin&per_page=3` and
extracts records from `items`. Public-service row identity and order are not a
correctness guarantee; the controlled oracle defines exact expected rows.

The relation's complete base-row domain is exactly the zero-to-three `items`
returned by that one fixed response page. It does not represent every GitHub
user or every result matching the search. The `q` and `per_page` values are
immutable source-definition constants; they are never derived from or
classified as DuckDB predicate or limit pushdown. The bounded domain is made
explicit in the preview relation name.

This preview intentionally replaces `example.items` rather than shipping two
product relations. Before `1.0`, preview connector/relation/schema changes may
move forward in a minor release; no backport or package compatibility is
promised. Unknown connector/relation identifiers continue to fail at bind with
safe actionable diagnostics.

### Shared interfaces

Connector Experience provides an immutable compiled-in snapshot containing
explicit `native_product_metadata` origin; `github` connector, `0.3.0` metadata,
and `duckdb_login_search_page` relation identity; and exactly one stable
fallback, zero-to-many, replay-safe-but-retry-disabled REST GET operation with
internal identifier `github_search_duckdb_login_page`.
Request metadata is structural rather than a prejoined URL: typed scheme
`https`, exact host `api.github.com`, port `443`, path `/search/users`, and
canonical ordered query fields
`q=duckdb+in%3Alogin` then `per_page=3`. Fixed headers are exactly
`Accept: application/vnd.github+json`, `User-Agent: duckdb-api/0.3.0`, and
`X-GitHub-Api-Version: 2022-11-28`. Response metadata selects `$.items[*]` and
requires `id BIGINT <- $.id`, `login VARCHAR <- $.login`, and
`site_admin BOOLEAN <- $.site_admin`.

The connector snapshot narrows execution to HTTPS and exact host
`api.github.com`; denies redirects, private/link-local/loopback destinations,
authentication, and pagination; and declares ceilings of 65,536 response bytes,
three records, and 256 bytes per extracted string. Host-owned ceilings such as
one request, wall time, batch rows, JSON nesting, decoded memory, and concurrency
are intersected into `ScanPlan`; they are not hidden connector-author rules.
The canonical snapshot and its source identity are tracked by product evidence.
It contains neither remote response-content provenance nor credentials and does
not activate the YAML authoring path.

Query Experience constructs a conservative `ScanRequest` from that snapshot
and actual DuckDB capabilities. It contains query intent and relation identity,
not transport or network authority.

Relational Semantics constructs one immutable typed `ScanPlan`. The plan binds
the compiled snapshot identity to the executable operation and applied host
policy; records `TRUE` remote and residual predicates relative to the defined
single-response base domain; records zero-to-many cardinality, no remote limit,
and no runtime limit; assigns every DuckDB filter, ordering, limit, and offset
to DuckDB; and explicitly disables unsupported features. Typed
enums/structures replace combinations that consumers would need to reinterpret.
Runtime validates only executable capability facts and must not rebuild the
canonical plan, compare the whole explanation snapshot, or reclassify
relational meaning.

Remote Runtime retains `ExecutionControl`, `ExecutionCancelled`,
`ScanExecutor`, and `BatchStream`. The row batch becomes DuckDB-free and
schema-aligned rather than fixture-specific `ItemRow{id,name,active}`.
Structured errors distinguish transport, HTTP status, decode, schema,
policy/resource, and internal stages without exposing URL, authority, response
content, or dependency diagnostics.

Query remains the only DuckDB-aware layer. Its bind data implements immutable
copy semantics for prepared plans; scan code consumes only the abstract
executor, stream, typed batch, cancellation, and structured errors.

### Operational behavior

- Network authority is acquired only during execution. The sole public
  authority is HTTPS `api.github.com:443`; no HTTP fallback or caller URL
  exists.
- The installed artifact reads no environment variable, DuckDB setting, SQL
  argument, file, or mutable global to select an authority. Controlled tests
  use a private non-installable composition that injects a compiled loopback
  snapshot and runtime service at construction. It traverses the unchanged
  production registration, bind/copy, planning, executor, stream,
  error-translation, and `DataChunk` path. Artifact inventory and negative
  canaries prove the loopback metadata and test seam are absent from the
  installed artifact.
- TLS peer and hostname verification are mandatory. Every resolved address is
  checked against host policy before connection. Redirects, proxies, netrc,
  cookies, credentials, filesystem access, and process authority are disabled.
- One plan execution permits one wire attempt. HTTP/1.1 is pinned and all retry
  behavior is disabled until replay safety is decided separately.
- Hard ceilings cover request attempts, response/header/decompressed bytes,
  decoded records, string bytes, JSON nesting, decoded memory, batch rows,
  wall time, and concurrency. Whole-response buffering is permitted only
  within those ceilings.
- Cancellation reaches transfer and decoding. DuckDB `interrupt()` is the
  prompt cancellation contract. `Cancel`, `Close`, and destructors are
  idempotent and non-throwing; connection close is contained by the five-second
  hard execution deadline but is not promised to cancel promptly.
- Before table-function registration, the runtime performs exactly one checked
  `curl_global_init(CURL_GLOBAL_DEFAULT)`, then verifies the linked libcurl
  version, SSL backend, and `CURL_VERSION_THREADSAFE` feature bit. Initialization
  or identity rejection fails extension load before function registration and
  balances the unpublished initialization immediately. Initialization is never
  query-lazy. Accepted global state is deliberately process-resident and left
  to OS process reclamation; no service, extension, DSO, or `atexit` cleanup
  hook is registered because complete curl-user shutdown ordering is unproved.
  Dynamic extension unload/reload is unsupported in `0.3.0`.
- The sole `0.3.0` cell uses the platform-provided macOS libcurl without
  redistributing curl or TLS dependency bytes: macOS 26.5.1 build `25F80`,
  Command Line Tools SDK 15.5, SDK libcurl 8.7.1 headers/stub, artifact install
  name `/usr/lib/libcurl.4.dylib`, and runtime libcurl 8.7.1 plus its observed
  SSL-backend identity and `CURL_VERSION_THREADSAFE` capability. Native runners
  derive the SDK through `xcrun`, verify
  pinned relative paths and canonical header/stub digests, and pass those exact
  values to `find_package(CURL 8.7.1 EXACT REQUIRED)`/`CURL::libcurl`. Any
  alternate resolution fails closed. The OS trust store and platform TLS
  implementation are declared cell inputs, not byte-pinned redistributed
  dependencies.
- Connector-package distribution remains excluded. Extension artifact and
  source-build behavior continue to follow RFC 0004; this RFC does not reopen
  ordinary publication work.

## Topology impact

| Team | Role in this RFC | Interface or charter impact | Interaction mode | Exit condition |
| --- | --- | --- | --- | --- |
| Query Experience | Sponsor and outcome owner | Permanent SQL, bind/copy, adapter, composition, user oracle | Collaboration | Accepted SQL and lifecycle pass without adapter knowledge of transport internals |
| Connector Experience | Compiled metadata provider | Adds the fixed native live-operation snapshot; author/package syntax remains inactive | X-as-a-Service | Consumers plan and explain the immutable snapshot without YAML or runtime knowledge |
| Relational Semantics | Planning provider | Generalizes the immutable plan and ownership proof | Collaboration, then X-as-a-Service | Planner properties pass and consumers do not duplicate semantic decisions |
| Remote Runtime | Execution provider | Adds bounded HTTPS policy, transport, decode, stream, and diagnostics | Collaboration, then X-as-a-Service | Focused runtime tests need no DuckDB adapter and Query consumes only the documented service |
| Engineering Enablement | Build/test facilitator | Transfers pinned dependency, controlled service, identity, and fresh-build practice | Facilitation | Owning teams maintain the gates without Enablement approval |

No accountability boundary moves. Cognitive load stays behind provider APIs:
Connector knows declarations, Semantics knows proof, Runtime knows network and
lifecycle, and Query knows DuckDB.

## Correctness, security, and lifecycle analysis

- **Relational semantics:** relative to the defined single-response base domain,
  `R = TRUE`; the fixed `q` and `per_page` source constants are not pushdown.
  There is no remote or runtime limit. DuckDB retains the full predicate and
  every relational operator. The runtime emits each decoded base row exactly
  once and cannot infer relational meaning from request or response data.
- **Authentication, credentials, network policy, and privacy:** no credentials
  exist. A fixed authority plus post-DNS address checks narrows host policy.
  Response contents, URLs, authorities, and dependency messages are redacted.
- **Resources, backpressure, and cancellation:** one bounded buffered response
  feeds batches no larger than the plan ceiling. Connect, transfer, decode, and
  batch delivery poll cancellation under a hard deadline.
- **Replay, retry, caching, and duplicates:** one request is the replay unit;
  there is no retry or cache. Rows from the one response are emitted once.
- **Concurrency, immutability, and ownership:** compiled metadata, plan, and
  executor service are immutable/shareable. Each scan owns one independent
  stream and its transfer/response/decoder state. The accepted preview permits
  one active transfer per stream under the plan concurrency ceiling.
- **FFI, initialization, reload, shutdown, and containment:** libcurl global
  initialization belongs to runtime composition. Rejected unpublished state is
  cleaned immediately; accepted state is process-resident. No exception crosses
  DuckDB's native boundary; cancellation is translated once; stream/service
  teardown is non-throwing and never calls global curl cleanup.
- **Diagnostics and observability:** stable stage/field/safe-message errors and
  request-count test observations exist. The preview adds no public metrics or
  custom explain surface.

## Compatibility and migration

`0.3.0` intentionally replaces the previous fixture preview relation. Users of
the unreleased/preview `example.items` SQL migrate the two named arguments and
the selected columns. The table-function spelling and named-argument types are
preserved. The `0.2.0` artifacts and inventory remain immutable; no artifact is
relabelled.

Rollback means loading an authentic earlier version and regaining its embedded
fixture behavior. A failed live request never falls back silently to fixture
rows. Unsupported capability profiles or mutated executable facts fail closed
before related I/O. This RFC establishes no connector-package migration,
ordinary distribution, or compatibility outside the evidence-backed native
cell.

## Evidence and bounded trials

| Question or claim | Evidence required | Method or fixture | Result and limitations |
| --- | --- | --- | --- |
| Can the native extension return live strict rows? | Direct-load HTTPS execution | Permanent source-demo runner against GitHub | Passed on the integrated `0.3.0` artifact with the three declared logical types and a bounded zero-to-three-row result; current-service compatibility only |
| Can correctness avoid public-service drift? | Exact deterministic request/row oracle | Private non-installable composition plus controlled local HTTP service | Passed on the permanent adapter path: offline bind/prepare, ordinary/prepared equality, exact rows, byte-identical relational variants, and public-artifact exclusion are covered by the controlled 20-request oracle and inventory canaries |
| Are failures and work bounded? | Status, redirect, malformed, oversized, disconnect, deadline, interrupt, close, and recovery counterexamples | Controlled product lifecycle plus focused Runtime transport/executor tests | Passed on permanent source; prompt interruption and recovery are proven, while connection close retains the accepted five-second deadline boundary |
| Is destination policy applied after DNS? | Resolved-address denial and public-address acceptance | Runtime network-policy tests and curl socket callback | Passed in focused policy/curl tests and the reusable and fresh product gates on the supported cell |
| Can DuckDB's bundled client supply HTTPS? | Actual HTTPS execution | DuckDB 1.5.4 native client probe | No; the inspected client path was HTTP-only |
| Is libcurl production-ready for the claimed cell? | Constrained SDK/header/stub, artifact install-name, runtime version/SSL-backend/thread-safe feature, and clean permanent-build identities | `0.3.0` pins plus dependency/source/artifact/fresh-product gates; init-before-version-info, rejected-init cleanup, and accepted process-resident lifetime contracts | Passed: exact dependency identities, lifecycle tests, target-link classification, installed-artifact canaries, and a fresh 618-target product build agree on the selected cell ([official init](https://curl.se/libcurl/c/curl_global_init.html), [cleanup](https://curl.se/libcurl/c/curl_global_cleanup.html), and [feature identity](https://curl.se/libcurl/c/curl_version_info.html)) |
| Does the permanent planner preserve conservative ownership? | Golden plans plus negative/property oracles | Focused planner and differential DuckDB tests | Passed: the immutable golden plan, narrowed/widened counterexamples, offline ordinary/prepared path, and DuckDB-owned filter/order/limit/offset variants pass on permanent source |

The completed trial supplied the original decision evidence for mechanism
feasibility. The permanent results above now supply the implementation
acceptance evidence without changing the accepted shared contract.

## Alternatives considered

### Retain the fixture-only product

This avoids network and dependency obligations but does not deliver the
project's core user value. It leaves transport feasibility permanently outside
the product and is rejected.

### Ship the trial as a second extension or copy its modules verbatim

This is quick but creates a parallel public function and duplicates planning.
The trial runtime also reconstructs the planner result and leaks DuckDB/test
concerns into transport construction. It violates team APIs and is rejected.

### Preserve both fixture and live relations

This avoids a preview migration but expands catalog/composition/executor
selection before it creates product value. Deterministic controlled tests do
not require a production fixture relation. It is deferred until multiple
relations have a real user outcome.

### Use DuckDB's built-in HTTP client

It reduces dependency work but the DuckDB 1.5.4 trial path did not establish
HTTPS, post-DNS policy, or the required one-attempt behavior. It is rejected
for this cell.

### Resolve an unconstrained ambient system libcurl

It passed the macOS proof but produces host-dependent TLS and transitive
behavior. It is rejected as release evidence. The selected platform dependency
instead derives the exact SDK through `xcrun`, verifies header/stub identities,
passes explicit paths to an exact CMake version match, and checks the artifact
and runtime identities.

### Fetch or bundle a pinned libcurl/TLS graph

This could support additional platforms later but adds source custody,
redistribution, TLS-backend, trust-store, and publication work that creates no
additional value on the single proven preview cell. It is deferred until a
new supported cell requires it.

### Wait for declarative connector compilation

This reverses the dependency order: authoring would target an unproven product
runtime. A compiled-in relation supplies the needed immutable metadata without
publishing package syntax, so authoring remains deferred.

## Drawbacks and failure modes

The preview acquires a platform HTTPS dependency and its build, TLS identity,
licensing, and lifecycle burden. No curl/TLS bytes are redistributed, but the
supported cell now includes the OS trust store and platform implementation.
Engineering Enablement transfers that capability to Remote Runtime and Query.
The public service can change or become unavailable;
controlled evidence remains authoritative and failures are explicit. Buffered
decoding limits response size; larger and paginated data remain unsupported.
The hard close deadline may delay connection shutdown for up to five seconds.
Fixed endpoint/schema changes require another preview release or later
declarative authoring rather than runtime configuration.

## Acceptance and verification

- **End-to-end demonstration:** build/load the permanent public `duckdb_api`
  artifact and execute the accepted SQL against GitHub. Separately load the
  private controlled-test composition, prove zero bind/prepare requests, and
  observe strict typed batches plus safe bounded failures through the same
  adapter path. Inventory and negative canaries prove the public artifact has
  no loopback authority or test-selection seam.
- **Automated oracle:** focused connector/planner/decoder/transport/executor
  tests and controlled DuckDB ordinary/prepared/lifecycle integration. The
  controlled Query oracle covers `WHERE`, `ORDER BY`, `LIMIT/OFFSET`, and
  filter-before-limit, with byte-identical request targets and exactly one
  request per executed scan. Differential results equal DuckDB evaluation over
  the same returned base rows, including a filter-before-limit case whose
  surviving row follows an earlier nonmatching row.
- **Quality gates:** `make build`, `make test`, `make demo`, source-identity
  verification, and a fresh `scripts/run-native-product-tests.sh` cell with the
  constrained SDK/libcurl identities. `release/0.3.0/pins.json`, dependency
  verification, and artifact inventory cover SDK version, canonical curl
  header-tree/stub digests, configured target/path/version, dylib install name,
  runtime version/SSL backend/`CURL_VERSION_THREADSAFE` bit, pre-registration
  initialization/identity rejection cleanup, absence of accepted-state cleanup
  hooks, public-artifact
  exclusion of loopback/test seams, and the compiled connector/controlled
  fixture/public-contract source identities. Only transport-bearing targets
  link `CURL::libcurl`.
- **Independent review:** relational correctness, transport/security,
  DuckDB lifecycle, and test-oracle false-positive perspectives.
- **Interaction exit:** every topology row's exit condition is audited against
  final source dependencies and oracles.

## Implementation completion record

Implementation is **Satisfied** by the permanent product graph integrated at
`f834eb0`, including Query's final public binder-context correction through
`ba389a9`. RFC lifecycle status remains `Accepted`: acceptance authorized the
decision, while this record captures the later delivery and propagation proof.

Fresh closure evidence includes:

- `make test` on the integrated product graph passed every focused Connector,
  Semantics, Runtime, and Query target; 25 SQLLogicTest assertions; installed-
  artifact symbol, dependency, and test-control inventory; the controlled
  product's 20-request relational/lifecycle oracle; and the public GitHub
  compatibility query.
- `scripts/verify-source-identities.py` reported native Connector source
  SHA-256
  `d9cf66acedb97b0325ca9c9883afceaa91a491fe48e2f6d5d3744137f8d13e86`
  and public-contract SHA-256
  `f5d9a5c14ef603fef34bf7154ad2272e86742fec0af994aacfbfec4afe84c8e9`;
  all 11 deterministic dependency-verifier counterexamples passed.
- Fresh `make verify PROFILE=debug` rebuilt 618 targets with
  `developer_cache_reused=false` from exact delivered product tree
  `f9f11018fa4671faa213ff9999adc9c7c72e9689`. It repeated dependency
  identity and linkage classification, focused tests, TLS probes,
  SQLLogicTests, public inventory/direct load, controlled evidence, and the live
  example. Public artifact SHA-256 was
  `55371437224cee67a71f3b548643de35ce149c9f94626c25fc071a44c61f9182`;
  controlled artifact SHA-256 was
  `8813dff1d2a815a27bacf74c6c08012262f6126e57176e998318c88bcdb2663e`.

The final topology exit audit is:

| Team | Exit status | Final dependency and oracle evidence |
| --- | --- | --- |
| Query Experience | Satisfied; accountable outcome complete | The permanent and controlled entry points reuse one DuckDB adapter. Bind constructs only a conservative `ScanRequest`, retains the immutable `ScanPlan`, and opens only the public `ScanExecutor`/`BatchStream` service. Adapter, controlled relational/lifecycle, public contract, and artifact-canary oracles pass. |
| Connector Experience | Satisfied; X-as-a-Service | The canonical immutable native snapshot and direct contract target remain independent of DuckDB, planner, runtime, transport, YAML, and live-service machinery. Query and Semantics consume its public values without duplicating request authority. |
| Relational Semantics | Satisfied; X-as-a-Service | Focused plan representation and planner-policy targets independently prove the immutable offline plan and conservative counterexamples. Query constructs only `ScanRequest`; Runtime validates only typed executable facts and neither consumer reconstructs relational ownership. |
| Remote Runtime | Satisfied; X-as-a-Service | Focused executor, transport, policy, decode, budget, TLS, cancellation, and lifecycle tests remain DuckDB-free. Query consumes only the documented executor, stream, batch, control, and error service; the adapter has no Runtime-internal dependency. |
| Engineering Enablement | Satisfied; facilitation ended | Responsibility-named source groups, constrained dependency identities, curl-link classification, private/public artifact custody, source identities, stable Make commands, and a fresh product root are maintained as reusable gates without Enablement owning the domain oracles or approving normal delivery. |

No affected-team confirmation remains outstanding for RFC acceptance or these
recorded interaction exits. A future change to any public/shared contract,
support cell, or team boundary requires its own routing and affected-team
review.

## Contract propagation

| Source of truth or artifact | Impact | Required update | Completion evidence |
| --- | --- | --- | --- |
| `docs/ARCHITECTURE.md` | Affected | Replace fixture-only preview mapping with the accepted live native profile and exclusions | Complete: Section 5.0 defines the bounded relation, typed team boundaries, conservative DuckDB ownership, runtime policy/lifecycle, controlled/public evidence split, and supported cell; focused and end-to-end tests exercise that mapping |
| `docs/CONNECTOR_SPECIFICATIONS.md` | Clarification only | State that the compiled-in live snapshot does not activate draft package authoring | Complete: Section 1.4 and the distribution boundary distinguish native `CompiledConnector` metadata from YAML parsing, package loading, author tooling, compatibility, and distribution |
| `docs/RUNTIME_CONTRACTS.md` | Affected | Define live compiled metadata, typed plan, batches, network/resource policy, diagnostics, and lifetime | Complete: the native `0.3.0` mapping, DuckDB lifecycle, executor/stream, diagnostics, cancellation, and process-resident libcurl sections agree with the private C++ interfaces and permanent failure/lifecycle oracles |
| `docs/TEAM_TOPOLOGY.md` and active charters | Not affected | Existing accountabilities and interfaces already govern the split | Complete: the accepted review record and final source-dependency audit preserve all five charter boundaries and close each temporary interaction without moving accountability |
| `docs/PRODUCT_DELIVERY.md`, `AGENTS.md`, and skills | Not affected | Existing trial-graduation and topology rules correctly stopped direct promotion | No update required |
| Examples, diagnostics, fixtures, and tests | Affected | Replace fixture product example/inventory and add controlled/public live oracles | Complete: public SQL/Python examples and `0.3.0` inventory expose only the GitHub relation; fixture-era product composition and inputs are retired; focused, controlled, public, source-identity, and artifact-exclusion oracles pass |

## Deferred boundaries

- The exact canonical SDK header/stub digests and observed SSL-backend string
  are recorded and verified through `release/0.3.0/pins.json`; they instantiate
  the selected cell and do not reopen the dependency choice.
- Additional supported build cells remain outside this RFC's delivered scope
  and require their own complete dependency and product evidence.

## Review record

| Required reviewer | Team | Result | Evidence or objection | Disposition by decision owner |
| --- | --- | --- | --- | --- |
| Query permanent-delivery reviewer | Query Experience | Approved | Initial objection required separation of the fixed installed authority from the controlled loopback oracle, removal of an unproved DuckDB-visible `NOT NULL` claim, and executable retained-operator coverage; revised draft approved | Incorporated: private non-installable composition and artifact canaries are explicit; required/non-null decode behavior is separated from DuckDB logical types; controlled relational queries require one request per scan |
| Connector live-contract reviewer | Connector Experience | Approved | Initial objection required an exact native metadata subset, structural request fields and headers, connector-vs-host policy ownership, non-response provenance, preview migration, and connector-package distribution clarity; revised draft approved | Incorporated, including the exact internal operation identifier `github_search_duckdb_login_page` before snapshot freeze |
| Relational permanent-delivery reviewer | Relational Semantics | Approved | Initial objection found that `github.users` plus fixed search and `per_page=3` misrepresented the relation domain and resembled hidden predicate/limit pushdown; revised draft approved | Corrected to the bounded `github.duckdb_login_search_page` domain; source-definition constants, `R = TRUE` scope, no remote/runtime limit, and differential request/result oracles are explicit |
| Remote Runtime permanent-delivery reviewer | Remote Runtime | Approved | Approved the constrained platform dependency after requiring `CURL_VERSION_THREADSAFE` in runtime identity/gates, init-before-version-info ordering, rejected-init cleanup, and deliberately process-resident accepted state with unsupported dynamic unload/reload | Incorporated; exact digests and passing lifecycle/network-policy tests remain delivery evidence |
| Engineering Enablement live-build reviewer | Engineering Enablement | Approved | Initially objected that dependency acquisition was deferred; revised RFC selects the constrained macOS system-libcurl cell without redistribution and specifies current-cell pins, exact CMake resolution, identity verification, controlled-test exclusion, and fresh-build service | Incorporated; exact digests and successful gates remain delivery evidence rather than an unresolved decision |

## Decision and rationale

- **Technical decision owner:** Lead agent.
- **Product approval:** Approved by the product manager on 2026-07-18: the
  bounded `github.duckdb_login_search_page` preview, replacement compatibility
  policy, fixed network authority, five-second bounded-close claim, and use of
  the platform-provided libcurl/TLS implementation with no dependency bytes
  redistributed by the project.
- **Rationale:** Accepted. The completed trial establishes mechanism
  feasibility; the corrected contract defines an honest bounded relation
  domain, preserves offline/conservative relational behavior, keeps network
  authority behind Remote Runtime, and selects the smallest evidence-backed
  dependency cell. All required reviewers approved after their objections were
  incorporated.
- **Material objections:** Query's controlled-authority/test-seam objection,
  Relational Semantics' hidden-domain/limit objection, Connector Experience's
  incomplete metadata objection, Remote Runtime's process-resident curl
  objection, and Engineering Enablement's unresolved dependency-acquisition
  objection were incorporated as recorded in the review table.
- **Superseded by:** Not applicable.

## Follow-on goals

| Outcome or objective | Accountable team | Supporting teams and interaction modes | Activation condition |
| --- | --- | --- | --- |
| First live REST relation | Query Experience | Connector X-as-a-Service; Relational and Runtime Collaboration then X-as-a-Service; Enablement Facilitation | RFC 0005 Accepted with product approval recorded |

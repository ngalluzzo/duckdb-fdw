# Query Experience plan: bounded authenticated repository traversal

## Outcome and status

Deliver RFC 0007's fixed `github.authenticated_repositories` relation through
the existing `duckdb_api_scan` and explicitly named temporary-secret path. A
DuckDB user receives the required five-column duplicate-preserving row bag
across the accepted bounded page sequence, or one actionable redacted failure;
the user never supplies or observes page state.

Status: **Planned; implementation not begun**. RFC 0007 is Accepted and the
parent goal is Active. Query Experience is accountable for the DuckDB-visible
outcome. Connector Experience, Relational Semantics, and Remote Runtime remain
provider teams under the parent goal's recorded interactions.

## Query ownership boundary

Query Experience owns the DuckDB-specific edge:

- expose the new relation through the existing generic table function without
  adding a relation-specific function, page argument, URL, header, or setting;
- derive bind schema and secret-presence rules from the selected immutable
  `CompiledConnector`, construct only the conservative protocol-neutral
  `ScanRequest`, and retain the returned immutable `ScanPlan`;
- keep `DESCRIBE`, `EXPLAIN`, `PREPARE`, bind-data copy, and executor open
  deterministic and network-free, with temporary-secret lookup occurring once
  per global scan initialization as already accepted by RFC 0006;
- consume one Runtime-owned `BatchStream` through repeated pull callbacks,
  require every successful pull to contain a nonempty schema-aligned bounded
  batch, and treat `false` alone as clean source exhaustion;
- copy strict typed values into DuckDB `DataChunk`s, translate cancellation and
  structured failures once, and ensure early result close reaches cancel and
  idempotent close without another adapter pull;
- preserve DuckDB ownership of filter, ordering, limit, and offset, including
  an explicit local `ORDER BY id` only in deterministic acceptance queries;
  and
- own the controlled and privacy-safe live DuckDB narratives, static SQL
  inventory, adapter lifecycle evidence, and user-facing limitations.

Query does **not** parse or retain Link fields, construct later page requests,
count pages or aggregate transport resources, reapply bearer headers, infer
pagination from query fields, buffer the complete relation, deduplicate rows,
or claim stable ordering or snapshot isolation. A missing provider behavior is
returned to its owning team rather than implemented as an adapter workaround.

## Permanent source responsibilities

| Artifact | Query-owned responsibility | Boundary proof |
| --- | --- | --- |
| `src/duckdb_api_adapter.cpp` | Preserve generic registration, relation-aware bind, immutable bind copy, one-secret-resolution global initialization, single-task stream ownership, repeated pull, typed `DataChunk` output, interruption, close, and error translation. Strengthen the consumer check so `Next == true` with zero rows is a provider contract violation rather than a zero-cardinality DuckDB chunk. | Includes only DuckDB host APIs plus public Connector, ScanRequest/ScanPlan, secret-resolution, and execution APIs. It contains no pagination state, response metadata, URL/query builder, transport, decoder, or aggregate counter. |
| `src/include/duckdb_api/scan_request.hpp` and `src/scan_request.cpp` | Continue to build a deterministic full-projection request for the exact selected relation and logical secret name. No pagination input or capability is added to Query's request. Change production code only if the accepted provider interfaces reveal a genuine genericity defect. | Snapshots for `authenticated_repositories` contain its identity, five declared columns, conservative unavailable relational capabilities, and logical secret reference, but no page number, Link value, credential, or I/O authority. |
| `src/include/duckdb_api/product_composition.hpp` and `src/product_composition.cpp` | Assemble Connector's immutable three-relation catalog and Runtime's executor as separate provider services; update adjacent documentation from the two-relation `0.4.0` composition to RFC 0007 without learning provider internals. | Production composition calls provider factories and returns only `CompiledConnector` plus `shared_ptr<const ScanExecutor>`; it neither constructs pagination metadata nor imports `internal/` headers. |
| `src/include/duckdb_api_extension.hpp` and `src/duckdb_api_extension.cpp` | Preserve the sole installed entry point, registration order, and exception boundary. Update adjacent Query documentation only when needed for the three-relation surface; version constants remain build-owned. | The installed entry point selects only `BuildProductComposition()` and exposes no controlled authority, page selector, or fault mode. |
| `src/include/duckdb_api/duckdb_secret.hpp` and `src/duckdb_secret.cpp` | Reuse the RFC 0006 exact-name temporary-memory resolver unchanged unless regression evidence exposes a Query-owned defect. | One execution-scoped authorization capability crosses to Runtime; Query never reads its alternative or credential bytes after construction. |

Pagination requires no new Query production module. In particular, the
adapter must not acquire a `PaginationPlan` accessor, normalized response type,
or Runtime state-machine type merely to expose the new relation. Generic
metadata-driven activation is preferred over a relation-name branch.

## Query-owned tests and product evidence

| Evidence surface | Query-owned evidence |
| --- | --- |
| `test/cpp/scan_request_tests.cpp` | Exact repository relation selection; five-column full-projection closure; required logical secret; deterministic copy/snapshot; conservative projection/filter/order/limit/offset/progress capabilities; and absence of page fields, Link data, token sentinels, SQL text, and provider authority. Preserve both existing relation cases. |
| `test/cpp/duckdb_adapter_auth_bind_tests.cpp` | New relation schema and secret-presence rules; exact case-sensitive identifier selection; offline `DESCRIBE`, `EXPLAIN`, and `PREPARE`; no runtime entry; no change to the registered parameter inventory; and regressions for anonymous and authenticated-user bind behavior. |
| New focused `test/cpp/duckdb_adapter_repository_lifecycle_tests.cpp` | Five-column repeated-pull consumption; `false`-only exhaustion; rejection of a successful empty batch before it reaches DuckDB; first-batch-then-error translation; cancellation during a later pull; early `LIMIT`/result close; prepared execution, repeated and concurrent scan isolation; and local filter/order/limit/offset behavior over duplicate-preserving synthetic rows. This suite tests the adapter contract, not Link mechanics. |
| `test/cpp/support/query_runtime_scenarios.hpp` and `.cpp` plus existing Query adapter support | Add only schema-aligned five-column fake batches, empty-success contract violation, late structured failure, blocking-later-pull, and lifecycle observations. The fake never emits a Link header, constructs a request, counts a page, or simulates authority validation. |
| `test/cpp/support/controlled_product_composition.hpp` and `.cpp` and `test/cpp/controlled_duckdb_api_extension.cpp` | Assemble Connector's controlled catalog and Runtime's controlled executor through their separate public test services, then invoke the unchanged production Query registration path. Preserve the private port selector and installed-artifact exclusion. |
| New `test/python/repository_pagination_product_contract.py` and `test/python/repository_pagination_product/` package | Black-box SQL narrative over a controlled loopback service: exact five-column bag with local `ORDER BY id`, three-page traversal, empty middle page, single-page exhaustion, late status/decode/schema failure, malformed next metadata, cancellation, early close, aggregate resource failure, recovery, and redaction. Request-sequence assertions observe only HTTP facts; Runtime's independent tests remain the parser/state-machine oracle. |
| `test/sql/duckdb_api.test` | Static `0.5.0` relation inventory and schema, required-secret binder rules, offline explain/bind behavior, stable table-function parameters, and regressions for both `0.4.0` relations without requiring a live credential. |
| Query-facing example and release-contract assertions | Show temporary-secret creation and the accepted repository SQL, state the 32-page/30-second fail-closed envelope and mutable duplicate-preserving bag limitation, use local `ORDER BY` only for display, and avoid any page argument or snapshot claim. Live evidence records schema and aggregate row/page/request counts only, never repository names, IDs, private flags, Link values, or token material. |

The controlled SQL fixture may contain only synthetic repository identities.
Its service can emit Link values as black-box response input, but Query tests
must not duplicate Runtime's Link grammar or decide whether a transition is
safe. Query asserts the resulting rows, request count/targets, DuckDB error
category, cancellation, and close behavior exposed at its customer boundary.

The live check is opt-in compatibility evidence using the PM-provided
short-lived fine-grained credential. It is not a correctness oracle, must not
become a default repository/CI dependency, and must leave no credential or row
values in logs, fixtures, evidence, shell history captured by the task, or
committed files.

## Provider dependencies and dependency direction

| Provider | Query consumes | Query must not do | Observable provider readiness |
| --- | --- | --- | --- |
| Connector Experience | `BuildNativeGithubConnector`, exact relation lookup, five-column schema, authentication requirement, and immutable connector identity | Construct `CompiledPagination`, copy `/user/repos` or header constants, infer page size from query fields, activate YAML, or import Connector test internals | Public accessors expose the validated repository relation and existing relations; catalog snapshots and explanation are deterministic; a controlled catalog can be composed without Runtime owning it. |
| Relational Semantics | `BuildConservativeScanPlan` and the complete immutable `ScanPlan` returned for Query's conservative request | Construct or mutate `PaginationPlan`, inspect base-domain or consistency internals to make adapter decisions, reclassify ownership, or reconstruct unavailable SQL structure | Offline planner oracles accept the repository relation, reject inconsistent pagination, retain DuckDB relational ownership, and return the complete executable plan without I/O. |
| Remote Runtime | `ExecutionControl`, `ExecutionCancelled`, structured errors, `ScanExecutor`, nonempty-success `BatchStream`, typed batches, production executor factory, and controlled executor/observation service | Parse Link, build page requests, decorate credentials, import Runtime `internal/` headers, count resources, poll transport directly, or turn an empty page into a zero-row DuckDB chunk | Public comments and contract tests define `true => nonempty aligned batch`, `false => clean exhaustion`; the executor accepts the exact plan without I/O; DuckDB-free runtime tests prove page sequencing, bounds, cancellation, close, late failure, and redaction. |
| Lead-owned build and integration | Focused target registration, private-artifact composition, product runner invocation, version/source identity, public contract, contracts, changelog, and fresh/cached gates | Edit root `CMakeLists.txt`, `Makefile`, runner scripts, pins, authoritative contracts, source-identity lists, release identity, or another team's plan from this workstream | Every new Query source and oracle is registered exactly once; public and controlled artifacts remain disjoint; `0.5.0` identity and contract propagation agree before public activation. |

The existing private seam is a required boundary correction, not a template:
`LoopbackCurlRuntime` currently retains and exposes a `CompiledConnector`, and
`BuildControlledProductComposition` receives both catalog and executor through
that Runtime-owned object. For this goal, Runtime's controlled service must
provide the executor and observations only, Connector must provide the catalog,
and Query must assemble the two. The lead coordinates the cross-team signature
change and build graph; Query does not edit Runtime's loopback implementation
or preserve the coupling with an adapter shim.

## Disjoint parallel work

| Track | Query-owned files | May proceed when | Must wait for |
| --- | --- | --- | --- |
| Adapter consumer contract | `src/duckdb_api_adapter.cpp`, new repository lifecycle test, Query runtime scenarios | The accepted nonempty-success contract is fixed; provider fakes can exercise it without HTTP | Runtime's final public error and stream comments before closing the interface audit |
| Request and bind behavior | `test/cpp/scan_request_tests.cpp`, `test/cpp/duckdb_adapter_auth_bind_tests.cpp` | Connector publishes the relation/accessors and Semantics publishes a plan Query can consume | Final provider snapshot/schema before exact expected assertions are frozen |
| Black-box product oracle | New repository-pagination Python runner/package | New files and synthetic row assertions can be developed without production source overlap | Connector/Semantics/Runtime controlled services and the lead-owned controlled artifact target for executable evidence |
| Composition and publication audit | Query product/controlled composition files and adjacent comments | Provider factories have stable bounded interfaces | Removal of Runtime's connector-retaining controlled seam and lead integration of `0.5.0` identities |
| User-facing evidence | Query example content, SQLLogicTest assertions, privacy-safe live procedure | RFC 0007 already fixes SQL and limitations | A fresh integrated artifact; lead-owned public contract, changelog, and release identity |

These tracks are file-disjoint from Connector metadata/planner/Runtime
implementation work. Within Query, the adapter and C++ fake track must land
before its focused lifecycle test is treated as authoritative; the Python
suite must not edit the shared C++ Runtime fixtures to make a scenario pass.
Root build and runner files are integration overlaps owned by the lead, not
parallel Query writing surfaces.

## Sequencing and gates

1. **Provider-interface gate.** Connector's repository metadata and Semantics'
   offline plan compile and pass their focused oracles. Query can bind the five
   columns and logical secret through public accessors; no public artifact yet
   advertises an executor-incompatible relation.
2. **Stream-contract gate.** Runtime publishes and tests nonempty-success,
   clean exhaustion, structured errors, and no-I/O executor open. Query's fake
   lifecycle suite proves the adapter enforces that contract and translates
   violations or late failures once.
3. **Controlled-composition gate.** The connector-retaining Runtime test seam
   is removed, Query assembles separate provider services, and the private
   artifact reaches the same registration, bind/copy, plan, authorization,
   stream, error, and `DataChunk` path as production.
4. **Public-activation gate.** Only after the real Runtime consumes the exact
   repository plan does the integrated `0.5.0` composition expose the relation.
   The controlled multi-page, empty-middle-page, early-close, cancellation,
   budget, late-failure, redaction, and two-relation regression narratives pass.
5. **Repository gate.** Run the Query-focused C++ targets and Python product
   runner first, then `make build`, `make test`, `make demo`, source and native
   dependency identity checks, and a fresh native product cell. The lead stages
   the intended change before cached-diff checks so new files are covered.
6. **Acceptance gate.** Run the privacy-safe live compatibility check last;
   complete Query lifecycle/FFI review, required adversarial review, and final
   declaration/include/construction/build/test dependency audit. No interaction
   exits on test names alone.

## Code and user documentation obligations

- Beside `BatchStream` consumption, document that a successful pull is
  nonempty because DuckDB treats zero output cardinality as finished; identify
  adapter ownership of validation and Runtime ownership of same-pull empty-page
  advancement.
- Keep callback ownership explicit: immutable connector/executor in function
  info, immutable plan/executor in bind data, one stream and completion flag in
  global state, callback-local batch/control, and DuckDB-owned output vectors.
- Preserve call-scoped `ClientContext` cancellation, one execution deadline
  owned behind Runtime's service, non-throwing cancel/close/destruction, and the
  one-time exception boundary. Query documentation must not explain Runtime's
  parser or transport algorithm.
- Document product and controlled composition as assembly of separate provider
  APIs, including why loopback authority and scenario controls are absent from
  every installed target.
- User-facing text must call the result a mutable duplicate-preserving bag,
  state that local `ORDER BY` is required for deterministic presentation, and
  explain that budget exhaustion or a late page fails the statement rather
  than yielding a complete-looking partial result.
- Diagnostics and examples may name safe fields such as `pagination.next`,
  `pages`, or `response_bytes`; they must not echo Link contents, received
  destinations, repository row values, authorization headers, or credentials.
- `EXPLAIN`, `DESCRIBE`, and `PREPARE` evidence must remain offline and must not
  overstate ordering, limit, progress, or snapshot capabilities. A new public
  explain format is outside this goal unless separately governed.

## Explicit non-work

- Connector declaration syntax, catalog validation, `CompiledPagination`, and
  connector-package/YAML compatibility.
- Base-domain, consistency, ordering, limit, residual-ownership, or resource-
  intersection decisions in `PaginationPlan` and `ScanPlan`.
- Link capture or parsing, next-target validation, page state, request
  reconstruction, bearer decoration, per-page/aggregate counters, transport,
  JSON decoding, or runtime cancellation algorithms.
- Changes to temporary-secret providers, persistent/environment credentials,
  token selection, credential scope, redirects, proxies, or destination policy.
- Retry, rate-limit waiting, parallel pages, resume, caching, providers,
  GraphQL, deduplication, page-size inputs, caller URLs/headers, progress totals,
  snapshot isolation, or a public native ABI.
- Root build/release scripts, authoritative contract propagation, release
  version/source identities, public changelog, Git integration, live-secret
  custody, or another team's goal plan; these remain lead-owned handoffs.

## Observable interaction exits

- **Connector Experience — Open; Collaboration, then X-as-a-Service.** Exit
  when Query's ordinary and prepared binds consume the three-relation catalog
  through const public accessors, the five-column schema and secret rule agree
  with independent catalog oracles, controlled composition obtains the catalog
  from Connector rather than Runtime, and no Query source copies request or
  pagination metadata.
- **Relational Semantics — Open; Collaboration, then X-as-a-Service.** Exit
  when Query constructs only the conservative `ScanRequest`, retains the
  immutable returned `ScanPlan`, reads only schema/execution facts required at
  the adapter edge, and controlled SQL proves DuckDB-local filter, ordering,
  limit, and offset without Query reclassification or page-one fallback.
- **Remote Runtime — Open; Collaboration, then X-as-a-Service.** Exit when the
  adapter and Query fakes consume only documented executor/stream/batch/control
  and error APIs, `true` can never reach DuckDB with an empty batch, Runtime's
  controlled service no longer owns Connector metadata, and independent
  Runtime plus Query lifecycle tests prove cancellation, early close, late
  failure, capability release, redaction, and recovery.
- **Query Experience outcome — Open.** Exit when the exact SQL with local
  `ORDER BY id` returns the synthetic controlled five-column row bag across
  multiple pages and an empty intermediate page; all meaningful failures are
  non-truncating and redacted; both `0.4.0` relations regress cleanly; installed
  artifact inventory excludes controlled authority; privacy-safe live evidence
  confirms current compatibility; provider dependency direction is supported
  by final source/test/build evidence; and cached/fresh gates plus independent
  review have no unresolved findings.

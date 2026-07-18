# Query Experience plan: permanent live REST query path

## Outcome and status

Deliver RFC 0005's bounded `github.duckdb_login_search_page` relation through
the permanent `duckdb_api_scan(connector := ..., relation := ...)` DuckDB path.
Query Experience owns registration, bind and immutable bind-data copying,
adapter capability reporting, scan initialization, typed-batch transfer into
`DataChunk`, host cancellation and exception translation, product composition,
and the user-visible controlled and public query oracles.

Status: **Satisfied on `main`**. Commit `f834eb0` integrates the permanent
Connector, Relational Semantics, Remote Runtime, and Query graph after Query's
final public binder-context correction in `ba389a9`. Query consumes the
committed provider interfaces without copying the trial adapter, constructing
transport objects, reclassifying planner decisions, or exposing a selectable
authority.

## Owned production files

| Artifact | Query Experience responsibility |
| --- | --- |
| `src/include/duckdb_api/scan_request.hpp` and `src/scan_request.cpp` | Protocol-neutral request and DuckDB capability profile; construct the conservative full-projection request from the resolved immutable connector and relation without SQL reconstruction, I/O, environment, filesystem, or transport authority |
| `src/include/duckdb_api/product_composition.hpp` and `src/product_composition.cpp` | Sole installed composition seam; assemble the canonical Connector snapshot and Remote Runtime's production `ScanExecutor` service behind immutable provider interfaces, with no curl, decoder, DNS-policy, or test-authority knowledge |
| `src/duckdb_api_adapter.cpp` | DuckDB table-function registration, constant named-argument validation, metadata-only bind and planning call, immutable `TableFunctionData::Copy`, single-task global state, execution-control view, stream open/pull/cancel/close, typed-batch validation and `DataChunk` output, and one-time safe error translation |
| `src/duckdb_api_extension.cpp` and `src/include/duckdb_api_extension.hpp` | Installed extension identity and entry point plus the narrow registration API shared with the private controlled artifact; the installed entry point selects only `BuildProductComposition()` |
| Removed `src/example_composition.cpp`, `src/include/duckdb_api/example_composition.hpp`, and `src/include/duckdb_api/embedded_example.hpp` | Retired the fixture-only product composition and embedded response after the lead-owned build graph switched to the permanent live composition |

The adapter may retain only immutable `CompiledConnector`, `ScanRequest`,
`ScanPlan`, and `ScanExecutor` values in registration or bind state and exactly
one `BatchStream` in global scan state. Callback-local typed batches are
validated against the bound schema and DuckDB vector bound before values are
copied into DuckDB-owned vectors. No provider-internal type, request builder,
URL, header, libcurl handle, JSON parser, resolved address, or policy algorithm
belongs in these files.

Root `CMakeLists.txt`, `Makefile`, build and release scripts, dependency pins,
and shared architecture/runtime documents are integration-owned and are not
edited by this workstream.

## Owned tests, public contract, and examples

| Artifact | Query Experience responsibility |
| --- | --- |
| `test/cpp/scan_request_tests.cpp` | Provider-independent request identity, full-projection closure, conservative capability, determinism, and connector-derived-value oracles |
| `test/cpp/duckdb_adapter_tests.cpp` | Provider-fake adapter oracles for offline bind, bind-data copy, schema, typed batches, cancellation/error translation, early close, final result/context-owner teardown, repeated and concurrent scans, and DuckDB-owned relational operators |
| `test/cpp/support/query_runtime_scenarios.hpp` and `.cpp` | DuckDB-free fake `ScanExecutor`/`BatchStream` implementations and lifecycle probes used only by adapter tests; no HTTP behavior or provider policy is recreated |
| `test/cpp/support/controlled_product_composition.hpp` and `.cpp` | Private non-installable composition of the accepted controlled compiled snapshot, the public planner, and a Runtime-owned controlled executor service; Query does not construct a `ScanPlan` or import transport internals |
| `test/cpp/controlled_duckdb_api_extension.cpp` | Test-only DuckDB extension entry point that calls the same production registration/adapter code with the controlled composition |
| `test/python/live_rest_product_contract.py` and `test/python/live_rest_product/` | Direct-load controlled-service runner with separate socket support, relational, and lifecycle responsibilities for exact request and rows, offline bind/prepare, immutable prepared authority, local relational ownership, failures, cancellation, recovery, and teardown |
| `test/python/artifact_contract.py` and `test/python/source_demo_contract.py` | Installed `0.3.0` public inventory, absence of authority/test seams, direct-load identity, safe bind failures, and public GitHub compatibility demonstration |
| `test/sql/duckdb_api.test` | Accepted dispatcher signature, version, static schema/bind behavior, and migration from the removed fixture identifiers without making public-service data a deterministic SQLLogicTest oracle |
| `examples/first-live-rest-relation.sql` and `examples/first_live_rest_relation.py` | Source-built public GitHub query narrative with no exact public row identity or order claim |
| `release/0.3.0/public_contract.json` | Machine-readable extension/function/identifier/schema/diagnostic inventory and explicit removal of `example.items` |

Query owns the black-box Python HTTP service and DuckDB-visible assertions for
its user acceptance narrative. Remote Runtime separately owns the typed
loopback composition internals, socket-policy observations, and low-level
transport fixtures; the responsibility-specific harnesses share no production
authority or implementation types. Query consumes Runtime's documented
test-only factory as a public `ScanExecutor` service and does not include
runtime-internal transport headers. The lead-owned build graph may compile the
controlled entry point as a separate non-installable artifact, but the
installed `duckdb_api` artifact is always built from the production entry point
and composition.

## Provider dependencies and dependency direction

- **Connector Experience — X-as-a-Service:** provides the canonical immutable
  `github.duckdb_login_search_page` snapshot. Query resolves only identifiers,
  column names, logical types, and nullability needed for bind. It does not
  copy request constants or connector validation.
- **Relational Semantics — Collaboration, then X-as-a-Service:** provides the
  public side-effect-free planner from `CompiledConnector` plus conservative
  `ScanRequest` and host ceilings to an immutable `ScanPlan`. Query copies and
  executes the result without inspecting request authority, operation
  selection, ownership classifications, or explanation fields. The provider
  also defines how the private controlled snapshot traverses the same planner
  entry point; Query never constructs a plan.
- **Remote Runtime — Collaboration, then X-as-a-Service:** provides checked
  process-lifetime runtime initialization, immutable production executor
  construction, `ExecutionControl`, `ExecutionCancelled`, structured errors,
  `ScanExecutor`, `BatchStream`, and schema-aligned typed batches. A Runtime-
  owned test-support factory supplies the controlled executor and observations
  without exposing HTTP/curl/decode/network-policy internals to Query.
- **Engineering Enablement — Facilitation:** owns the root build composition,
  constrained platform dependency identities, test-artifact target, source
  identities, and fresh product cell. Query supplies source and oracle entry
  points but does not edit those gates or pins.

The integrated graph preserves the provider commits and their dependency
direction. Provider contract gaps were returned to their owners; Query added no
compatibility shim, duplicate request constant, or `internal/` provider-header
dependency.

## Private controlled composition and artifact exclusion

The public artifact contains only the canonical GitHub authority. It reads no
environment variable, DuckDB setting, SQL argument, file, or mutable global to
select an authority. The private controlled artifact injects its compiled
loopback snapshot and Runtime-owned service only at construction, then follows
the unchanged production registration, bind/copy, planner, executor, stream,
translation, and `DataChunk` path.

The installed-artifact oracle must prove all of the following:

- only the accepted `duckdb_api_scan(connector VARCHAR, relation VARCHAR)`
  function and `0.3.0` identity are added;
- `github.duckdb_login_search_page` is the sole accepted relation and the
  removed `example.items` identifiers fail during bind;
- no loopback authority, proof function, test factory, fault scenario, URL
  override, environment key, extra setting, or test-only symbol is present or
  selectable; and
- ambient files, environment, Python paths, proxy variables, and DuckDB
  settings cannot change the bound or executed authority.

## Completion evidence

- `LOAD`, `DESCRIBE`, and `PREPARE` perform zero controlled-service requests.
  Ordinary and copied prepared bind data freeze equal immutable requests and
  plans; changing ambient state before `EXECUTE` cannot change authority,
  schema, or request bytes.
- The base controlled scan returns the exact three required non-null values in
  strict `BIGINT`, `VARCHAR`, and `BOOLEAN` columns and uses bounded typed
  batches. Missing, null, incompatible, malformed, and resource-exhausted
  values produce the accepted redacted category without leaking response or
  dependency text.
- Controlled `WHERE`, `ORDER BY`, `LIMIT/OFFSET`, and filter-before-limit
  queries issue a byte-identical request target and exactly one request per
  executed scan. Results equal DuckDB evaluation over the same base rows,
  including a filter-before-limit fixture whose surviving row follows an
  earlier nonmatching row.
- Runtime cancellation is translated exactly once to DuckDB interruption,
  with a sub-second controlled interrupt. Success, failure, cancellation,
  early result close, repeated scans, independent concurrent scans, final
  `StreamQueryResult`/`ClientContext` owner teardown, and the accepted
  five-second bounded active close reach idempotent non-throwing cleanup
  without masking the primary outcome. Releasing a `Connection` while its
  result retains the context does not claim an earlier teardown boundary.
- Unknown connector/relation names fail during bind with safe actionable
  diagnostics and no executor open. Structured execution stages map once to
  the accepted connector/relation-aware DuckDB messages; unknown exceptions
  become redacted internal errors.
- The installed artifact executes the accepted GitHub SQL as current-service
  compatibility evidence. The demonstration asserts logical types and a
  bounded zero-to-three-row result, not public row identity or ordering.

Fresh current-tree evidence confirms those outcomes. `make test` passed during
this closure audit on the unchanged `f834eb0` product graph, including every
focused Connector, Semantics, Runtime, and Query target; 25 SQLLogicTest
assertions; installed-artifact inventory; the private controlled product's
20-request relational and lifecycle oracle; and the live public compatibility
query. Fresh
`scripts/verify-source-identities.py` reported native Connector source SHA-256
`d9cf66acedb97b0325ca9c9883afceaa91a491fe48e2f6d5d3744137f8d13e86`
and public-contract SHA-256
`f5d9a5c14ef603fef34bf7154ad2272e86742fec0af994aacfbfec4afe84c8e9`;
the 11 deterministic dependency-verifier counterexamples also passed. The
recorded fresh `make verify PROFILE=debug` rebuilt 618 targets from the exact
`f834eb0` product tree and repeated the dependency, focused, controlled,
artifact, SQL, and public evidence without reusing the developer cache.

## Interaction exits

- **Connector Experience — Satisfied; X-as-a-Service.** Product composition
  obtains the canonical immutable snapshot from `BuildNativeGithubConnector`,
  while Query request construction and bind consume only the public
  `CompiledConnector` values needed for identity and schema. Query source
  contains no duplicated request authority or Connector implementation import;
  focused request, controlled-product, and public-contract oracles agree on the
  three-column schema.
- **Relational Semantics — Satisfied; X-as-a-Service.** Ordinary and prepared
  bind construct only Query's conservative `ScanRequest`, call
  `BuildConservativeScanPlan`, and retain the returned immutable `ScanPlan`.
  Query reads output types and execution bounds needed at the adapter edge but
  neither constructs nor reclassifies predicates, ownership, ordering, limits,
  or explanation. The controlled differential oracle proves DuckDB-owned
  filter, ordering, limit/offset, and filter-before-limit behavior against a
  byte-identical one-request base scan.
- **Remote Runtime — Satisfied; X-as-a-Service.** The adapter consumes only
  `ScanExecutor`, `BatchStream`, typed batches, call-scoped execution control,
  and structured errors. It has no curl, decoder, DNS-policy, or transport
  dependency. The private composition obtains Runtime's documented loopback
  service through its test-support factory, and focused plus controlled oracles
  prove cancellation, error translation, early close, repeated/concurrent
  scans, recovery, and final owner teardown.
- **Engineering Enablement — Satisfied; facilitation ended.** The permanent and
  private artifacts, pinned dependency identities, public-artifact exclusion
  canaries, source identities, reusable `make test` path, and fresh
  `make verify PROFILE=debug` path all pass. Query owns its adapter, public
  contract, and black-box product oracles; Enablement retains the reusable
  build and identity service without becoming a Query approval queue.

Query Experience's outcome exit is **Satisfied**. The accepted SQL and safe
failure narrative pass through permanent source, the installed artifact is
free of the controlled authority seam, every provider dependency follows the
recorded direction, and final review corrections plus cached and fresh gates
support the complete lifecycle contract.

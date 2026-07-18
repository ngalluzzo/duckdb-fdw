# Query Experience plan: permanent live REST query path

## Outcome and status

Deliver RFC 0005's bounded `github.duckdb_login_search_page` relation through
the permanent `duckdb_api_scan(connector := ..., relation := ...)` DuckDB path.
Query Experience owns registration, bind and immutable bind-data copying,
adapter capability reporting, scan initialization, typed-batch transfer into
`DataChunk`, host cancellation and exception translation, product composition,
and the user-visible controlled and public query oracles.

Connector Experience has published the immutable native metadata. Query does
not implement against inferred planning or execution shapes: Relational
Semantics and Remote Runtime land their final provider commits before Query
implementation consumes those interfaces. This workstream does not copy the
trial adapter, construct transport objects, interpret planner fields, or expose
a selectable authority.

## Owned production files

| Artifact | Query Experience responsibility |
| --- | --- |
| `src/include/duckdb_api/scan_request.hpp` and `src/scan_request.cpp` | Protocol-neutral request and DuckDB capability profile; construct the conservative full-projection request from the resolved immutable connector and relation without SQL reconstruction, I/O, environment, filesystem, or transport authority |
| `src/include/duckdb_api/product_composition.hpp` and `src/product_composition.cpp` | Sole installed composition seam; assemble the canonical Connector snapshot and Remote Runtime's production `ScanExecutor` service behind immutable provider interfaces, with no curl, decoder, DNS-policy, or test-authority knowledge |
| `src/duckdb_api_adapter.cpp` | DuckDB table-function registration, constant named-argument validation, metadata-only bind and planning call, immutable `TableFunctionData::Copy`, single-task global state, execution-control view, stream open/pull/cancel/close, typed-batch validation and `DataChunk` output, and one-time safe error translation |
| `src/duckdb_api_extension.cpp` and `src/include/duckdb_api_extension.hpp` | Installed extension identity and entry point plus the narrow registration API shared with the private controlled artifact; the installed entry point selects only `BuildProductComposition()` |
| `src/example_composition.cpp`, `src/include/duckdb_api/example_composition.hpp`, and `src/include/duckdb_api/embedded_example.hpp` | Retire the fixture-only product composition and embedded response after the lead-owned build graph switches to the permanent live composition |

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
| `test/python/live_rest_product_contract.py` | Direct-load controlled-service oracle for exact request, rows, offline bind/prepare, immutable prepared authority, local relational ownership, failures, cancellation, recovery, and teardown |
| `test/python/artifact_contract.py` and `test/python/source_demo_contract.py` | Installed `0.3.0` public inventory, absence of authority/test seams, direct-load identity, safe bind failures, and public GitHub compatibility demonstration |
| `test/sql/duckdb_api.test` | Accepted dispatcher signature, version, static schema/bind behavior, and migration from the removed fixture identifiers without making public-service data a deterministic SQLLogicTest oracle |
| `examples/first-live-rest-relation.sql` and `examples/first_live_rest_relation.py` | Source-built public GitHub query narrative with no exact public row identity or order claim |
| `release/0.3.0/public_contract.json` | Machine-readable extension/function/identifier/schema/diagnostic inventory and explicit removal of `example.items` |

The controlled server and its low-level transport observations remain Remote
Runtime-owned test support. Query consumes a documented test-only factory that
returns the public `ScanExecutor` service plus bounded observations; it does
not include runtime-internal transport headers. The lead-owned build graph may
compile the controlled entry point as a separate non-installable artifact, but
the installed `duckdb_api` artifact is always built from the production entry
point and composition.

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

Provider commits are cherry-picked unchanged in dependency order. Any provider
contract gap is returned to its owner; Query does not add compatibility shims,
duplicate constants, or reach into an `internal/` header to unblock itself.

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

## Acceptance evidence

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

## Interaction exits

- **Connector Experience — X-as-a-Service:** **Open** until Query binds the
  canonical snapshot without importing connector implementation or duplicating
  request constants, and the public/controlled narratives agree on schema.
- **Relational Semantics — Collaboration, then X-as-a-Service:** **Open** until
  ordinary and prepared Query paths consume the immutable planner result
  without constructing or reinterpreting it, and the differential DuckDB
  operator oracle passes.
- **Remote Runtime — Collaboration, then X-as-a-Service:** **Open** until the
  adapter includes only public runtime service types, direct runtime tests need
  no DuckDB source, the controlled composition imports no runtime internals,
  and cancellation/error/close oracles pass.
- **Engineering Enablement — Facilitation:** **Open** until the permanent and
  private artifacts, dependency identities, exclusion canaries, source
  identities, and fresh product gate are runnable and maintained without
  Enablement approval.

Query Experience's outcome exit is satisfied only when the accepted SQL and
safe failure narrative pass through permanent source, the public artifact is
free of the controlled seam, every provider dependency follows the recorded
direction, and final review/gates support the complete lifecycle contract.

# Query Experience plan: GraphQL repository analytics

## Outcome, authority, and topology

Status: **Complete; accountable outcome and provider interactions are Satisfied**.

Query Experience owns the `0.7.0` DuckDB-user outcome: an authenticated user
can bind, inspect, prepare, and query `github.viewer_repository_metrics`,
compose ordinary SQL, cancel execution, and receive typed rows or safe failures.

Accepted [RFC 0011](../../../rfcs/0011-add-graphql-repository-analytics.md)
fixes the contract. The [Query Experience charter](../../../teams/QUERY_EXPERIENCE.md)
authorizes the adapter, lifecycle, explanation, diagnostics, composition, and
user evidence. The lead owns integration; reserved choices remain with the PM.

### Topology routing

- Accountable stream: Query Experience for the correct, explainable DuckDB
  result or redacted diagnostic.
- Connector Experience: Collaboration then X-as-a-Service for the immutable
  relation, schema/nullability, authentication, and `CompiledConnector`.
- Relational Semantics: X-as-a-Service with bounded collaboration for the
  deterministic `ScanRequest -> ScanPlan` service and safe plan facts.
- Remote Runtime: Collaboration then X-as-a-Service for authorization-envelope
  admission and the bounded, cancelable `ScanExecutor -> BatchStream` service.

## Exact scope

### In scope

- Expose the fourth native `github` relation through the existing generic
  `duckdb_api_scan` function and explicitly named temporary-secret path.
- Bind the RFC's exact ordered eight-column schema from immutable metadata,
  including nullable `primary_language VARCHAR`, with no network, credential
  resolution, or executor entry during bind, `DESCRIBE`, `EXPLAIN`, or
  `PREPARE`.
- Build the existing conservative protocol-neutral `ScanRequest`: full schema
  closure, logical secret name only, unsupported remote predicate, and absent
  projection, ordering, limit, offset, and progress delegation.
- Retain and copy the complete immutable `ScanPlan`; preserve DuckDB ownership
  of filtering, final projection, ordering, limit, and offset.
- Consume nullable typed values through the public Runtime row interface and
  write DuckDB validity without sentinel values or permissive conversion.
- Render safe GraphQL plan facts and translate structured provider failures at
  the single DuckDB exception boundary.
- Assemble installed and controlled products from bounded Connector,
  Semantics, and Runtime provider APIs without relation-specific execution.
- Own offline adapter, SQL-result, prepare/repeat, lifecycle, controlled
  end-to-end, privacy-safe live compatibility, and retained REST evidence.

### Out of scope

- GraphQL documents, parsing, generation, variables, JSON request bodies,
  bearer placement, transport requests, response-envelope decode, cursor
  transitions, request/body accounting, or any other Runtime internal.
- Connector package/YAML syntax, author tooling, native metadata construction,
  document identity/digest validation, or Connector-private fixtures.
- Base-domain, replay, operation, predicate, cursor, resource, ordering, or
  cardinality classification.
- Remote predicate/projection/order/limit pushdown, caller inputs, endpoints,
  documents, page size, cursor, retries, cache, providers, parallel pages,
  resume, deduplication, partial-data recovery, or snapshot guarantees.
- Relation-specific execution, temporal conversion for `updated_at`, public
  progress/telemetry, or a v1 stability promise for the preview name.
- Root build/release identity, authoritative contract integration, another
  team's implementation or plan, goal closure, and the retain-or-narrow choice.

## Query-owned adapter contract

### Offline bind and immutable state

Bind performs exact connector/relation lookup, validates required secret-name
presence, copies the declared names/types/nullability, builds one baseline
request, and calls the public planner. It stores only the logical secret name,
request, plan, and immutable executor service. It does not resolve the secret,
open a stream, inspect a document, or perform I/O.

`DESCRIBE`, `EXPLAIN`, `PREPARE`, bind-data copy, and optimizer callback copies
preserve that boundary. Each prepared execution resolves the current exact-name
temporary-memory secret only during global initialization and moves the opaque
authorization capability directly to Runtime. Plan copies share values, not
mutable cursor, stream, deadline, or response state.

The relation has no predicate mapping. Generic candidate translation remains
protocol-neutral; DuckDB retains every offered expression.

### Nullable value handoff

The public `TypedValue` handoff gains explicit validity while retaining the
planned scalar kind. Query records expected kind and planned nullability for
each column when global state is created.

- A valid value must match the planned kind and follows the existing strict
  `BIGINT`, `VARCHAR`, or `BOOLEAN` conversion.
- An invalid value is accepted only for a planned nullable column and marks the
  exact DuckDB vector position invalid.
- Required nulls, kind/arity mismatch, empty successful batches, and widened
  batches fail closed; Query invents no default or sentinel.
- Empty strings, zero, and `false` remain valid values and must not become
  `NULL`.
- Cancellation is checked during row transfer; terminal failure is not clean
  exhaustion.

Runtime owns whether missing or JSON-null data may produce a typed null. Query
uses only validity, kind, and planned nullability; it never reads response paths
or GraphQL error data.

### Explanation and diagnostics

Explanation is derived only from typed `ScanRequest` and `ScanPlan` facts. It
reports relation, GraphQL protocol, closed query-only operation identity,
redacted endpoint identity, sequential mutable cursor strategy, page/row/body
bounds, nullable columns, unsupported remote predicate, and DuckDB ownership of
all relational operators. It claims no stable remote order, snapshot, total,
projection pushdown, retry, or partial-data behavior.

Explanation omits document bytes, variables, cursors, credentials, response
values, rows, and remote messages. It is never parsed or used as authority.

Query exhaustively maps Runtime's structured remote-protocol failure stage and
safe structural field into the existing bounded DuckDB diagnostic form. A
GraphQL `errors` response is distinguishable from transport, HTTP, decode,
schema, authentication, authorization, policy, resource, cancellation, and
internal failures without exposing its message or data. Unknown exceptions use
the fixed internal diagnostic; interruption remains DuckDB cancellation.

## Production, test, and documentation ownership

| Artifact | Query Experience responsibility |
| --- | --- |
| `src/include/duckdb_api/scan_request.hpp`, `src/query/scan_request.cpp` | Protocol-neutral full-closure request and logical secret selector; no GraphQL operation branch |
| Proposed `src/query/duckdb/typed_value_adapter.{hpp,cpp}` | One reason to change: validate kind/validity/nullability and write DuckDB vector values |
| `src/query/duckdb/table_function_adapter.cpp` | Generic registration, offline bind, immutable state, secret resolution at init, stream pull, cancellation/close, and exception translation |
| `src/query/duckdb/scan_plan_explanation.*` | Safe rendering from typed request/plan facts, including protocol/cursor/nullability without authority inference |
| `src/query/duckdb/table_function_plan_state.*` | Independent baseline/selected request and plan copies across prepare and optimizer callbacks |
| `src/query/product_composition.cpp`, `src/include/duckdb_api/product_composition.hpp` | Assemble native catalog and executor provider services; no relation-specific construction |
| `src/query/{README.md,sources.cmake,targets.cmake}` | Discoverable ownership, source inventory, target dependencies, lifecycle and no-protocol-internals guidance |
| `test/cpp/query/scan_request_tests.cpp` | Fourth-relation lookup, schema closure, secret-name-only request, determinism, and conservative capability facts |
| Proposed `test/cpp/query/duckdb/typed_value_adapter_tests.cpp` | Valid/null matrix, required-null rejection, kind/arity mismatch, sentinel counterexamples, and vector validity |
| Existing adapter bind, plan-state, stream, auth, secret, and lifecycle tests | Offline bind/describe/explain/prepare, copy isolation, repeated scans, late failure, cancellation, close, shutdown, and safe diagnostics |
| Proposed `test/cpp/query/duckdb/graphql_adapter_contract_tests.cpp` | Query-only GraphQL schema, explanation, structured-error, and provider-API consumption oracle |
| `test/cpp/query/integration/` | Controlled product assembly from public providers through the unchanged registration and adapter path |
| Proposed `test/python/graphql_repository_analytics_product_contract.py` and package | Black-box actual-DuckDB success, null, SQL composition, failure, cancellation, recovery, REST regression, and live-probe orchestration |

Exact names may follow adjacent conventions. `src/query/sources.cmake` lists
each Query source once; focused targets never list provider production sources.

Query supplies the adapter/offline/nullability/explanation wording for
`docs/ARCHITECTURE.md` and `docs/RUNTIME_CONTRACTS.md`, plus the SQL narrative,
preview compatibility, safe failures, nullable behavior, and limitations for
README, changelog, examples, and release notes. The lead integrates those
shared documents. `docs/CONNECTOR_SPECIFICATIONS.md` remains Connector-owned.

Adjacent C++ documentation traces registration through `DataChunk` output and
states callback ownership, lifetimes, concurrency, cancellation/close,
exception boundaries, DuckDB coupling, no-I/O phases, and error ownership.

## Product oracles and privacy boundary

The deterministic controlled product oracle is the correctness authority. It
uses only synthetic users, repositories, cursors, errors, and credentials and
executes through the private controlled loadable extension. Query asserts:

- exact schema/types and nullable metadata; zero, one, and multiple pages;
- required values plus null and non-null `primary_language`, duplicates, and
  strict `updated_at VARCHAR` values;
- filtered, ordered, limited, joined, prepared, and repeated SQL with DuckDB
  owning every relational operator;
- GraphQL error with and without data, required-null/schema failure, late-page
  failure, cancellation, early close, budget failure, recovery, and redaction;
- late provider failures remain terminal, and all three REST relations retain
  their schemas, results, explanation, and lifecycle behavior.

Query consumes Runtime's controlled GraphQL executor/service and safe
observations. It does not construct or compare a GraphQL request body, parse a
response, validate cursors, or duplicate Runtime's protocol oracle.

The opt-in live GitHub probe is compatibility evidence only. It uses a
short-lived explicitly named temporary secret, strict accepted budgets, no
fixture recording, and no default CI dependency. It records only extension
identity, exact bound schema, successful completion, boolean required-null
checks, cancellation classification where exercised, and redacted failure
shape. It never records token material, document/variables, cursors, repository
names or IDs, owner login, stars, language, timestamps, row samples, raw counts,
response bodies, or remote messages.

## Dependencies, sequencing, and parallel work

1. Connector publishes the fourth relation and bounded compiled/fixture API;
   Semantics publishes the exhaustive plan and fixture services. Query may
   scaffold value and lifecycle tests meanwhile, but exact assertions wait.
2. Freeze `TypedValue` validity and structured error interfaces with Runtime.
   Then nullable conversion, explanation, and Runtime execution can proceed in
   parallel in disjoint files.
3. Query lands focused request/value/bind/explain/stream tests against provider
   services. Runtime separately proves protocol, cursor, budget, and lifecycle
   internals.
4. The lead activates native `0.7.0` composition only after Connector,
   Semantics, and Runtime services accept the same plan. Query then lands the
   controlled SQL oracle and retained REST differential.
5. Contract/product documentation, privacy-safe live evidence, independent
   review, full gates, source/target audit, and the retain-or-narrow completion
   record serialize after integrated behavior passes.

Query must not edit frozen provider APIs in parallel or invent a temporary
GraphQL plan/executor. Root composition, identities, releases, and Git history
stay lead-owned.

## Acceptance evidence

Run the focused request, adapter, stream, secret, and new GraphQL Query targets
first, then the controlled Python product contract and existing REST product
contracts. Integrated completion also requires `make build`, `make test`,
`make demo`, source and native dependency identity gates, and a fresh native
product cell, followed by agent-asset validation and both diff checks.

Independent review covers offline bind/prepare, DuckDB null validity, strict
kind conversion, explanation truthfulness, redaction, FFI/callback lifetime,
cancellation/close, SQL oracle quality, live-data privacy, and target/include
boundaries. Required adversarial review covers nullability, credentials,
resources, cancellation, terminal failure, and lifecycle behavior.

## Observable interaction exits

All interactions are **Satisfied**. The 2026-07-19 final source, include,
target, and test audits established the following:

- `duckdb_api_query_request_service` links
  `duckdb_api_connector_metadata_service` and the public relational predicate
  service only; Query includes the public Connector facade and constructs no
  Connector operation or fixture internals.
- Query adapter targets link `duckdb_api_relational_planning_service` and
  `duckdb_api_runtime_interface_service`; they do not list Semantics or Runtime
  production `.cpp` files or include provider-private headers.
- The GraphQL Query target may link
  `duckdb_api_connector_fixture_service` for catalog lookup and the public
  planner/runtime interfaces; it uses no Connector private access, Semantics
  fixture builder, Runtime protocol fixture, or direct provider source list.
- `duckdb_api_controlled_loadable_extension` is the sole private, controlled,
  direct-source integration composition; it is excluded from installation and
  is not a product or release artifact. `QUERY_CONTROLLED_COMPOSITION_SOURCES`
  retains only Query's integration entry and composition sources; Runtime owns
  `REMOTE_RUNTIME_LOOPBACK_PRODUCT_SOURCES`, and that lead-owned root target
  assembles the two explicit inventories without hiding a Runtime source in a
  Query-owned list. The ordinary static and loadable products compose only
  their declared production inventories.
- Production Query receives the native catalog from Connector, builds only a
  protocol-neutral request, receives the immutable plan from Semantics, moves
  authorization to the executor, and consumes only `BatchStream`, typed rows,
  execution control, and structured errors.
- Query contains no document/body/variable/cursor/path parser, request builder,
  response decoder, relation-name execution branch, semantic/replay
  reclassification, provider-private construction, or explanation parsing.
- Focused provider tests remain independently runnable, and Query's controlled
  product test is the only place that composes their behavior into actual
  DuckDB SQL.

No target hides direct provider production sources, Query fixtures do not
construct provider-private state, nullable authority comes from the immutable
plan, and the actual DuckDB product target composes the public planning and
controlled Runtime services. Query retains only protocol-neutral request,
adapter, typed-row, error, and DuckDB SQL responsibilities.

# Remote Runtime plan: GraphQL repository analytics

## Outcome, authority, and topology

Status: **Planned; Collaboration-to-X-as-a-Service exit remains Open**.

Remote Runtime supports Query Experience's accountable `0.7.0` outcome by
admitting Relational Semantics' complete immutable GraphQL `ScanPlan`, executing
the canonical query through the standard `ScanExecutor`, and returning the
ordinary bounded `BatchStream`. Accepted
[RFC 0011](../../../rfcs/0011-add-graphql-repository-analytics.md) fixes the
contract. The [Remote Runtime charter](../../../teams/REMOTE_RUNTIME.md) gives
this workstream authority over executable admission, authentication execution,
transport, protocol decode, cursor state, resources, cancellation, failures,
and close. The lead agent owns integration; Query Experience retains product
accountability; reserved decisions remain with the product manager.

### Topology routing

- **Accountable stream:** Query Experience for the authenticated DuckDB result.
- **Platform service:** Remote Runtime provides canonical GraphQL execution and
  the unchanged `ScanExecutor -> BatchStream` consumer boundary.
- **Provider:** Relational Semantics supplies the exhaustive immutable plan and
  a bounded plan-fixture service; Runtime does not consume Connector metadata.
- **Interaction:** Collaboration while the new plan and execution profiles are
  proven, then X-as-a-Service when real target/include dependencies show that
  Query uses only the public executor, stream, authorization, value, and error
  APIs.
- **Decision authority:** RFC 0011 and the lead agent; Runtime makes only
  reversible charter-local implementation choices within that contract.

## Exact scope

### In scope

- Exhaustively admit the REST-or-GraphQL planned-operation sum and fail closed
  on every unknown, inactive, contradictory, or unsupported alternative.
- Recognize only `GITHUB_VIEWER_REPOSITORY_METRICS_V1`; recompute and compare
  its digest and validate exact bytes, query-only kind, endpoint, variables,
  response paths, schema/nullability, cursor profile, disabled features, and
  page/scan budgets before bearer placement or I/O.
- Serialize a deterministic JSON `POST` envelope from admitted document bytes,
  fixed `pageSize = 100`, and the stream-owned nullable cursor; measure and
  debit its complete escaped bytes before bearer placement.
- Extend the private HTTP transport with an admitted non-secret body and content
  type while retaining TLS, post-DNS policy, redirects-off, ambient-authority
  denial, byte bounds, cancellation, and redaction for REST and GraphQL.
- Strictly decode GraphQL envelopes, fail every nonempty `errors` array even
  when `data` exists, decode eight typed node fields, represent nullable
  `primary_language` explicitly, and return typed page-info metadata.
- Own sequential cursor transitions, duplicate-preserving row delivery,
  request/response/body accounting, one-at-a-time pull execution, stable
  terminal failure, cancellation, close, destruction, and capability release.
- Preserve all REST admission, requests, decoding, pagination, authorization,
  resource, security, and lifecycle behavior.

### Out of scope

- Connector-package syntax, canonical profile construction, Connector
  validation/snapshots/fixtures, arbitrary documents or endpoints, generated
  selections, introspection, mutations, subscriptions, or partial-data policy.
- Operation selection, base-domain proof, replay derivation, predicate or
  residual classification, SQL ordering/limits, deduplication, snapshot claims,
  `ScanRequest`/`ScanPlan` construction, or safe plan explanation.
- DuckDB registration, binding, vector writes, SQL diagnostics, secrets lookup,
  product composition, live GitHub evidence, release records, or goal closure.
- Retry, rate-limit waiting, cache, providers, parallel pages, prefetch, resume,
  remote projection/filter/order/limit, or another scalar type.
- A relation-specific Query or Runtime entry point. Dispatch is by the closed
  planned protocol/operation identity, never relation name, SQL text, snapshot,
  document parsing, or Connector internals.

## Canonical execution boundaries

| Boundary | Required Runtime contract |
| --- | --- |
| Admission | `Open` validates the entire GraphQL executable profile and authorization alternative without DNS, sockets, transport observations, token copying, or request allocation. Identity, exact bytes, recomputed digest, and typed profile agree as one authority; a label alone grants nothing. |
| Request body | Emit fixed-order compact JSON equivalent to `{"query": document, "variables": {"pageSize": 100, "cursor": null-or-string}}`. JSON escaping is strict and deterministic. Only the cursor changes between pages. Body bytes, document, variables, and cursor never enter diagnostics. |
| Authorization and transport | Intersect the 8 KiB planned per-request body ceiling with the 16 KiB host ceiling, debit the 256 KiB scan ceiling, then place exactly one bearer header for `https://api.github.com:443/graphql`. Curl sends exact JSON bytes as `POST` with `application/json`; redirects and alternate authority remain impossible. |
| Envelope decode | Validate the complete JSON document and top-level envelope before publishing the page. Nonempty `errors` is `REMOTE_PROTOCOL`; missing/duplicate/wrong-shaped required data is schema/decode failure. Remote messages, paths containing values, response fragments, and rows remain redacted. |
| Values and nulls | Required scalars convert strictly and losslessly. `primaryLanguage: null` yields an invalid `VARCHAR` `TypedValue` retaining its planned kind; a missing field, `primaryLanguage.name` failure, or null required column fails. Existing REST values remain valid and source-compatible. |
| Cursor | Start with `cursor = null`. When `hasNextPage` is false, exhaust cleanly. When true, require a nonempty string `endCursor` not previously used; it grants only the next variable value. Validate transition before publishing that page and preserve every received node occurrence. |
| Resources and lifecycle | At most 32 pages, 100 rows/page, 3,200 rows/scan, one attempt/page, one active request, one decoded page, and batches of at most 64 rows. Cancellation checkpoints precede authorization/request/transport, occur during decode and batch transfer, and precede every next page. Every terminal path releases body, response, rows, cursor state, transport, and authorization idempotently. |

No error, cancellation, or exhausted budget becomes clean exhaustion. A later
page failure remains terminal after earlier batches and is rethrown on repeated
pulls. Automatic retry stays disabled even though each admitted page is a
query-derived replay unit.

## Production ownership

Each module has one primary reason to change; final names may adjust only if the
responsibility and dependency boundaries remain visible.

| Artifact | Remote Runtime ownership and one reason to change |
| --- | --- |
| `src/include/duckdb_api/execution.hpp`, `src/runtime/api/execution_error.cpp` | Protocol-neutral nullable typed-value validity and the safe `REMOTE_PROTOCOL` error stage; changes with the Runtime-to-Query value/error API. |
| Proposed `src/include/duckdb_api/internal/runtime/execution/graphql_plan_admission.hpp` and `src/runtime/execution/graphql_plan_admission.cpp` | Exhaustive GraphQL executable-profile validation and immutable admitted profile; changes with Runtime-supported GraphQL authority, never relational meaning. |
| Proposed `src/include/duckdb_api/internal/runtime/transport/graphql_request_body.hpp` and `src/runtime/transport/graphql_request_body.cpp` | Canonical envelope serialization, JSON escaping, exact byte measurement, and cursor-only variation; changes with outbound GraphQL encoding. |
| `src/include/duckdb_api/internal/runtime/transport/http_transport.hpp` | Protocol-neutral method/body/content-type and request-body-limit values; changes with the private transport contract, not an installed caller API. |
| `src/runtime/transport/{curl_transfer,curl_response_accumulator,curl_http_transport}.cpp` and adjacent headers | Execute admitted GET or POST bytes, bound uploads, preserve exact origin/security settings, and contain curl state; changes with dependency transport behavior. |
| Proposed `src/include/duckdb_api/internal/runtime/decoding/graphql_response_decoder.hpp` and `src/runtime/decoding/graphql_response_decoder.cpp` | One strict GraphQL envelope traversal producing typed rows plus page info; changes with response/error/null conversion rules. Generic REST JSON decode remains separate. |
| Proposed `src/include/duckdb_api/internal/runtime/pagination/graphql_cursor_pagination.hpp` and `src/runtime/pagination/graphql_cursor_pagination.cpp` | Nullable current cursor, bounded seen set, forward transition, loop denial, and exhaustion; changes with cursor state rules and grants no request authority. |
| `src/include/duckdb_api/internal/runtime/policy/scan_resource_accounting.hpp` and `src/runtime/policy/scan_resource_accounting.cpp` | Add per-page/scan serialized-body debits to the existing overflow-safe ledger; changes with operational accounting and preserves response/row/memory/deadline ownership. |
| `src/include/duckdb_api/internal/runtime/authentication/fixed_github_user_bearer_authenticator.hpp` and implementation | Decorate an already admitted, body-accounted canonical request while the token remains opaque; changes with fixed GitHub credential placement. |
| Proposed `src/include/duckdb_api/internal/runtime/execution/graphql_paginated_scan.hpp` and `src/runtime/execution/graphql_paginated_scan.cpp` | Compose body, authorization, transport, envelope decode, cursor, batches, terminal failure, cancellation, and close for any admitted GraphQL profile; changes with protocol stream lifecycle, not one relation. |
| `src/runtime/execution/http_scan_executor.cpp` and its header | Exhaustive REST/GraphQL dispatch through standard open methods; changes with supported executor alternatives and contains no relation-specific entry point. |
| `src/runtime/{README.md,sources.cmake,targets.cmake}` and `test/cpp/runtime/{sources.cmake,targets.cmake}` | Discoverable ownership, source inventories, focused services, and enforceable dependencies; changes with package composition and maintainer routes. |

The existing Link parser/paginator and root-array REST decoder receive
regression work only. GraphQL error handling does not get folded into the
generic JSON parser, and GraphQL cursor state does not reuse Link targets.

## Provider fixture and oracle ownership

Runtime focused tests consume GraphQL plans only through
`duckdb_api_semantics_fixture_service` and its public
`semantics/support/graphql_scan_plan_test_fixtures.hpp`. Semantics supplies one
valid immutable plan plus isolated identity/digest, protocol, response,
cursor, resource, ownership, and nullability counterexamples. Runtime does not
include Connector fixture headers, call the planner, construct a catalog or
plan, or compile Semantics production sources.

Runtime owns these deterministic oracle families:

| Test module or target | Owned evidence and one reason to change |
| --- | --- |
| Proposed `graphql_plan_admission_tests.cpp` | Valid profile admission; every isolated counterexample and unknown alternative fails at open with zero body, token, transport, DNS, or socket observation. |
| Proposed `graphql_request_body_tests.cpp` | Exact first/later envelope bytes, escaping, body ceilings at exact/+1, aggregate debit, oversized cursor, and body failure before bearer placement. |
| Proposed `graphql_response_decoder_tests.cpp` | Zero/one/100 nodes, duplicates, nullable language, strict eight-column conversion, complete syntax, errors-only, data-plus-errors, empty errors, and missing/duplicate/wrong/null required fields. |
| Proposed `graphql_cursor_pagination_tests.cpp` | Terminal page, nonempty next cursor, empty/null/wrong-type/repeated cursor rejection, 32-page boundary, and unchanged operation facts. |
| Proposed `graphql_scan_executor_tests.cpp` | Exact request sequence, empty pages, 64/36 draining, no prefetch, late terminal failure, cancellation at each checkpoint, repeated failure, close/destruction, stream isolation, and retained REST behavior. |
| Proposed `curl_graphql_transport_tests.cpp` plus existing curl targets | Real-wire POST/body/content-type/header evidence, request and response budgets, TLS/address/ambient-policy denial, cancellation, cleanup, and absence of document/cursor/credential canaries from diagnostics. |
| `execution_contract_tests.cpp`, decoder/resource/policy tests, and all existing executor/Link/curl targets | Nullable batch alignment, error-stage stability, expanded body ledger, and complete REST regression. |

Positive end-to-end Runtime evidence is a null-cursor first request followed by
one accepted cursor request, occurrence-preserving typed rows including SQL-null
handoff, clean false-only exhaustion, one bearer placement per page, and exact
resource counters. Negative evidence includes canonical admission drift,
mutation/subscription/extra-operation profiles, body overflow, GraphQL errors
with and without data, malformed envelopes/types/nulls, invalid/repeated
cursors, status/auth failures, response/row/memory/deadline exhaustion,
cancellation, and destruction after partial output. All failure assertions use
safe stage/field values and synthetic canaries that must be absent from text.

## Dependencies, parallelism, and serialization

```text
Semantics scan-plan service -> Runtime executor service -> Query public executor/stream
Semantics fixture service   -> focused Runtime tests
Runtime controlled service  -> Query integration tests through public executor only
```

1. Connector and Semantics first publish the frozen planned-operation value and
   GraphQL fixture service. Runtime does not create a substitute plan builder.
2. Freeze `execution.hpp` null/error semantics and `http_transport.hpp` body
   semantics before Query or multiple Runtime tracks depend on them.
3. Admission, body serializer, envelope decoder, cursor state, and curl support
   may proceed in parallel in disjoint files after their APIs are frozen.
4. Resource accounting and GraphQL stream integration serialize around their
   shared state; executor dispatch follows after all component oracles pass.
5. Runtime and Query then integrate in parallel against the public APIs. The
   lead alone owns root composition, shared-contract propagation, version and
   source identity, release records, final review, Git history, and goal close.

One writer owns each shared header, CMake inventory, controlled observation
type, and stream state machine. No temporary direct source list, private fixture
include, local `ScanPlan` constructor, relation-name switch, or document parser
is an acceptable parallelization shortcut.

## Code and contract documentation obligations

Adjacent public and private APIs document owner/consumers, exact inputs and
outputs, pre-`1.0` compatibility, immutable versus stream-owned lifetime,
threading, concurrency one, body/resource authority, cancellation checkpoints,
terminal error ownership, redaction, close idempotence, and destruction.
Comments explain validation/body-debit before bearer placement, why canonical
identity/bytes/digest are indivisible, why cursor metadata grants no authority,
why errors precede page publication, and why typed null retains scalar kind.

Runtime supplies the lead with executable-admission, body transport, envelope,
cursor, null, resource, error, and lifecycle wording for
`docs/RUNTIME_CONTRACTS.md` and corresponding execution-boundary wording for
`docs/ARCHITECTURE.md`. `src/runtime/README.md` routes maintainers to the new
modules and focused targets. Connector syntax/specification, Query SQL/examples,
release notes, and product diagnostics remain outside Runtime ownership.

## Acceptance evidence

- All proposed focused targets and every existing Runtime target pass, including
  REST request, Link pagination, strict decode, authorization, security,
  resource, cancellation, and lifecycle regressions.
- The valid and negative Semantics fixtures drive production admission; exact
  controlled and real-curl observations prove request order, bytes, bounds,
  authorization ordering, terminal behavior, and canary redaction.
- Integrated `make build`, `make test`, `make demo`, source/dependency identity
  gates, and a fresh native product cell pass; the lead adds supported-cell
  sanitizer evidence required for transport/lifecycle changes.
- `ruby scripts/validate-agent-assets.rb`, `git diff --check`, staged whitespace
  validation, final target/include/source audit, and independent adversarial
  review of authority, body budgets, cursor, null, resource, cancellation, and
  close have no unresolved evidence-backed finding.

## Observable Collaboration to X-as-a-Service exit

The exit becomes **Satisfied; X-as-a-Service** only when the final tree proves:

1. `duckdb_api_runtime_executor_service` links only
   `duckdb_api_runtime_interface_service` and `duckdb_api_scan_plan_service`;
   its sources include no Connector, Query, DuckDB adapter, planner, or
   Semantics-private header/source.
2. Focused Runtime GraphQL targets link
   `duckdb_api_semantics_fixture_service` and include only its public GraphQL
   fixture header; they link neither Connector metadata/fixtures nor relational
   planning and do not list provider production `.cpp` files.
3. `duckdb_api_scan_plan_service` remains the sole production plan dependency;
   Runtime admission consumes the exhaustive public value and never constructs,
   retains, parses, or reinterprets Connector state or relational meaning.
4. Query adapter/stream targets include only public `authorization.hpp`,
   `execution.hpp`, and `http_runtime.hpp` Runtime surfaces and link the narrow
   Runtime service; no `duckdb_api/internal/runtime/*` header, Runtime source,
   request/body/cursor/decoder type, or relation-specific GraphQL open path
   appears in Query.
5. Any Runtime controlled-fixture target exposes only a standard executor plus
   bounded scripted response/request observations; Query tests do not construct
   the transport, admitted profile, cursor state, decoder, or Runtime budgets.
6. Source and oracle inventories independently exercise admission, body,
   envelope, cursor, resource, transport, and lifecycle responsibilities; all
   positive/negative and retained REST targets pass with adjacent documentation.

The interaction stays Open if a target name hides direct provider source
compilation, a consumer imports private construction, Runtime branches on
`viewer_repository_metrics`, Query learns GraphQL execution state, or routine
profile changes still require cross-boundary edits outside the documented plan,
fixture, executor, stream, authorization, value, and error APIs.

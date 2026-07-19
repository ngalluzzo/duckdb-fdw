# Remote Runtime source ownership

Owning charter: [Remote Runtime](../../docs/teams/REMOTE_RUNTIME.md).

This tree implements the reusable, bounded remote-execution service. It accepts
an immutable Relational Semantics `ScanPlan`, matches an explicit authorization
capability, and returns the stable synchronous `BatchStream` contract. It owns
transport, authentication execution, decoding, pagination state, network and
resource enforcement, cancellation, and stream lifecycle. It does not construct
connector metadata, plan relational meaning, or depend on DuckDB adapter state.

## Team APIs

Remote Runtime consumes:

- Relational Semantics' immutable `ScanPlan`; Runtime validates only whether a
  complete plan is admitted by the installed execution profile and never
  reinterprets predicate, ordering, limit, or residual ownership.
- Query Experience's call-scoped `ExecutionControl` and move-only
  `ScanAuthorization` envelope.

Remote Runtime provides the stable headers in `src/include/duckdb_api/`:

- `authorization.hpp` for the closed move-only authorization capability;
- `execution.hpp` for redacted failures, cancellation, typed batches,
  `BatchStream`, and `ScanExecutor`; and
- `http_runtime.hpp` for process-lifetime curl initialization and the immutable
  executor composition.

Private headers mirror these packages under
`src/include/duckdb_api/internal/runtime/`. Consumers must not import those
headers as a team API.

## Package responsibilities and dependency direction

- `api/` implements the stable authorization and execution-error contracts.
- `authentication/` materializes the fixed bearer header after plan/capability
  admission and enforces the request-header envelope.
- `decoding/` validates complete JSON syntax and projects strict typed rows
  within record, string, nesting, deadline, and retained-memory budgets.
- `execution/` admits immutable plans, constructs isolated streams, and owns
  pull, cancellation, terminal failure, close, and page lifecycle state.
- `pagination/` validates URI and Link grammar, then applies the separate fixed
  sequential transition policy. Received targets never become requests.
- `policy/` enforces socket-address policy and aggregate page/scan resource
  accounting.
- `transport/` owns curl process lifetime, fixed HTTPS transport composition,
  easy-handle configuration, callbacks, response accumulation, and HTTP/1.1
  chunk framing.

Dependencies point from execution toward the narrower authentication,
decoding, pagination, policy, and transport services. Transport may consume
policy predicates and budgets but cannot grant plan or credential authority.
Pagination transition policy consumes generic Link and URI grammar; grammar
does not depend on the GitHub transition profile. Production Runtime depends on
the public `ScanPlan` value only, never Connector, Query, or Semantics internals.

## Implementation units

| Unit | Primary reason to change |
| --- | --- |
| `api/authorization.cpp` | The closed authorization capability's ownership, validation, or destruction contract changes. |
| `api/execution_error.cpp` | Stable Runtime error, cancellation, typed-value, or batch mechanics change. |
| `authentication/fixed_github_user_bearer_authenticator.cpp` | Fixed bearer placement or header-envelope enforcement changes. |
| `decoding/json_decoder.cpp` | Accepted JSON syntax, typed projection, schema conversion, or decoded-memory accounting changes. |
| `execution/http_plan_admission.cpp` | The installed immutable-plan admission profile changes. |
| `execution/http_scan_executor.cpp` | Single-response stream construction, pull state, or executor dispatch changes. |
| `execution/http_paginated_scan.cpp` | Sequential paginated stream state, page request reconstruction, or page lifecycle changes. |
| `pagination/uri_reference.cpp` | RFC URI/URI-reference grammar changes. |
| `pagination/link_header.cpp` | Generic Link field-value grammar changes. |
| `pagination/link_pagination.cpp` | Fixed next-target authority or sequential transition-state policy changes. |
| `policy/network_policy.cpp` | Address-family or private/link-local/loopback enforcement changes. |
| `policy/scan_resource_accounting.cpp` | Page/scan reservation, commitment, exhaustion, or deadline accounting changes. |
| `transport/http_runtime.cpp` | Curl process lifetime or installed HTTP runtime composition changes. |
| `transport/curl_http_transport.cpp` | Fixed request-to-curl profile mapping or socket-policy binding changes. |
| `transport/curl_transfer.cpp` | Curl easy-handle configuration, transfer orchestration, or terminal error classification changes. |
| `transport/curl_response_accumulator.cpp` | No-throw callbacks, response/header accumulation, metadata retention, or callback counters change. |
| `transport/http_chunk_decoder.cpp` | HTTP/1.1 chunk framing grammar or decoded-body ceiling enforcement changes. |
| `decoding/decoded_page_buffer.hpp` | Header-only decoded-page release and ownership accounting changes. |
| `policy/request_header_budget.hpp` | Header-only aggregate request-header byte accounting changes. |

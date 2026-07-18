# Remote Runtime plan: permanent bounded HTTPS execution

## Outcome and status

Provide Query Experience with the permanent DuckDB-free execution service for
RFC 0005's one fixed live REST relation. The service consumes the immutable
Relational Semantics plan, performs at most one policy-authorized HTTP/1.1
request, strictly decodes schema-aligned typed batches, and exposes bounded,
cancelable, redacted stream behavior. This workstream owns no DuckDB adapter,
product composition, connector declaration, or relational classification.

The branch is `goal/0.3-live-rest/runtime` in the isolated
`.worktrees/first-live-rest/runtime` worktree. Provider commits from Connector
Experience and Relational Semantics are cherry-picked unchanged before runtime
implementation consumes their declarations.

## Permanent module ownership

| Artifact | Remote Runtime responsibility |
| --- | --- |
| `src/include/duckdb_api/execution.hpp` and `src/execution_error.cpp` | DuckDB-free typed values, schema-aligned batches, execution control, pull stream, executor, and redacted structured error contract |
| `src/include/duckdb_api/http_runtime.hpp` and `src/http_runtime.cpp` | Process-lifetime libcurl initialization and capability verification, rejected-init cleanup, deliberately process-resident accepted state, and the production executor factory for the fixed installed authority |
| `src/include/duckdb_api/internal/http_transport.hpp` | Protocol-neutral one-attempt request/response boundary used by the executor and private deterministic tests |
| `src/include/duckdb_api/internal/curl_transfer.hpp` and `src/curl_transfer.cpp` | Shared one-attempt curl algorithm: HTTP/1.1 pinning, TLS verification, redirect/proxy/auth/cookie/netrc disablement, post-DNS socket callback, body/header/deadline ceilings, cancellation, and redaction |
| `src/include/duckdb_api/internal/curl_http_transport.hpp` and `src/curl_http_transport.cpp` | Installed HTTPS-only fixed-authority composition and public-address/port policy; contains no caller-selected authority or loopback path |
| `src/include/duckdb_api/internal/network_policy.hpp` and `src/network_policy.cpp` | Address parsing and denial of loopback, private, link-local, multicast, unspecified, reserved, mapped, and transition destinations after DNS resolution |
| `src/include/duckdb_api/internal/json_decoder.hpp` and `src/json_decoder.cpp` | Strict JSON syntax, `$.items[*]` selection, required non-null lossless field conversion, nesting/string/record/memory exhaustion, and schema-safe errors |
| `src/include/duckdb_api/internal/http_scan_executor.hpp` and `src/http_scan_executor.cpp` | Executable-plan capability validation, one-request stream state, whole-response bounded decode, batch backpressure, cancellation, progress, and idempotent non-throwing close |
| `test/cpp/execution_contract_tests.cpp` | Typed batch and stable redacted error contract oracles |
| `test/cpp/network_policy_tests.cpp` | Public and denied IPv4/IPv6 address-class oracles, including mapped and transition forms |
| `test/cpp/json_decoder_tests.cpp` | Strict syntax, field conversion, and every decode-budget boundary |
| `test/cpp/http_scan_executor_tests.cpp` | Attempt, batching, persistent-deadline, valid-plan/private-profile record-authority narrowing, invalid-profile construction rejection, cancellation, plan-capability rejection, failure staging, and close/recovery oracles |
| `test/cpp/curl_http_transport_tests.cpp` | Exact request, real hostile proxy/netrc exclusion, exact private option inventory, response-cookie isolation across fresh scans, status/redirect/disconnect, resolved-address denial, and one-socket/multiple-address oracles |
| `test/cpp/curl_http_budget_tests.cpp` | Malformed JSON, wire/header ceilings, and compressed exact/+1 decompressed-byte oracles |
| `test/cpp/curl_http_lifecycle_tests.cpp` | Concurrent checked initialization, fixed five-second transfer deadline, concurrent close, and recovery oracles |
| `test/cpp/curl_tls_security_tests.cpp` and `test/python/runtime_curl_tls_tests.py` | Real-curl trusted-peer success plus untrusted-peer and hostname-mismatch TLS counterexamples against an isolated Python TLS service |
| `test/cpp/support/controlled_http_transport.hpp` and `.cpp` | Non-installable scripted transport and observations used only by focused runtime tests |
| `test/cpp/support/loopback_curl_runtime.hpp` and `.cpp` | Non-installable numeric-loopback composition that exercises the shared curl algorithm and executor; the implementation and its permissive socket policy are excluded from every loadable target |
| `test/cpp/support/controlled_socket_service.hpp` and `.cpp`, `runtime_http_test_support.hpp` and `.cpp`, and `private_curl_probe.hpp` and `.cpp` | Reusable controlled socket, execution, and private curl-profile support; the private probe and `DUCKDB_API_PRIVATE_CURL_TESTS` definition are excluded from every production object, and installed/loadable artifacts must not contain marker `duckdb_api_private_curl_option_observer_v1` |
| `test/fixtures/runtime_tls/` | Deterministic non-production CA/server certificate and base64 PKCS#8 DER key for the isolated TLS harness; excluded from all artifact source inventories |

Build graph, root scripts, release identities, product composition, DuckDB
adapter code, public SQL, and durable product documentation belong to the lead,
Engineering Enablement, or Query Experience integration packages and are not
edited in this worktree.

## Provider boundary and dependency direction

Remote Runtime consumes `duckdb_api/scan_plan.hpp` as the only source of
executable authority. Relational Semantics owns operation selection, the base
domain, predicates, residual ownership, ordering, and limits. Runtime validates
only executable facts needed for safe I/O: the supported operation and schema,
fixed request structure, applied network/resource ceilings, and disabled
capabilities. It neither reconstructs a canonical plan nor reclassifies
relational meaning.

Connector Experience owns the immutable declarations from which Semantics
builds the plan. Runtime does not parse packages, select relations, or retain
connector implementation objects. Query Experience receives a documented
`ScanExecutor`/`BatchStream` service and schema-aligned typed batches without
curl, JSON parser, DNS-policy, or connector knowledge.

The production factory contains the sole installed authority and has no URL,
environment, SQL, DuckDB-setting, file, mutable-global, or loopback override.
The protocol-neutral transport constructor is private implementation surface;
only test-owned composition may supply the controlled loopback transport.

## Security, resource, and lifecycle oracles

- The production executor accepts only the exact RFC 0005 HTTPS host, path,
  ordered query, headers, schema, and applied policy; mutation fails before I/O.
- Every resolved address is checked immediately before socket connection.
  Redirects, proxy discovery, credentials, netrc, cookies, filesystem access,
  process authority, connection reuse, HTTP/2, retry, pagination, and cache are
  absent or explicitly disabled.
- One stream owns at most one wire attempt. Response, header, decompressed,
  record, string, nesting, decoded-memory, batch-row, wall-time, and concurrency
  ceilings fail closed at their exact boundaries. The installed execution
  profile fixes decoded-record authority at three, private profiles can only
  narrow it, and a plan wider than its executor fails before I/O. One deadline
  starts with the first execution work and remains fixed across every later pull.
- Cancellation is polled by transfer and decoding. `Cancel`, `Close`, and
  destructors are idempotent and non-throwing; no failure crosses the native
  boundary with URL, authority, response bytes, or dependency text.
- The dependency gate proves libcurl 8.7.1, the pinned SSL-backend identity,
  and `CURL_VERSION_THREADSAFE` out of process. Before registration, the process
  runtime performs exactly one checked `curl_global_init(CURL_GLOBAL_DEFAULT)`,
  then safely verifies the initialized identity. Rejection balances cleanup
  immediately. Initialization is never query-lazy. Accepted state is
  deliberately process-resident and left to OS process reclamation; service,
  extension, DSO, and `atexit` teardown register no cleanup hook because total
  curl-user shutdown ordering is unproved. Dynamic unload/reload is unsupported.
- Focused deterministic tests prove success, exact request observations, real
  hostile proxy/netrc exclusion, structurally observed auth/cookie option
  inventory, response-cookie isolation across fresh scans, TLS peer and hostname
  verification, denied resolved addresses, one-socket multiple-address containment,
  status/redirect/malformed/oversized/gzip/disconnect/deadline/cancellation
  failures, recovery, active-stream teardown ordering, and redaction. Public
  GitHub is compatibility evidence only and is not a correctness oracle.

## Interaction exits

- **Relational Semantics — Satisfied; X-as-a-Service.** The integrated graph
  keeps immutable `ScanPlan` representation and planner validation in the
  Semantics source group, and the planner and plan-contract targets prove that
  boundary independently. Runtime includes the public plan contract and its
  focused executor target consumes the accepted operation, schema, fixed
  request, and applied ceilings without importing planner implementation or
  recomputing predicate, residual, ordering, or limit ownership. Semantics
  retains relational classification and plan ownership; Runtime owns only
  fail-closed executable-policy validation and execution.
- **Query Experience — Satisfied; X-as-a-Service.** The integrated DuckDB
  adapter retains only the immutable connector and plan plus `ScanExecutor`,
  `BatchStream`, typed batches, execution control, and structured errors. It
  has no curl, JSON-decoder, DNS-policy, or transport dependency; permanent
  composition obtains the executor through `InitializeHttpRuntime()`. The
  private controlled product reuses that adapter and proves cancellation,
  error, early-close, and teardown behavior through the same service boundary.
  Query retains registration, bind, DuckDB value transfer, and diagnostic
  translation; Runtime retains transport, decoding, bounds, cancellation, and
  stream cleanup.
- **Engineering Enablement — Satisfied; facilitation ended.** The permanent
  CMake graph names Runtime interface, implementation, and private curl-test
  source groups explicitly. Only transport-bearing production, controlled,
  identity, and Runtime curl/TLS targets link the pinned platform libcurl; the
  focused adapter target remains curl-free. Cached and fresh product scripts
  run the direct executor, request, budget, concurrent-initialization,
  fixed-deadline, concurrent-close/recovery, and TLS oracles. They require the
  private option-observer marker in its private test target and reject it from
  installed and loadable artifacts. Runtime owns those lifecycle and security
  contracts; Enablement retains the reusable dependency-identity, build, and
  artifact-inventory gates without becoming Runtime's quality owner or a
  permanent approval queue.

All three interactions are closed by the integrated product graph at
`f834eb0`. The focused Semantics, Runtime, and Query targets, the private
controlled-service path, the installed-artifact seam canaries, and the fresh
native product gate now agree with the final source and link dependencies.

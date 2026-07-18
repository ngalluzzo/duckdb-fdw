# Remote Runtime plan: bounded HTTP-to-batch service

## Outcome and status

Provide Query Experience with one bounded, cancelable, redacted HTTP-to-row
stream for the fixed native REST plan. The transport, decoder, stream,
destination policy, and controlled failure/lifecycle oracle are implemented
and independently reviewed for this trial cell.

## Runtime-owned workstream

| Artifact | Responsibility |
| --- | --- |
| `src/include/live_rest/runtime.hpp` | DuckDB-free transport, cancellation, executor, row, and pull-stream contracts |
| `src/http_scan_runtime.cpp` | Canonical-plan validation, one-attempt execution, batch ownership, cancellation, and idempotent close |
| `src/json_decoder.cpp` and internal header | Strict JSON syntax, schema conversion, nesting, record, string, and response-budget enforcement |
| `src/curl_http_transport.cpp` | Fixed GET, TLS, proxy/auth/redirect/replay disablement, wall/body limits, cancellation callbacks, and redacted dependency failures |
| `src/network_policy.cpp` and internal header | Pre-socket post-DNS IPv4/IPv6 destination enforcement |
| `test/http_scan_runtime_tests.cpp` | Stream attempt, batch, cancellation, failure, and close behavior |
| `test/json_decoder_tests.cpp` | Strict syntax, conversion, and exhaustion behavior |
| `test/network_policy_tests.cpp` | Public, loopback-oracle, private, special-purpose, mapped, and transition address classes |
| `test/support/http_service.py` and `test/integration_failures.py` | Exact request and deterministic status, redirect, malformed, oversized, disconnect, timeout, interruption, peer-abort, recovery, and connection-close deadline evidence |

No runtime module imports DuckDB. The adapter supplies only a call-scoped
cancellation view and consumes `BatchStream`; the runtime never reconstructs
relational meaning or reads connector packages.

## Dependencies and parallel boundary

Remote Runtime begins from the immutable `LiveScanPlan` contract and can build
and test decoder, stream, transport, address policy, and failure oracle without
editing the DuckDB adapter. Query Experience can build the adapter against the
runtime header while the controlled server remains the Remote-owned shared
oracle. Relational Semantics owns plan construction and snapshot meaning.

The source split is intentional: address policy, transport, decode, stream,
and DuckDB lifecycle have separate modules and focused tests. A future change
that forces two teams to edit the same implementation file is a boundary smell
and requires the topology and RFC assessment before work starts.

## Acceptance evidence

- One stream performs at most one HTTP request and never uses HTTP/2 replay,
  redirects, retries, ambient proxies, ambient credentials, cookies, or
  connection reuse.
- Public authority resolves only to globally routable unicast addresses; the
  controlled profile permits exactly `127.0.0.1`.
- Response bytes, rows, strings, nesting, batch size, and wall time have hard
  bounds.
- Status, transport, decode, resource, interrupt, and unexpected dependency
  failures expose no URL, authority, path, response canary, or raw dependency
  diagnostic.
- Cancellation aborts the peer sub-second, timeout aborts it at the hard
  deadline, and connection close waits safely for that deadline. Prompt
  close-driven cancellation remains unproven.

## Interaction and RFC exit

The Query collaboration remains open until an accepted production contract
replaces these trial-only interfaces and Query can consume it without libcurl,
JSON-parser, or network-policy knowledge. Libcurl is proven on one macOS cell;
its production selection, portability, initialization, and support matrix
require the promotion RFC.

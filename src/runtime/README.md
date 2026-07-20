# Remote runtime

This package executes an immutable `ScanPlan` as a bounded, cancelable stream
of typed batches. It owns authentication execution, HTTP transport, decoding,
pagination, network and resource enforcement, and stream lifecycle.

Runtime accepts a complete plan and explicit authorization capability. It does
not construct connector metadata, classify relational operations, or depend on
DuckDB callback state.

Repository admission converts the complete Semantics handoff into one immutable
`AdmittedRepositoryRequestProfile`. That profile owns the canonical operation,
six-column schema, pagination target, and the optional closed
`visibility=private` input. Request construction, bearer placement, decoding,
and Link validation consume only that profile or an exact derived request;
they do not inspect relational predicates or Connector declarations.

Admission exhaustively validates the structured predicate-decision values and
the DuckDB-owned execution envelope, but does not reclassify them. The installed
GitHub `0.6.0` repository operation admits only Connector's validated Superset
decision with its supported typed input. Exact remains a valid Semantics state
for a controlled, non-installable proof operation and fails this installed
profile rather than relabeling the GitHub source. Unsupported and Ambiguous
decisions with no input produce the same unrestricted request. The typed
conditional input is the sole predicate-derived request authority. Superset may
retain either the selected predicate or the opaque complete DuckDB filter;
fallback reasons admit only the retained-filter scopes that the Semantics
decision service can emit. Unknown or contradictory structured values,
unsupported operation/input pairs, and delegated projection, filtering,
ordering, or bounds fail before authorization materialization or network I/O.

## Directory guide

| Directory | Change it for |
| --- | --- |
| `api/` | Authorization and stable execution/error value behavior |
| `authentication/` | Credential placement and request-header enforcement |
| `decoding/` | JSON syntax, typed projection, conversion, and decoded-memory ownership |
| `execution/` | Plan admission, executor dispatch, pull state, cancellation, failure, close, and page lifecycle |
| `pagination/` | URI and Link grammar or the fixed sequential next-page policy |
| `policy/` | Network-address rules and page/scan resource accounting |
| `transport/` | Curl lifetime, request configuration, callbacks, response accumulation, and HTTP framing |

Public consumer contracts are
[`authorization.hpp`](../include/duckdb_api/authorization.hpp),
[`execution.hpp`](../include/duckdb_api/execution.hpp), and
[`http_runtime.hpp`](../include/duckdb_api/http_runtime.hpp). Headers under
`duckdb_api/internal/runtime/` are private. Production and test inventories are
in the package `sources.cmake` and `targets.cmake` files.

## Before changing execution code

- Admit the complete plan and authorization capability before materializing
  credentials or starting side effects.
- Treat predicate category, accuracy, reason, remote/residual vocabulary, and
  explanation as producer facts to validate, never as an alternate request
  builder. Only the typed conditional input may select the admitted fixed field.
- Keep credentials inside opaque, move-only capabilities bound to approved
  authenticators, placements, and hosts. Diagnostics must remain redacted.
- Treat terminal failure as failure, never clean exhaustion.
- Keep `Cancel` and `Close` non-throwing and idempotent. Each scan owns an
  isolated stream and call-scoped `ExecutionControl`.
- Keep pagination sequential. A received Link target is validated and converted
  back into typed plan state; it never becomes a request directly.
- A selective repository Link must preserve `visibility=private` exactly on
  every page; omission, change, duplication, or extra query fields fail closed.
- Preserve explicit page and scan resource ownership and release ordering.
- Curl initialization has process lifetime. Dynamic unload/reload is not a
  supported cleanup boundary.
- Keep private curl observers confined to test targets and out of product
  artifacts.

The exact state machines, error taxonomy, resource authority, and protocol
rules are in [RUNTIME_CONTRACTS.md](../../docs/RUNTIME_CONTRACTS.md).

## Tests

Tests mirror the production directories under `test/cpp/runtime/`:

- `api/` covers authorization, batches, errors, and stream contracts;
- `policy/`, `pagination/`, and `decoding/` contain deterministic unit oracles;
- `execution/` uses controlled transports and Semantics-owned plan fixtures;
- `transport/` contains real-curl, TLS, budget, pagination, policy, and lifecycle
  oracles;
- `support/` contains bounded test services and private probes, not production
  APIs.

| Change area | Focused targets |
| --- | --- |
| Authorization and stream values | `duckdb_api_authorization_contract_tests`, `duckdb_api_execution_contract_tests` |
| Network and resource policy | `duckdb_api_network_policy_tests`, `duckdb_api_scan_resource_accounting_tests` |
| URI and Link pagination | `duckdb_api_uri_reference_tests`, `duckdb_api_link_pagination_tests` |
| JSON and decoded-page ownership | `duckdb_api_json_decoder_tests`, `duckdb_api_json_root_array_decoder_tests`, `duckdb_api_decoded_page_buffer_tests` |
| Executor and page lifecycle | `duckdb_api_http_scan_executor_tests`, `duckdb_api_http_scan_executor_policy_tests`, `duckdb_api_http_scan_pagination_tests` |
| Curl transport and lifecycle | Targets beginning `duckdb_api_curl_` in `test/cpp/runtime/targets.cmake` |

`make test` runs every focused Runtime executable. Run `make build` before
invoking a target from `<build_root>/extension/duckdb_api/`, where `build_root`
is printed by `make paths`. Run `make verify` before handoff on the supported
product cell.

Security, credential, network, resource, pagination, concurrency, or lifecycle
changes require the independent review defined in [AGENTS.md](../../AGENTS.md).
Apply the [RFC triggers](../../docs/RFC_PROCESS.md) when the change makes a
durable contract decision; an internal refactor or strict restoration of an
existing contract does not require an RFC by default. Repository workflow is
in [CONTRIBUTING.md](../../CONTRIBUTING.md), and maintainer accountability is
recorded in the [Remote Runtime charter](../../docs/teams/REMOTE_RUNTIME.md).

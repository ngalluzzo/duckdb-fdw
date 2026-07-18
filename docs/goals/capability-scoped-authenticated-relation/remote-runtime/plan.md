# Remote Runtime plan: capability-scoped bearer execution

## Outcome and status

Provide Query Experience with the DuckDB-free Remote Runtime service accepted
by RFC 0006: a caller can move one resolved temporary-secret snapshot into a
least-authority bearer capability, open one isolated scan, and transmit that
credential exactly once as `Authorization: Bearer ...` to the fixed
`https://api.github.com/user` operation. Runtime validates the complete
executable authorization and network envelope before decoration, preserves the
anonymous `0.3.0` relation, and owns bounded cancellation, redacted failures,
and cleanup of every transient credential-bearing object.

This is the Runtime workstream for the Query Experience outcome **First
capability-scoped authenticated relation**. RFC 0006 is Accepted. The branch is
`goal/0.4-auth/runtime` in the isolated `.worktrees/auth-delivery/runtime`
worktree. Implementation and the Runtime-to-Query interaction exit are
**Open**; this plan does not claim delivered behavior.

Remote Runtime remains a supporting platform team. It does not acquire product
accountability, DuckDB secret-manager coupling, connector metadata ownership,
or relational decision authority.

## Accepted boundary and invariants

- The only credential-bearing public Runtime value is a move-only,
  execution-scoped authorized-bearer capability. It is not copyable, has no
  plaintext accessor, and cannot be widened by host, header, authenticator, or
  operation parameters supplied by Query.
- Query resolves and validates the named DuckDB secret at execution
  initialization, then moves the token bytes into the Runtime-owned factory.
  Runtime receives no `ClientContext`, DuckDB secret entry, secret-manager
  handle, storage-provider object, or DuckDB exception.
- The immutable `CompiledConnector`, `ScanRequest`, `ScanPlan`, bind state,
  explanation snapshot, and structured diagnostics contain no credential
  bytes. Runtime consumes the Semantics-owned plan without reconstructing or
  reclassifying relational meaning.
- The effective credential authority is the intersection of the fixed Runtime
  profile and the plan-declared policy: bearer authentication, exact
  `Authorization` placement, `GET /user`, and HTTPS `api.github.com:443`.
  Connector or plan policy may narrow authority but cannot widen Runtime's
  fixed profile.
- Plan, destination, operation, placement, duplicate-header, and capability
  validation complete before the bearer value is materialized as an HTTP
  header. Post-DNS address validation occurs immediately before socket creation;
  a denied address can have a transient local header buffer but cannot receive
  a connection or byte.
- Redirects, proxy and pre-proxy discovery, netrc, cookies, caller headers,
  environment and filesystem credential lookup, connection sharing, retry,
  cache, pagination, and fallback remain absent or explicitly disabled.
- One stream owns one immutable plan snapshot, one authorization snapshot, one
  deadline, one response/decode state, and at most one wire attempt. Concurrent
  streams share no mutable authentication state and can use different token
  snapshots.
- `Cancel`, `Close`, and destruction are idempotent and non-throwing. Every
  success, rejection, allocation failure, transport failure, HTTP failure,
  cancellation, early close, and destructor path releases the capability,
  authorized request, curl header list/easy handle, response, decoder, and
  batches without replacing the original failure.
- Runtime makes no secure-zeroization or hostile-process claim. It minimizes
  lifetime and retained copies, and it never puts token bytes into process-
  global curl state, connection keys, telemetry, logs, errors, fixtures, or
  retained scan state after close.

## Provider and consumer contracts

| Producer | Consumer | Contract and dependency rule |
| --- | --- | --- |
| Connector Experience | Relational Semantics; indirectly Runtime | Immutable credential-free relation and policy declarations. Runtime does not parse YAML, select relations, construct Connector internals, or receive a DuckDB secret name or value. |
| Relational Semantics | Remote Runtime and Query Experience | Immutable `ScanPlan` with the selected relation/operation, exact auth requirement, bearer/host/header policy, typed destination, schema, disabled features, and applied ceilings. Runtime reads only public typed accessors and never derives cardinality, predicates, residual ownership, ordering, limits, or offsets from request fields. |
| Remote Runtime | Query Experience | Runtime-owned authorization factory and opaque move-only capability; `ScanExecutor` open service; bounded `BatchStream`; typed batches; call-scoped `ExecutionControl`; structured redacted errors; checked HTTP runtime initialization. Query includes only documented public Runtime headers and never imports bearer, transport, curl, DNS-policy, or decoder internals. |
| Query Experience | Remote Runtime invocation | A validated token snapshot moved once into the Runtime factory, the immutable plan, and a call-scoped cancellation view. Query does not construct an `Authorization` header, supply a destination or placement to the capability factory, retain capability internals, or pass DuckDB objects across the service boundary. |

The authorization type and its construction/consumption rules are Runtime team
API even though Query supplies the token bytes. A public constructor or factory
may accept ownership of a string, but it must bind the fixed accepted scope
inside Runtime and expose no way to read, clone, serialize, compare, log, or
retarget the secret. The executor consumes an explicit move-only authorization
envelope whose alternatives are anonymous execution or the one authorized
bearer capability; a nullable or optional plaintext string is not an acceptable
service contract.

## Permanent module ownership

The exact source split may be refined while preserving these reasons to
change. Do not fold credential authority into a catch-all execution or adapter
module.

| Artifact | Remote Runtime responsibility |
| --- | --- |
| New public authorization header and implementation under `src/include/duckdb_api/` and `src/` | Define the opaque move-only authorized-bearer capability, fixed-scope ownership-taking factory, moved-from/invalid behavior, non-throwing destruction, and absence of any general plaintext accessor. Keep representation and token storage out of headers where practical. |
| `src/include/duckdb_api/execution.hpp` and `src/execution_error.cpp` | Extend the documented executor-open service to consume the move-only authorization envelope; preserve the DuckDB-free control/stream contract; add stable `AUTHENTICATION` and `AUTHORIZATION` stages while retaining existing failure containment. |
| New internal fixed-bearer authenticator module | Be the sole code allowed to consume capability contents. Validate the final structural request and plan policy, reject unsafe header-value bytes and duplicate/case-variant `Authorization`, append exactly one canonical bearer header, and return only a transient internal request. |
| `src/include/duckdb_api/internal/http_scan_executor.hpp` and `src/http_scan_executor.cpp` | Validate both anonymous and authenticated executable profiles before I/O, require exact authorization presence/absence, move each capability into one stream, preserve first-pull network behavior, choose the declared response shape, map status stages, and release authorization state on every stream transition. |
| `src/include/duckdb_api/internal/http_transport.hpp` | Keep the decorated request and sensitive header inside a private Runtime boundary. If sensitivity metadata is added, it must prohibit diagnostic formatting rather than create a generic credential-placement facility. |
| `src/include/duckdb_api/internal/curl_http_transport.hpp` and `src/curl_http_transport.cpp` | Admit only the two installed fixed request profiles, select only their compiled-in public URLs, reject structural drift before curl, and pass the already-authorized request unchanged. Do not derive a URL from caller text or add an authority override. |
| `src/include/duckdb_api/internal/curl_transfer.hpp` and `src/curl_transfer.cpp` | Preserve per-call easy handle and curl-slist ownership, disabled redirect/proxy/netrc/cookie/share/user-password state, TLS checks, zero DNS cache, fresh non-reused connection, post-DNS socket policy, cancellation, ceilings, and raw dependency-error redaction while carrying the transient bearer header. |
| `src/include/duckdb_api/internal/json_decoder.hpp` and `src/json_decoder.cpp` | Add the fixed successful `/user` object shape without weakening the existing `$.items[*]` array path, strict required-field conversion, or decode budgets. Decode mode follows the already validated operation; it does not infer relational cardinality from response shape. |
| `src/include/duckdb_api/http_runtime.hpp` and `src/http_runtime.cpp` | Document the `0.4.0` service composition and preserve checked process-global curl initialization. No token or authorization state enters the service object or process-resident lifetime owner. |

`network_policy.cpp` remains the post-DNS address oracle unless evidence
requires a contained correction; authentication does not justify a broader
address policy. Product composition, DuckDB adapter and secret registration,
Connector/Semantics implementations, root build scripts, release identities,
public SQL, and authoritative contract propagation are integration-owned and
must not be edited in the Runtime implementation package merely to make its
focused tests pass.

## Authorization and stream state machine

1. Query resolves one DuckDB secret snapshot and transfers token ownership to
   the Runtime authorization factory. Factory construction rejects empty or
   structurally unsafe HTTP header values without exposing the rejected value.
2. `ScanExecutor::Open` checks cancellation, validates the full immutable plan
   against one of the two installed profiles, and intersects the plan's auth,
   destination, redirect, network, and resource policy with fixed Runtime
   authority.
3. Anonymous plans require an explicit no-credential envelope and reject any
   capability. `authenticated_user` requires one valid capability and rejects
   missing, moved-from, mismatched, or surplus authority. All mismatches fail
   before stream allocation reaches transport and before any header exists.
4. Open checks cancellation again and moves the plan and authorization into a
   newly isolated stream. It performs no DNS lookup, socket creation, or HTTP
   request and does not retain the call-scoped control view.
5. The first `Next` initializes the single execution deadline, builds the
   structural request without credential bytes, rechecks the final exact
   operation/destination/placement envelope, and only then invokes the fixed
   bearer authenticator.
6. The authenticator consumes the capability and appends exactly one
   `Authorization: Bearer ...` header. The capability becomes empty; the
   authorized request owns the only Runtime request copy until transport
   returns or throws.
7. Curl constructs a per-call header list/easy handle. The socket callback
   checks the resolved address before connection. Redirect following remains
   off, so a `3xx` is one failed response and no second destination can receive
   the header.
8. On return or exception, request and curl-owned header buffers are destroyed
   before later pulls. Successful decode may remain for bounded batch delivery,
   but it retains no capability or header. `401` becomes `authentication`,
   `403` becomes `authorization`, and neither response body is retained.
9. Close before first pull destroys the unused capability. Concurrent close
   publishes cancellation before waiting for the active pull; bounded transfer
   exit destroys the request/header/capability state before close clears decoded
   rows. Repeated cancel/close/destruction cannot restart or replay work.

Code comments beside the implementation must explain why destination and
placement checks precede decoration, why the DNS check necessarily follows
local header construction yet still precedes transmission, and why a fresh
non-shared curl handle prevents credential state from crossing scans.

## Diagnostic and redaction contract

- Runtime `authentication` covers invalid/missing Runtime authorization and
  remote `401`; `authorization` covers remote `403`. Executable profile,
  destination, placement, duplicate-header, and address denials remain `policy`
  failures. Query owns DuckDB secret lookup/type/provider/persistence messages
  and the final adapter translation.
- Safe Runtime errors may carry a stable category, safe field identifier, and
  remote status. They never carry the logical secret name, token, bearer value,
  request dump, response body, URL assembled from untrusted text, dependency
  diagnostic, or unrestricted upstream message.
- Curl must discard non-success bodies before status translation. Unknown
  exceptions remain a bounded generic transport/internal error. Cancellation
  remains the dedicated marker and cannot be replaced by cleanup failure.
- Runtime introduces no public auth metric or progress claim. Any private test
  observation that captures the exact bearer value is an explicit secure oracle
  surface, never a diagnostic or retained production observation.

## DuckDB-free oracle families

| Oracle family | Required evidence |
| --- | --- |
| Public capability contract | Compile-time traits prove non-copyability, move ownership, and non-throwing destruction. Public headers expose no token getter, serialization, comparison, host/header setter, or generic secret map. Moved-from and duplicate-use attempts fail closed without token text. |
| Open and policy ordering | A valid anonymous plan opens only without a capability; a valid authenticated plan opens only with one capability. Wrong relation, method, path, host, port, scheme, authenticator, placement, duplicate `Authorization`, redirect flag, feature, schema, or budget fails before transport observation and before bearer decoration. Open remains network-free. |
| Exact bearer request | A controlled transport and real loopback curl service observe one `GET /user`, exact fixed non-secret headers, and exactly one canonical bearer header. The anonymous request has no authorization header. Token bytes never appear in plan/explanation/error observations. |
| Simultaneous snapshot isolation | Create distinct runtime-generated token A and token B capabilities, open two streams around a synchronization barrier, and interleave their first pulls. Each recorded request and synthetic identity must match its own token regardless of execution order; closing/canceling one stream cannot alter or release the other's authority. No shared mutable auth state is permitted. |
| Wrong destination and placement | Provider-owned plan fixtures or public plan builders produce wrong-host/header/authenticator/operation counterexamples. Each returns a stable policy error with zero transport calls. Runtime tests must not construct or reinterpret private Semantics state to obtain these cases. |
| DNS and redirect denial | A private real-curl probe builds the authorized header locally, then a deny-all address callback proves zero server connections. A first controlled service returning a redirect points at a separate recording sink; the first service sees one bearer request, the sink sees zero connections, and the stream returns one redacted non-success failure with no replay. |
| Ambient authority exclusion | Under hostile upper/lower-case proxy variables, hostile `HOME`/`CURL_HOME` netrc, and cookie-setting responses, authenticated curl still reaches only the controlled destination. Exact option inventory proves proxy/pre-proxy empty, netrc ignored, built-in and proxy auth disabled, unrestricted auth off, redirects off, no cookie/share/user-password options, fresh connection, forbidden reuse, and zero DNS cache. A later scan emits neither cookie nor prior bearer token. |
| Status and redaction | `401` and `403` map separately, fail rather than return zero rows, and discard bodies. Transport, policy, allocation, decode, schema, cancellation, and unknown failures exclude token, complete `Authorization` value, response/body canaries, curl text, and captured wire. |
| Lifecycle and cleanup | Deterministic private lifetime observations plus concurrent tests cover factory failure, open rejection, close before pull, success, status failure, transport failure, decode failure, cancellation during transfer/decode/delivery, concurrent close, repeated close/cancel, destruction, and recovery. Every case releases exactly its own capability/header/transfer state, performs at most one attempt, and leaves the shared executor usable. |
| Response decoding | Fixed `/user` success yields one schema-aligned row with strict `BIGINT`/`VARCHAR`/`BOOLEAN` conversion; missing, duplicate, null, wrong-type, malformed, oversized, deep, and overlong values fail under the existing budgets. Existing search-page decoding remains unchanged. |

Private lifetime, curl, loopback, and wire-capture seams must compile only into
focused Runtime tests and carry negative artifact canaries. They must not add a
caller-selected authority or credential hook to installed objects.

Credential canaries are generated at runtime and are distinct from connector,
relation, field, and logical-secret names that may legitimately appear in other
layers. Tests preflight that the complete canary is absent before injection,
compare exact wire values without including them in assertion text, and report
only the affected surface name or digest on failure. The explicit controlled
wire capture is excluded from the absence scan while Runtime errors, safe
observations, logs, and evidence are included. This prevents both leakage by a
failing oracle and false positives caused by a committed literal or allowed
diagnostic identity.

## Test and documentation placement

- Keep capability API/lifetime tests in a dedicated focused Runtime target or a
  clearly isolated authorization section of `execution_contract_tests.cpp`.
- Keep plan/capability intersection, bearer decoration, simultaneous snapshots,
  status mapping, and stream ownership in `http_scan_executor_tests.cpp` plus
  Runtime-owned controlled transport support. Extend support to record a
  thread-safe request sequence rather than a single lossy last observation.
- Keep real libcurl header transmission, redirect-sink, DNS denial, hostile
  proxy/netrc/cookie, option inventory, and cross-scan leakage in
  `curl_http_transport_tests.cpp` and private curl/socket support.
- Keep concurrent cancel/close/destruction and post-failure recovery in
  `curl_http_lifecycle_tests.cpp`; keep single-object strict decoding in
  `json_decoder_tests.cpp`.
- Reuse existing TLS, byte-budget, network-address, dependency-identity, and
  artifact-inventory oracles. Authentication must deepen them where credential
  transmission changes risk, not duplicate them in an end-to-end Query suite.
- Add adjacent API documentation for capability purpose/scope, construction,
  move ownership, concurrency, allowed consumer, token lifetime, error
  ownership, cancellation, close, destruction, and zeroization limits. Document
  the executor authorization envelope and the bearer/DNS safety ordering beside
  their declarations and state machines.

## Delivery sequence and evidence

1. Consume the accepted Connector and Semantics provider interfaces without
   altering their ownership. Establish valid anonymous/authenticated plan
   fixtures and negative policy cases through their documented APIs.
2. Add the opaque authorization API and executor-open ownership change with
   public contract tests. Prove that Query can compile against the service
   without internal Runtime includes before transport behavior is added.
3. Add fixed bearer validation/decoration, authenticated executor state, status
   categories, and strict single-object decode. Make controlled transport tests
   green while preserving all anonymous Runtime tests.
4. Extend the installed curl transport to the second fixed public profile and
   deepen real-curl DNS, redirect-sink, ambient authority, cookie, TLS, budget,
   and option-inventory oracles.
5. Complete simultaneous-snapshot, cancellation, close, destruction, redaction,
   recovery, and private-seam artifact canaries. Inspect the final source,
   include, construction, and test dependencies against this responsibility
   map.
6. Run focused Runtime targets first, then `make build`, `make test`, `make
   demo`, `scripts/verify-source-identities.py`,
   `python3 -I -B scripts/test-native-dependencies.py`, and a fresh
   `make verify PROFILE=debug` after integration supplies the authoritative
   `0.4.0` build graph and product contracts.
7. Obtain at least two fresh adversarial perspectives covering credential
   exfiltration/redirect/DNS/oracle design and concurrency/cancellation/cleanup.
   Resolve evidence-backed findings, rerun focused checks, and repeat the
   topology exit audit on the final integrated graph.

## Interaction exits

- **Query Experience — Open; Collaboration → X-as-a-Service.** Exit when Query
  resolves DuckDB secrets and moves token bytes through the documented Runtime
  factory, opens and consumes streams through public authorization/execution
  APIs, and translates structured errors without constructing auth headers or
  importing Runtime internals. Runtime must import no DuckDB header or secret
  object. Query's end-to-end rotation/drop/cancellation narrative and Runtime's
  DuckDB-free capability/lifecycle tests must agree.
- **Relational Semantics — Open; Collaboration → X-as-a-Service.** Exit when
  Runtime consumes public immutable plan accessors for the exact executable
  auth/network envelope, focused tests use provider-owned plans or builders,
  and no Runtime source constructs, mutates, reparses, or reclassifies
  relational or policy internals.
- **Connector Experience — Open; Collaboration → X-as-a-Service.** Exit when
  Runtime receives credential-free policy only through the accepted plan,
  neither imports Connector construction internals nor accepts a secret or
  DuckDB name from metadata, and wrong-policy tests demonstrate fail-closed
  consumption without bespoke coordination.

The Runtime interaction is not satisfied merely because an opaque type exists
or an end-to-end query passes. Exit requires the final declarations, public
headers, includes, factories, stream state, build targets, focused tests, and
code documentation to show that Runtime independently owns authorization and
transport enforcement while Query remains a low-friction consumer. Until that
audit passes, all three collaborations remain Open.

## Explicit non-work

This Runtime workstream does not register or query DuckDB secrets, define SQL
arguments, own prepared-statement catalog behavior, modify connector authoring
syntax, construct `ScanPlan`, decide exactly-one relational semantics, add
OAuth or refresh, accept persistent/environment/file credentials, enable caller
URLs or headers, add redirects/proxies/retries/cache/pagination, claim secure
zeroization, broaden the supported DuckDB/libcurl cell, publish artifacts, or
change public distribution policy. Any evidence requiring one of those changes
returns to the lead agent for scope and RFC assessment rather than being hidden
inside Runtime.

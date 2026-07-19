# Remote Runtime plan: bounded authenticated repository pagination

## Outcome and status

Status: **Implemented; provider interactions exited; fresh verification open**.

Provide Query Experience with RFC 0007's permanent DuckDB-free execution
service for `github.authenticated_repositories`. Runtime consumes one complete
immutable `ScanPlan` and one moved GitHub bearer authorization capability,
executes the fixed first request and only strictly validated sequential next
pages, and returns bounded nonempty typed batches until clean source
exhaustion. Malformed continuation metadata, a failed page, cancellation,
close, deadline, or resource exhaustion terminates the stream without a
complete-looking partial result.

This is a supporting platform workstream for the Query Experience outcome
**Bounded authenticated repository traversal**. RFC 0007 is Accepted and the
product goal is Active. Remote Runtime owns normalized Link capture, Link
parsing and page state, repeated authorization execution, transport, decoding,
aggregate accounting, backpressure, cancellation, close, and structured
redacted failures. It does not acquire product accountability, Connector
metadata construction, relational interpretation, DuckDB adapter behavior, or
release integration.

The permanent service must preserve both `0.4.0` execution profiles while
adding the exact `0.5.0` repository profile. A Runtime that cannot execute the
complete pagination, authorization, schema, network, and resource contract
rejects the plan before I/O; it never degrades to page 1.

### Delivered evidence

- The permanent Runtime is split across normalized Link transport metadata,
  typed Link pagination, scan resource accounting, root-array decoding, and a
  dedicated paginated scan service. `http_scan_executor.cpp` retains the two
  one-response profiles and dispatches the accepted repository plan without
  absorbing the new state machine.
- Focused DuckDB-free targets pass for RFC 3986 URI-reference structure, Link
  list/relation grammar, and authority denial,
  exact/+1 page and scan budgets, decoded-page capacity release, root-array
  schema and limits, controlled request sequences, empty intermediate pages,
  64+36 row draining before page 2, cancellation, close, late failure, and
  preserved one-response behavior. Real-curl targets pass for terminal Link
  capture and three fresh sequential authorized connections.
- Runtime no longer constructs or exposes Connector metadata. The controlled
  composition obtains `BuildNativeGithubConnector()` and Runtime's executor as
  separate providers, while Query consumes only the public execution and
  authorization APIs.
- The cached native cell built and linked all registered Runtime targets, then
  `make test` passed every focused target, the SQL suite, installed-artifact
  inventory, and all three controlled product oracles. `make demo` also passed
  with the public `0.5.0` artifact.
- The first repository active-close product run exposed a real integration
  defect: the private loopback Runtime profile had inherited the public 30
  second scan ceiling, so close settled after 30.273 seconds. Restoring the
  controlled-only profile to the existing five-second
  `MAX_EXECUTION_MILLISECONDS` cap preserved the production 30-second plan,
  made the unchanged active-close oracle pass in 5.97 seconds, and was followed
  by a green full cached `make test`.
- Source identity, dependency-verifier unit tests, native developer guards,
  native formatting, and staged/unstaged whitespace checks pass. A fresh native
  product cell and the final independent adversarial-review set remain lead
  integration evidence rather than provider-interaction blockers.

## Runtime ownership boundary and invariants

- Executor open consumes only public immutable `ScanPlan` accessors and a
  move-only `ScanAuthorization`, validates the complete installed-profile
  intersection, and performs no DNS lookup, socket acquisition, secret lookup,
  or HTTP request. The first `BatchStream::Next` starts the one scan deadline
  and page 1.
- One stream owns one plan snapshot, one authorization snapshot, one sequential
  page state machine, one aggregate budget tracker, at most one decoded page,
  one output batch being transferred, and at most one active request. Separate
  scans share only immutable executor and transport services.
- Response Link data can select only the typed value of the next page after
  validation against the plan. It cannot grant scheme, host, port, path,
  query-name, header, authenticator, credential, redirect, or retry authority.
  Runtime reconstructs a canonical request from immutable operation facts and
  never transmits the received target string.
- Every accepted page is one new replay unit with one attempt. A failed page is
  terminal and is never tried again. Pagination is not retry, and no emitted or
  uncommitted page is automatically replayed.
- `Next == true` means a schema-aligned batch containing at least one row;
  `false` alone means clean exhaustion. Empty nonterminal pages are released
  and crossed within the same pull under the same cancellation signal,
  deadline, and aggregate budgets.
- The next request cannot start while the current page has unconsumed decoded
  rows. Pull controls progress; there is no prefetch, producer queue, parallel
  page task, or scan-wide row materialization.
- Connector ceilings and the Semantics-owned host intersection may narrow the
  installed Runtime profile but never widen it. Budget debit and remaining-
  allowance calculation use overflow-safe arithmetic, and reaching a ceiling
  with an advertised next page is an error rather than silent exhaustion.
- Validation precedes request reconstruction, reconstruction and final request
  validation precede bearer decoration, and transport policy precedes socket
  creation. TLS verification, post-DNS address policy, disabled redirects,
  disabled ambient proxy/netrc/cookie/auth authority, and request-header
  budgeting apply independently to every page.
- Link contents, received destinations, repository values, authorization
  headers, credential bytes, response bodies, and dependency diagnostics never
  enter plans, safe observations, logs, errors, snapshots, or committed
  fixtures. Safe failures use existing stages with bounded fields such as
  `pagination.next`, `pages`, `header_bytes`, `response_bytes`, or
  `wall_milliseconds`.
- `Cancel` publishes cancellation without waiting. `Close` publishes
  cancellation before synchronizing with an active pull, is idempotent and
  non-throwing, and prevents every later request. Success, failure,
  cancellation, close, and destruction release the authorization snapshot,
  authorized request/header, Link metadata, body, decoded rows, counters, and
  per-call curl state without replacing the original outcome.

## Permanent source responsibilities

| Artifact | Remote Runtime responsibility | Boundary evidence |
| --- | --- | --- |
| `src/include/duckdb_api/execution.hpp` and `src/execution_error.cpp` | Document and enforce the provider contract that successful `BatchStream::Next` is nonempty and aligned, `false` is clean exhaustion, and cancel/close/destruction remain non-throwing. Preserve existing stable error stages; invalid continuation is a redacted policy failure rather than a new DuckDB-facing parser type. | Query compiles against execution, typed-batch, control, and structured-error APIs only and needs no pagination include or response type. |
| `src/include/duckdb_api/authorization.hpp`, `src/authorization.cpp`, and `src/include/duckdb_api/internal/fixed_github_user_bearer_authenticator.hpp` plus its implementation | Change the fixed GitHub bearer capability from one decoration to one scan-owned authorization snapshot that can decorate each accepted request. Keep it move-only, non-readable outside the authenticator, closed to the accepted installed operations, and released on every terminal path. Revalidate plan, final destination, operation, placement, duplicate headers, and header budget before every decoration. | Query still constructs one capability and moves it once. No public token getter, clone, destination setter, generic header placement, or credential map is introduced; simultaneous scans retain isolated snapshots. |
| `src/include/duckdb_api/internal/http_transport.hpp` | Extend the private one-attempt response with one narrow normalized metadata value containing only final-response `Link` field-values in receipt order plus bounded accounting. Do not expose a raw libcurl response, status-block buffer, general header map, parser decision, or received URL authority. Extend per-request limits only as needed to cap retained metadata by the remaining page/scan memory allowance. | The transport interface remains protocol-neutral and DuckDB-free; parser and executor tests can inject normalized Link metadata without curl. |
| New `src/include/duckdb_api/internal/uri_reference.hpp` and `src/uri_reference.cpp` | Own syntax-only RFC 3986 URI-reference and URI validation, including hierarchical authority, IP-literal, path, query, fragment, and percent-encoding structure. Perform no resolution, normalization, DNS, or request construction. | A focused curl-free target covers valid relative, hierarchical, rootless, IPv6, IPvFuture, and IPv4-tail forms plus malformed authority, path, percent, fragment, and URI-only counterexamples. |
| New `src/include/duckdb_api/internal/link_pagination.hpp` and `src/link_pagination.cpp` | Own the independently testable combined Link field-value parser, bounded empty-list handling, first-`rel` semantics, strict next-target validation, typed positive page extraction, exact-increment and seen-page checks, and canonical next-request patch. Consume the URI syntax service and a Runtime execution policy derived from public `PaginationPlan` accessors; never import Connector declarations or accept caller URL/header authority. | Pure tests map normalized metadata and current typed state to either one canonical next page, exhaustion, or a stable redacted policy error. No DuckDB, curl, secret, or native catalog constructor is needed. |
| New `src/include/duckdb_api/internal/scan_resource_accounting.hpp` and `src/scan_resource_accounting.cpp` | Own scan counters, per-page/aggregate intersection, overflow-safe debit, remaining allowances, one-deadline handling, and fail-closed exhaustion for requests, pages, headers, wire/decompressed bytes, decoded records, captured metadata, and decoded memory. Batch rows and active request concurrency remain instantaneous ceilings rather than cumulative totals. | Focused boundary tests exercise exact and +1 values without HTTP or DuckDB; the executor asks this service for narrowed page limits and reports completed usage instead of duplicating counter arithmetic. |
| `src/include/duckdb_api/internal/curl_transfer.hpp` and `src/curl_transfer.cpp` | Preserve the one-attempt curl algorithm while capturing only case-insensitive `Link` field-values from the terminal response header section after ordinary header-byte accounting. Bound retained metadata before allocation, reset interim-header state correctly, and return no raw framing or dependency objects. Preserve all TLS, DNS, timeout, cancellation, redirect/proxy/netrc/cookie/share, fresh-handle, and redaction controls. | Real-curl tests prove exact capture order, terminal-block selection, header and metadata ceilings, cleanup, and absence of capture from non-Link fields or later diagnostics. |
| `src/include/duckdb_api/internal/curl_http_transport.hpp` and `src/curl_http_transport.cpp` | Add only the exact fixed authenticated repository request profile. Validate method, typed HTTPS authority, canonical `/user/repos?per_page=100&page=N` target, fixed non-secret headers, and exactly one bearer header before composing the installed URL from fixed authority plus the already reconstructed canonical target. Preserve the anonymous search and authenticated-user profiles. | No caller or response string can select authority; invalid page targets fail before curl and real-wire oracles prove one fresh policy-checked request per accepted page. |
| `src/include/duckdb_api/internal/json_decoder.hpp` and `src/json_decoder.cpp` | Add the explicit root-array response source and strict five-column repository decode while retaining full-document validation, lossless BIGINT conversion, required/non-null extraction, cancellation/deadline checkpoints, and page record/string/nesting/memory ceilings. Response shape and columns come from an already validated plan, not relation-name or extractor inference inside the decoder. | Decoder tests cover empty/root arrays, exact 100 records, +1 records, five field types, duplicates/missing/null/wrong types, string/memory limits, malformed input, cancellation, and all existing response modes. |
| `src/include/duckdb_api/internal/http_scan_executor.hpp` and `src/http_scan_executor.cpp` | Validate all three installed operations and their complete plan/profile intersections, derive Runtime-only pagination/decoder/budget policies from public plan facts, and orchestrate page request, authorization, transport, decode, Link transition, batching, cancellation, terminal failure, and cleanup. Replace the current single `attempted` bit only for the paginated profile; preserve exact one-attempt behavior for both existing profiles. | Executor tests use Semantics-owned plans or plan fixtures and verify no relational fact is reconstructed from request strings, relation credentials, or Connector internals. Open remains I/O-free and no unsupported plan can reach transport. |
| `src/include/duckdb_api/http_runtime.hpp` and `src/http_runtime.cpp` | Update adjacent service documentation and installed profile construction for `0.5.0` ceilings while preserving checked process-global curl initialization, process-resident accepted lifetime, unsupported dynamic unload/reload, and the no-authority-override factory. | Initialization/lifecycle tests show new per-scan state is stream-owned and no authorization, Link, page, or counter state enters the process-global service. |

The split is responsibility-driven. Generic URI syntax changes with RFC 3986;
Link grammar and typed transitions change with protocol policy; resource
accounting changes with operational envelopes; transport changes with
dependency header delivery; decode changes with JSON source shape; and the
executor composes those services. They must not be folded into one larger
`http_scan_executor.cpp` state machine or a catch-all utility.

## Narrow normalized Link metadata

The private transport response carries a dedicated Link metadata value with
these rules:

1. The curl callback counts every received header byte against the page's
   ordinary header ceiling before inspecting or retaining it.
2. Only field-values whose field name equals `Link` case-insensitively are
   retained. Status lines, framing, other headers, and raw dependency handles
   are not returned.
3. Interim HTTP header sections do not contribute continuation metadata. A new
   status line resets the candidate field-values, so the returned value
   represents only the terminal response section associated with the returned
   status. Redirect following remains disabled.
4. Field-values retain receipt order and enough physical spelling for the Link
   parser to apply the combined grammar. Transport removes only HTTP field
   framing and permitted outer whitespace; it does not split links, select
   `rel`, parse a URL, or decide continuation.
5. Retained content and ownership overhead are bounded before allocation,
   reported as metadata bytes, charged to decoded-page memory by the executor,
   and released before another response replaces it. Header, metadata, or
   allocation failure returns a redacted resource error and no partial value.
6. Non-success responses may retain no body or Link metadata after status
   translation. Safe observations expose counts, not contents.

Controlled-transport and real-curl oracles must cover one/multiple physical
fields, mixed-case names, quoted comma/semicolon content, no Link, oversized
capture, malformed framing, an interim response followed by a final response,
and terminal cleanup. Synthetic Link strings are allowed only in explicit
Runtime fixture inputs and must never be echoed by assertion or error text.

## Link parser and sequential page state machine

The Link service accepts normalized field-values plus the immutable accepted
pagination policy and current typed page. It parses the combined field-value
grammar with delimiter awareness for angle-bracket targets and quoted
parameters, ignores at most 128 RFC list empty elements, honors only the first
`rel` parameter on a link-value, and delegates complete URI-reference syntax to
the focused RFC 3986 service. Malformed quoting, escaping, delimiters, URI
structure, relation spacing, or parameters fail closed. It requires zero or
one link whose relation set contains the exact accepted `next` relation and
rejects ambiguous multiple-next targets.

For the installed repository profile, the selected target must have exact
`https`, case-normalized `api.github.com`, absent or explicit port 443,
`/user/repos`, no user information or fragment, and exactly one raw
`per_page=100` plus one raw positive decimal `page=N`. Empty, duplicate, or
unknown query fields; encoded authority/path/field-name recovery; alternate
host spellings; invalid/overflowing numbers; non-incrementing pages; jumps;
and seen identities fail with `POLICY` and safe field `pagination.next`.
Percent decoding is never used to make a rejected component acceptable.

The state machine begins at the plan's typed first page, retains at most the
bounded seen-page identities, and produces only a typed next page. A separate
request builder combines the immutable operation with that typed value in the
canonical query order. Absence of accepted `next` marks true exhaustion.
Presence of `next` when the next page/request budget is unavailable fails as a
resource error; it does not convert the last permitted page into exhaustion.

Page state advances only after a successful response is status-checked,
decoded, and its continuation metadata is accepted. A page's rows are emitted
without deduplication. Runtime preserves response traversal but claims no SQL
ordering, stable snapshot, total, or remote/runtime limit.

## Aggregate budgets, backpressure, and lifecycle

- Before each attempt, the budget tracker reserves one page and one request.
  It derives transport limits as the minimum of the per-page ceilings and the
  remaining aggregate header, wire, decompressed, metadata-memory, and deadline
  allowances. Thus curl aborts at the aggregate boundary instead of returning
  an oversized page for post hoc rejection.
- After transport, status and reported counters are validated and committed
  once. The decoder receives the minimum per-page and remaining aggregate
  record/memory limits after captured metadata is charged. Failed accounting,
  status, decode, schema, or pagination is terminal and never replayed.
- One deadline starts on the first pull and is shared by every page, parser,
  decoder, batch transfer, cancellation wait, and close synchronization. A
  consumer pause cannot reset it, and a later page receives only remaining
  time.
- A decoded page is drained in batches of at most 64 rows. The body and Link
  metadata are released after decode; the page row buffer is released before
  the next request. If a page decodes to zero rows and has an accepted next,
  the same pull checks cancellation/deadline/budget again and advances without
  returning success.
- `Cancel` and `Close` can race with transport, parsing, decoding, and batch
  production. The existing stream mutex plus runtime-owned atomic cancellation
  remain the synchronization boundary; call-scoped `ExecutionControl` is never
  retained. The implementation must document lock ownership and must not hold
  a credential, response, or counter object in a process-global or shared scan
  service.

## Repeated authorization contract

`ScanAuthorization::GithubUserBearer` continues to take ownership of one
visible-ASCII token snapshot under the existing 8 KiB token ceiling. Query
moves it once into `OpenWithAuthorization`; pagination adds no second secret
lookup and no public refresh, clone, or page-decoration API.

Runtime keeps the capability opaque and stream-owned until terminal state. For
each accepted page it builds an uncredentialed canonical request, validates the
complete plan/request/operation/destination/placement intersection, then asks
the sole fixed authenticator to append one bearer header from the retained
snapshot. The transient request owns the per-attempt header copy; the scan
capability remains available only for a later validated page. Header creation,
transport failure, `401`, `403`, `429`, any other non-success, cancellation,
budget failure, malformed Link, close, and destruction all fail or finish
without replay and release their local copies. The retained snapshot is
released as soon as exhaustion or another terminal state is known.

Focused tests must prove page 1 through page N carry the same generated token,
an authority-escaping Link causes no later decorated request, concurrent scans
never cross token identity, closing one scan does not affect another, and no
token or full Authorization value survives in response metadata, counters,
diagnostics, observations, or post-close state. Runtime retains the RFC 0006
no-secure-zeroization and hostile-process limitation; it minimizes lifetime and
copies without making a stronger claim.

## Runtime-owned oracle placement

| Evidence surface | Required Runtime evidence |
| --- | --- |
| `test/cpp/execution_contract_tests.cpp` and authorization contract tests | Compile-time and behavioral proof of nonempty-success stream semantics, move-only authorization, no plaintext/public retargeting surface, invalid/moved-from behavior, and non-throwing teardown. Preserve existing anonymous and one-request cases. |
| New `test/cpp/link_pagination_tests.cpp` | Combined grammar, exact target acceptance, explicit/default port, query order handling with canonical reconstruction, termination, multiple next targets, every authority/query/encoding counterexample, numeric overflow/increment/cycle rules, safe errors, and bounded state. This is the primary parser/state-machine oracle. |
| New `test/cpp/scan_resource_accounting_tests.cpp` | Exact and +1 per-page/scan attempts, pages, headers, wire/decompressed bytes, metadata, decoded records/memory, deadline, concurrency, overflow, remaining-limit derivation, and advertised-next-at-ceiling failure. |
| `test/cpp/json_decoder_tests.cpp` | Root-array and five-column repository success plus empty, exact-page-size, +1, missing/duplicate/null/wrong-type, malformed, string/nesting/memory, deadline, and cancellation cases; retain array-under-field and root-object regressions. |
| New `test/cpp/http_scan_pagination_tests.cpp` with `test/cpp/support/controlled_http_transport.hpp` and `.cpp` | Scripted normalized responses and request observations for page targets, per-request limits, nonempty batching, empty intermediate pages, no-next exhaustion, late status/transport/decode/schema/pagination failures, aggregate exhaustion, cancellation between and during pages, early close, deadline persistence, capability release, redaction, recovery, and preserved one-request profiles. |
| New `test/cpp/curl_http_pagination_tests.cpp` with controlled socket support | Actual Link capture and exact repeated wire requests with one bearer header per page; final-header-block selection, multiple physical fields, header/metadata bounds, fixed authority, fresh connection/DNS checks, TLS and ambient-authority preservation, late failure, early close, and no redirect or credential forwarding. Existing curl request, budget, policy, TLS, and dependency tests remain authoritative regressions. |
| `test/cpp/curl_http_lifecycle_tests.cpp` | One deadline across pages, cancellation of an active later transfer, close between pages and during transfer, concurrent close/cancel/destruction, independent concurrent scans, process-runtime ownership, and executor recovery after every terminal class. |
| `test/cpp/support/loopback_curl_runtime.hpp` and `.cpp` | Private Runtime service returns executor and safe observations only. It no longer constructs, retains, or exposes `CompiledConnector`; controlled loopback authority remains absent from installed artifacts. |

The scripted controlled transport should move from one mutable response to a
bounded response sequence keyed by request order, with optional barriers before
or during a selected page. Its safe observations may contain canonical request
targets, header names/counts, numeric limits, and request counts. Exact bearer
values and synthetic Link field-values belong only to explicitly isolated
comparison inputs and must not be returned through ordinary observations or
failure text.

New focused targets and source-group registration are lead-owned CMake
integration. Runtime supplies the precise source/test list and negative
installed-artifact canaries; it does not edit root build or release files in
this workstream merely to make its tests discoverable.

## Provider dependencies and overlap

| Provider or consumer | Runtime consumes or provides | Dependency rule and readiness evidence |
| --- | --- | --- |
| Connector Experience | Indirectly consumes credential-free declarations only after Semantics maps them into the plan. | Runtime production must not include `connector_catalog.hpp`, construct `CompiledPagination`, import the native builder, infer pagination from relation/auth/query names, or copy catalog validation. Connector and Semantics fixtures must make the plan available without Runtime owning catalog state. |
| Relational Semantics | Consumes the complete immutable `ScanPlan`, including explicit `PaginationPlan`, source shape, schema, authorization obligation, network intersection, and page/scan budgets. | Runtime integration waits for public const accessors and Semantics-owned positive/counterexample fixtures. Runtime validates executable support but does not decide base-domain, mutable-bag, ordering, residual, limit, offset, or fallback meaning. |
| Query Experience | Provides `ScanExecutor`, `BatchStream`, typed batches, call-scoped control, structured errors, production executor factory, and executor-only controlled service. | Query must not import Runtime `internal/` headers or receive Link/page/budget/authenticator types. It moves one capability, pulls batches, translates errors once, and closes the stream. |
| Lead-agent integration | Receives new source/target lists, provider signature handoffs, artifact canaries, and focused command names. | Root CMake/Makefile/runners, controlled product composition, source/version identity, authoritative contracts, changelog, release notes, public activation, Git history, and final gates remain lead-owned overlap. |

The most consequential current overlap is
`test/cpp/support/loopback_curl_runtime.*`: `LoopbackCurlRuntime` constructs and
retains `BuildNativeGithubConnector()`, exposes `Connector()`, and is compiled
directly into Query's controlled composition. That makes a Runtime test service
also a Connector provider and forces Query to obtain two team APIs through one
object. Runtime must change only the loopback service to return its executor and
safe observations; Connector supplies the controlled catalog separately, and
Query/lead integration updates `BuildControlledProductComposition` and the
build graph. No adapter shim or duplicate catalog is permitted.

A second ordering dependency is the not-yet-landed Semantics pagination
service. Parser mechanics, normalized transport capture, root-array decoding,
and isolated authorization lifetime work can begin from RFC 0007, but executor
integration and authoritative request/budget assertions wait for Semantics to
publish the immutable `PaginationPlan`, scoped budget accessors, and exact
provider-owned fixtures. Runtime does not fill that gap by parsing Connector
query fields or constructing `ScanPlan` internals.

## Disjoint parallel work

| Track | Runtime-owned files | May proceed when | Must not overlap or wait condition |
| --- | --- | --- | --- |
| URI and Link grammar plus typed transition | New `uri_reference.*`, `uri_reference_tests.cpp`, `link_pagination.*`, and `link_pagination_tests.cpp` | RFC 0007 fixes the accepted grammar and authority; production plan adapter waits for the Semantics accessor shape | Does not edit executor, transport, Connector, planner, adapter, or root build files |
| Normalized transport metadata | `internal/http_transport.hpp`, `curl_transfer.cpp`, new curl pagination test and narrowly extended socket support | Narrow response shape and accounting rule are fixed | One owner for callback/state changes; executor consumes only after this boundary is tested |
| Repository decoder | `internal/json_decoder.hpp`, `json_decoder.cpp`, `json_decoder_tests.cpp` | Connector's five-column schema and root-array source are fixed by RFC/catalog plan | Does not infer pagination or touch executor/transport; coordinate with any concurrent decoder writer |
| Resource accounting | New `scan_resource_accounting.*` and its tests | RFC ceilings are fixed; constructor from Semantics plan waits for provider accessors | Does not duplicate counters in executor or alter relational planning |
| Authorization and executor state | authorization/fixed-authenticator files, `http_scan_executor.*`, controlled transport support, new executor pagination tests | Semantics plan/fixtures and the four primitive tracks expose stable interfaces | This is one serialized ownership track because authorization lifetime, stream mutex, state transitions, and request construction meet in `HttpBatchStream` |
| Controlled-boundary correction | `test/cpp/support/loopback_curl_runtime.*` and Runtime-facing support comments/tests | Executor-only factory contract is agreed with Query and lead | Query alone edits controlled product composition; lead alone edits source groups/build graph |
| Lifecycle and security deepening | Existing curl lifecycle/policy/TLS tests plus new pagination real-curl test | Integrated executor and scripted socket support are available | Does not weaken or rewrite existing RFC 0005/0006 oracles; new shared support has one writer |

Parallel writers use disjoint files or separate worktrees. Shared headers,
`http_scan_executor.cpp`, controlled transport support, controlled socket
support, and CMake are single-owner integration points. A temporary pagination
boolean, relation-name branch in Query, caller-selected URL, broad response
header map, plaintext credential accessor, or page-one fallback is not an
acceptable bridge between tracks.

## Sequencing and gates

1. **Governance gate — satisfied.** RFC 0007 is Accepted, product approval and
   all affected-team reviews are recorded, and the product goal is Active.
2. **Provider-interface gate — satisfied.** Connector and Semantics landed the
   closed pagination/resource provider shapes and focused fixtures. Runtime's
   immutable plan contains every execution fact and requires no Connector
   import or query-field inference.
3. **Primitive-service gate — satisfied.** Narrow final-response Link capture,
   typed Link transition, root-array decoding, aggregate accounting, and their
   DuckDB-free exact/+1/counterexample tests pass independently.
4. **Stream-contract and authorization gate — satisfied.** Nonempty-success
   comments and oracles, repeated scan-owned bearer decoration, complete
   profile validation, and I/O-free open are implemented. Both one-request
   profiles remain covered, and wrong plans or capabilities fail before I/O.
5. **Sequential-executor gate — satisfied.** The dedicated paginated service
   integrates page state, accounting, transport, decode, and batching. Its
   controlled oracle proves multi-page and empty-page traversal, no prefetch,
   canonical requests, terminal failures, cancellation, close, one deadline,
   redaction, capability release, and recovery.
6. **Real-curl and boundary gate — satisfied.** Private loopback tests prove
   terminal-block Link capture and repeated wire authorization while
   preserving TLS, DNS, ambient-authority, header, and decompression controls.
   `LoopbackCurlRuntime` exposes only Runtime's executor, and installed-artifact
   canaries exclude controlled authority and test hooks.
7. **Controlled-product gate — satisfied.** Lead and Query assemble Connector's
   catalog and Runtime's executor as separate providers through the production
   adapter path. Runtime tests remain executable without DuckDB; Query owns the
   SQL and lifecycle assertions and does not duplicate Link logic.
8. **Public-activation gate — satisfied.** Integration activated the exact
   repository plan, three-relation `0.5.0` catalog, and version identities only
   after the real Runtime consumer path passed focused evidence. The controlled
   SQL and product narratives now pass.
9. **Verification and exit gate — cached evidence satisfied; fresh evidence
   open.** Focused Runtime targets, integrated `make build`, `make test`,
   `make demo`, source identity, dependency-verifier unit tests, native
   formatting, and whitespace gates pass. The lead still owns a fresh native
   product cell, the final independent adversarial-review set, and final staged
   dependency and contract audits.

## Code documentation obligations

- Beside normalized response metadata, document producer and consumer,
  terminal-header-block selection, byte/memory accounting, lifetime, accepted
  normalization, absence of transport authority, and redaction rules.
- Beside URI/Link parsing and page state, document RFC 3986 structural
  validation, bounded empty-list and first-`rel` behavior, the accepted grammar subset,
  delimiter/quote behavior, target validation order, exact increment and cycle
  rules, canonical reconstruction, terminal transitions, error ownership, and
  why received targets are never sent.
- Beside the budget tracker, document host-versus-plan authority, per-page and
  aggregate scopes, debit/commit order, overflow behavior, remaining-limit
  derivation, instantaneous memory/concurrency ownership, and why an advertised
  next page at exhaustion fails rather than truncates.
- Beside authorization, document scan ownership, the only allowed consumer,
  repeated decoration without a public plaintext accessor, per-request header
  copies, exact operation/host/placement scope, concurrent isolation, release
  transitions, and the existing no-secure-zeroization limitation.
- Beside `HttpBatchStream`, document state transitions and invariants for
  request, response, decode, drain, empty-page continuation, exhaustion,
  failure, cancel, and close; lock/atomic ownership; call-scoped control; one
  deadline; and why the next request waits for current-page drain.
- Beside root-array decoding, document accepted source shape, full-document
  validation, strict conversion, empty array behavior, record/memory authority,
  cancellation, and that decoder output has no relational or pagination
  meaning.
- Beside production and controlled factories, document fixed authority,
  profile compatibility, process-runtime lifetime, executor-only controlled
  service, and the negative installed-artifact boundary.

## Explicit non-work

This Runtime workstream does not:

- construct or validate `CompiledPagination`, parse connector YAML, load
  packages, expose author syntax, or own native catalog identity;
- build `ScanRequest` or `ScanPlan`, decide base-domain bag meaning,
  consistency, filtering, ordering, limit, offset, residual ownership, or
  conservative fallback;
- register the SQL relation, resolve DuckDB secrets, import `ClientContext`,
  mutate `DataChunk`, translate DuckDB errors, own prepared statements, or
  produce the public/live demonstration;
- follow response URLs, accept caller destinations/headers/page sizes/filters,
  enable redirects/proxy/netrc/cookies/environment credentials, or expose a
  general response-header API;
- add retry, replay, rate-limit sleep, parallel pages, prefetch, resume, cache,
  providers, GraphQL, deduplication, total/progress claims, snapshot isolation,
  or a public native ABI;
- change the temporary-secret provider, credential selection, persistent or
  environment secret policy, supported DuckDB/libcurl cell, process-global
  curl cleanup policy, or dynamic unload/reload support; or
- edit root build/release scripts, source/version identities, authoritative
  contracts, public changelog/release notes, Query controlled composition,
  another team's plan, Git history, live-secret custody, or goal completion.

Evidence that requires any excluded behavior returns to the lead agent for
scope and RFC assessment rather than being hidden in Runtime internals.

## Observable interaction exits

- **Relational Semantics — Exited to X-as-a-Service.** Runtime production
  includes only the public immutable plan contract,
  consumes explicit pagination/source/auth/network/page-and-scan budget facts,
  and independently executes provider-owned positive and counterexample plans
  without constructing plan internals, importing planner/Connector code,
  parsing structural query fields to discover pagination, reclassifying the
  duplicate-preserving bag, or applying ordering/limit/offset.
- **Connector Experience — Exited to X-as-a-Service.** Runtime has no
  production include, construction, retained object, or
  link-time dependency on Connector declarations; its fixed executable profile
  agrees with the accepted plan; Runtime fixtures no longer obtain a catalog
  through `LoopbackCurlRuntime`; and existing plus repository profiles pass
  without copying native metadata into the parser.
- **Query Experience — Exited to X-as-a-Service.** Query consumes only public
  authorization/executor/stream/batch/control/error
  APIs, `true` can never reach DuckDB with an empty batch, executor open remains
  I/O-free, early close/cancellation/late failure/capability release/redaction
  agree across independent Runtime and Query tests, and controlled composition
  obtains Runtime's executor separately from Connector's catalog with no
  Runtime-internal include or pagination workaround.
- **Remote Runtime workstream — Implemented; cached exit evidence satisfied.** DuckDB-free parser,
  authority-denial, resource, repeated-authorization, request-sequence,
  decoder, cancellation, close, deadline, redaction, failure, concurrency,
  recovery, real-curl, and preserved-profile oracles pass; adjacent API/state-
  machine documentation covers the charter obligations; installed artifacts
  contain no controlled authority or test hook; final source/include/factory/
  build/test dependencies match this plan; required adversarial findings are
  resolved or rejected with evidence; authoritative Runtime contracts are
  propagated by integration; and all focused and cached product gates pass.
  Final goal closure additionally requires the lead-owned fresh product cell
  and independent adversarial-review set.

An interaction remains **Open** if consumers need routine knowledge of Link
grammar, page state, transport, credential decoration, or counters; Runtime
constructs or retains Connector/Semantics internals; a test passes only through
a combined provider object; page-one fallback or silent truncation exists; or
the final source and test graph contradicts the documented provider boundary.

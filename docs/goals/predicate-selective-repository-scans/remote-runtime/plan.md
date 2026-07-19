# Remote Runtime plan: predicate-selective repository scans

## Outcome, status, and charter disposition

Status: **Planned; Runtime RFC re-review approved; implementation evidence and
provider-interface handoff pending**.

Remote Runtime supports Query Experience's active product outcome by executing
the repository operation that Relational Semantics has already selected and
bound. For the approved SQL case, an accepted selective plan will carry the
RFC-defined typed repository-visibility request input; an unselective or
fallback plan will carry the existing complete-traversal operation. Runtime
will apply the selected input to every reconstructed page request and return a
bounded `BatchStream`. The sole selective input is the closed typed value that
encodes `visibility=private`; Runtime admits it exactly once with the fixed
repository operation and pagination target into one immutable executable
request profile. It will not inspect a DuckDB predicate, derive the input from
a column value, classify exactness, choose residual ownership, filter decoded
rows, or decide when fallback is semantically required.

Remote Runtime's revised RFC disposition is **Approved**. The RFC accepts the
earlier objection to competing request authorities: one complete typed
`ScanPlan` input is admitted before authorization or I/O into one immutable
request profile used by first-page construction, every continuation, and Link
validation. Raw operation fields, encoded query strings, and received targets
cannot independently select or modify the field. This review is team input,
not the RFC decision; production work may begin only after all of the following
are available:

- an Accepted product RFC that fixes the shared typed-plan and fixed GitHub
  request contracts;
- Connector Experience's immutable predicate-to-input declaration and its
  validation evidence;
- Relational Semantics' complete immutable `ScanPlan`, including the selected
  typed input, relational classification, residual owner, network capability,
  pagination plan, and resource budgets; and
- Semantics-owned positive and counterexample plan fixtures that Runtime can
  consume through the existing fixture-service dependency.

If the accepted RFC changes those premises, this plan must be revised before
implementation. Runtime does not use this plan to establish the RFC decision.
No Runtime correctness blocker to RFC acceptance remains.

## Runtime interpretation of the accepted plan

The execution boundary remains:

```text
complete immutable ScanPlan + moved authorization capability
    -> admitted executable repository profile
    -> bounded, cancelable BatchStream
```

The typed visibility input is execution data, not a relational expression.
Runtime validates the closed `visibility=private` form against the installed
repository profile and encodes that form into the fixed request. It must not
recover a type from an encoded query string, interpret the output visibility
value as a predicate, compare DuckDB and GitHub truth domains, or alter the
plan's remote predicate, accuracy, residual, ordering, limit, or offset facts.

For both selective and fallback plans:

- executor open validates the complete plan/capability intersection without
  DNS, socket, secret lookup, credential materialization, or HTTP I/O;
- the first pull starts execution and the single retained scan deadline;
- Runtime emits every row returned by the admitted remote operation, with
  strict six-column schema conversion and duplicate-preserving page traversal;
  the repository schema is exactly required `id`, `full_name`, `private`,
  `fork`, and `archived`, followed by required `VARCHAR visibility` extracted
  from `$.visibility`;
- DuckDB applies any DuckDB-owned residual and ordering after the stream; and
- a terminal late-page failure remains a failed relation rather than clean
  exhaustion, even after earlier batches were consumed.

## Typed request-input execution

### Admission and representation

Runtime will consume the accepted explicit typed input accessor from
`ScanPlan`; it will not infer selective execution from relation identity,
`remote_predicate`, `classification_reason`, output column values, or raw
`PlannedQueryParameter` spelling. Plan admission will distinguish only the
closed executable alternatives supplied by Semantics:

1. the existing unselective repository traversal; and
2. the accepted selective traversal carrying the one closed typed input
   `visibility=private`.

Missing required payload, unknown variants, wrong scalar kinds, duplicate or
conflicting bindings, any field or value other than `visibility=private`, or a
mismatch among the operation, exact six-column schema, request field set, and
pagination target fail as a bounded `POLICY` error before the authorization
capability is opened or transport can observe a request. Successful admission
produces one immutable `AdmittedRepositoryRequestProfile` containing the
canonical operation origin/path, the selected non-pagination field set, the
page-size/page-number declarations, and the exact required response schema.
Runtime validates executability, not relational correctness.

### Request construction

Create a focused Runtime-private request-input application service if the
accepted contract requires more than a local value object. Its only job is to
convert an already validated typed binding into canonical, percent-safe fixed
request fields. The service receives no DuckDB object, predicate tree,
Connector declaration, secret, URL authority, or response metadata.

The request-input application and operation/pagination checks run once during
admission. No later component re-reads raw `ScanPlan` operation or input fields
to make another selection. The repository page builder reconstructs each
request from:

- the single immutable admitted repository request profile; and
- the current typed page number from Runtime's sequential state.

For the selective profile, the canonical request fields are exactly
`per_page=100`, `page=N`, and `visibility=private`; the unselective profile has
only the two page fields. The selected input is therefore identical on page 1
and every later page. The only mutable request fact is the next typed page
number. A received Link target is never transmitted, and no user or remote
string gains authority to select scheme, host, port, path, header,
authenticator, or additional query fields.

### Pagination preservation

The Link state machine consumes the same admitted request profile and validates
a prospective `next` target against it, not only against independently copied
page fields. It requires the
exact fixed origin and path, the accepted immutable non-pagination field set,
one fixed page-size field, and exactly the next positive page identity. It will
reject omitted, changed, duplicate, or extra fields, alternate authorities or
paths, fragments, unsafe encodings, replayed pages, jumps, and multiple next
targets with the existing redacted pagination policy failure.

The parser returns only `{has_next, next_page}`. The page request is then
reconstructed canonically from the plan and that typed page number. Query-field
order in received metadata is a parser concern and cannot grant additional
authority. The existing two-field unselective profile remains a regression
case.

Runtime does not inspect a decoded repository's `visibility` value to filter or
stop and does not predict how many pages a selective scan should have. A smaller
trace is observed only when the controlled selective upstream response omits
continuation earlier. Missing `next` means ordinary exhaustion; an advertised
`next` with no remaining page/request/time authority remains a resource error.

## Security, resources, and lifecycle ownership

### Authorization and network policy

- Preserve the move-only GitHub-user bearer capability and one capability
  identity per scan. The typed input is not credential or destination
  authority.
- Validate the complete plan/capability intersection during admission, then
  validate the immutable admitted profile and final uncredentialed request
  before materializing the bearer header on every page. Selective and fallback
  forms must have the same exact authenticator, header placement, origin, path,
  TLS, DNS/address, redirect, proxy, netrc, cookie, and ambient-authority
  policy.
- Extend only the fixed installed request allowlist required by the accepted
  profile. Do not introduce caller URLs, arbitrary query maps, generic header
  placement, redirects, or a broader host capability.
- Keep request targets, received Link values, response bodies, token bytes,
  and Authorization values out of public diagnostics and safe observations.
  Rejected typed inputs use bounded field names and messages without rendering
  caller or remote values.

### Resource and replay policy

- Retain one active request, sequential pages, pull-driven backpressure, one
  decoded page, at most 64 output rows per batch, and the existing page/scan
  ceilings unless the Accepted RFC explicitly narrows them.
- Debit actual requests, pages, header bytes, wire/decompressed bytes, records,
  retained decoded memory, and elapsed time through the existing
  `ScanResourceAccounting` state machine. A shorter selective trace consumes
  less of the ceiling; it does not change the ceiling's meaning.
- Keep one attempt per page and retry disabled. Pagination is not replay
  authority, and no request is retried before or after row commitment.
- Preserve release-before-next-page ordering and do not add prefetch, parallel
  pagination, remote limit, cache, provider, or rate-limit behavior.

### Stream lifecycle

- `BatchStream::Next == true` continues to mean a nonempty schema-aligned batch;
  `false` alone means clean source exhaustion.
- Cancellation, close, destruction, malformed plan or Link metadata, policy or
  resource failure, HTTP status, transport failure, and decode/schema failure
  release authorization, request, response, Link metadata, and decoded-page
  state through the existing terminal path.
- `Cancel` and `Close` remain non-throwing and idempotent. Call-scoped
  `ExecutionControl` is never retained. The same deadline and cancellation
  checkpoints cover input application, request construction, transport,
  decode, Link validation, batch transfer, and later pulls.
- Failure after an emitted page remains terminal and stable on repeated pulls;
  it cannot become fallback, retry, or complete-looking partial output.

## Source ownership

| Source surface | Remote Runtime responsibility | Boundary constraint |
| --- | --- | --- |
| `src/include/duckdb_api/scan_plan.hpp` and Semantics implementation/fixtures | **Consume only.** Review the accepted typed request-input accessor for executable completeness and lifetime, then compile against it. | Relational Semantics owns construction, immutability, classification, input binding, exactness, residual ownership, and explanation. Runtime does not patch around an absent provider contract. |
| New `src/include/duckdb_api/internal/runtime/execution/request_input_application.hpp` and `src/runtime/execution/request_input_application.cpp`, if warranted by the accepted shape | Own DuckDB-free validation of the closed typed `visibility=private` binding and canonical non-pagination field construction during admission. State accepted type/value domain, encoding, allocation/error ownership, and absence of relational meaning. | No Connector metadata, predicate representation, secret, network call, received URL, or generic caller-selected query map enters this service. If the accepted shape is trivial, keep the same responsibility as a focused private function rather than creating an empty abstraction. |
| `src/runtime/execution/http_plan_admission.cpp` | Admit both exact installed repository execution forms and construct the sole immutable `AdmittedRepositoryRequestProfile`; validate operation, six-column schema, selected input, fixed fields, pagination target, authorization, network, and resource facts before side effects while preserving the anonymous and authenticated-user profiles. | Admission may reject unsupported executable facts but may not reclassify, repair, or leave raw plan fields as a second request authority. |
| `src/runtime/execution/http_paginated_scan.cpp` | Consume only the admitted profile plus typed page state to build every page request; preserve decode, batching, accounting, cancellation, terminal failure, and release ordering. | Do not re-read raw operation/input fields, filter decoded `visibility` values, or inspect plan explanation/predicate facts to select a request. |
| `src/include/duckdb_api/internal/runtime/pagination/link_pagination.hpp` and `src/runtime/pagination/link_pagination.cpp` | Validate each target against the admitted profile's immutable origin, path, and non-pagination field set together with exact sequential page transition; retain only typed page identity and preserve the unselective profile. | No independently configured fixed-field set or received target becomes request authority; the state machine remains DuckDB-, secret-, and transport-free. |
| `src/runtime/authentication/fixed_github_user_bearer_authenticator.cpp` | Extend final-request validation to the accepted canonical selective target while preserving exact plan/operation/destination/placement checks and header budgeting before each decoration. | The accepted input cannot widen credential host, path, placement, header count, or lifetime. |
| `src/runtime/transport/curl_http_transport.cpp` | Admit and compose only the accepted fixed selective repository target in addition to the existing canonical targets; preserve fixed HTTPS authority and all curl policy. | No general URL or arbitrary request surface is introduced. |
| `test/cpp/runtime/support/controlled_http_transport.*`, `loopback_curl_runtime.*`, and controlled socket support as needed | Provide bounded scripted selective/unselective response sequences and safe request observations for Runtime oracles. | Test authority remains absent from installed artifacts; ordinary observations redact bearer values and do not expose received Link contents. |
| `src/runtime/sources.cmake`, `src/runtime/targets.cmake`, `test/cpp/runtime/sources.cmake`, and `test/cpp/runtime/targets.cmake` | Register any focused Runtime source and target while preserving narrow service dependencies. | Runtime targets link the public scan-plan service and Semantics fixture service; they do not list Semantics production sources or import provider-private fixture construction. Root product/release integration remains lead-owned. |
| `src/runtime/README.md` and adjacent declarations | Keep the package map, supported fixed request profile, test routing, and lifecycle/security cautions current. | Shared architectural and public product narrative is integrated with the lead after RFC acceptance. |

No Runtime work is planned in Query's adapter, Connector's native catalog,
Semantics' classifier/planner, public SQL examples, or another team's plan.

## Runtime-owned oracle plan

| Oracle | Evidence owned by Remote Runtime |
| --- | --- |
| New focused request-input application test, or an equivalently focused section of `duckdb_api_http_scan_executor_policy_tests` | The accepted typed binding produces exactly `visibility=private`; unselective input produces no non-pagination field; wrong type/name/value, duplicate/conflicting binding, allocation failure, and unknown variant fail with bounded redacted errors and no authorization or I/O. The oracle uses no DuckDB predicate or Connector internals. |
| `duckdb_api_http_scan_executor_policy_tests` | Complete selective and fallback plans admit into one immutable profile; incomplete, mismatched, widened, relationally altered, wrong-schema, wrong-operation, or wrong-pagination-target counterexamples fail before authorization and controlled transport. Authorization alternatives, network authority, features, budgets, and all three existing relation profiles remain covered. |
| `duckdb_api_link_pagination_tests` | Selective targets require exactly `visibility=private` plus the page fields; unselective targets require only page fields. Every legal query-field order produces only the next typed page; omission/change/duplication/extra fields, alternate origin/path, unsafe encodings, multiple-next, replay/jump/overflow, malformed syntax, and post-terminal advance fail safely. |
| `duckdb_api_http_scan_pagination_tests` | Page 1 through N consume the same admitted profile and carry `visibility=private` plus the same bearer capability; the selective controlled source ends in fewer requests; no prefetch occurs; empty nonterminal pages, 64/36 page draining, aggregate exhaustion, late failures, cancellation, early close, and repeated terminal pulls preserve current behavior. The test asserts request trace only, not relational equivalence. |
| `duckdb_api_scan_resource_accounting_tests` | Existing exact/+1 page and aggregate boundaries remain unchanged. A shorter trace reports/debits actual usage; advertised continuation at the ceiling still fails. |
| `duckdb_api_curl_http_pagination_tests`, `duckdb_api_curl_http_transport_tests`, `duckdb_api_curl_link_metadata_tests`, and lifecycle/security regressions | Real curl emits only the canonical accepted selective or unselective target, one bearer header per page, and the existing TLS/DNS/redirect/ambient-authority/header/response limits. Final-block Link handling, active cancellation/close, process lifetime, and installed-artifact isolation remain intact. |
| Existing executor, authorization, decoder, and product-profile regressions | Anonymous search, authenticated user, and unfiltered authenticated repositories remain compatible; repository decoding requires the five existing columns followed by `VARCHAR visibility` from `$.visibility`, with missing, `NULL`, non-string, and over-budget values failing strictly; move-only capability behavior, I/O-free open, and executor recovery remain compatible. |

Query Experience owns the DuckDB black-box equivalence oracle between the
optimized result and full traversal plus local filtering, structured-filter
fallback, explain output, prepare/offline behavior, SQL ordering, and public
demonstration. Relational Semantics owns implication, exact/superset,
three-valued logic, residual, and immutable-plan snapshot oracles. Runtime's
smaller controlled trace is necessary evidence but is not evidence of SQL
equivalence by itself.

## Dependencies and handoffs

| Partner | Runtime consumes or provides | Readiness and exit evidence |
| --- | --- | --- |
| Connector Experience | Indirectly consumes the accepted immutable request-input capability only after Semantics compiles it. | Runtime includes no connector catalog/YAML headers and never infers mapping meaning from GitHub request fields. Connector's declaration has independent validation and explanation tests. |
| Relational Semantics | Consumes one complete immutable `ScanPlan` and provider-owned positive/counterexample fixtures. Provides executable-support feedback during RFC review, then a low-coordination executor consumer. | The plan carries all typed request, pagination, authorization, network, resource, and relational ownership facts. Runtime tests link `duckdb_api_semantics_fixture_service`, not private Semantics sources, and Runtime never rewrites those facts. |
| Query Experience | Provides unchanged `ScanExecutor`, `BatchStream`, typed batches, structured errors, cancellation, close, and runtime factory services. | Query moves authorization once, opens/pulls/closes through public APIs, and contains no query-parameter encoding, Link validation, request allowlist, auth placement, transport, or resource-accounting logic. |
| Lead/RFC integration | Provides Runtime's feasibility evidence and exact affected source/test inventory; consumes accepted shared contracts and final integration decisions. | The lead owns RFC acceptance, cross-contract propagation, root build/product composition, version/release records, full dependency audit, Git history, and goal closure. Runtime does not infer those decisions. |

External GitHub documentation and counterexamples that prove the remote
visibility domain belong to the RFC and Connector/Semantics evidence. Runtime
does not convert observed service behavior into a relational guarantee.

## Parallelizable work and serialization points

After RFC acceptance and publication of the Semantics plan/fixture interface,
Runtime work can proceed in parallel with Query's structured-filter adapter
work because the two teams consume opposite ends of the immutable plan.

Within Runtime, these disjoint tracks may proceed in parallel:

- typed input application plus its pure DuckDB-free oracle;
- plan-driven Link target validation plus pure pagination counterexamples;
- controlled transport/socket fixture support for selective and fallback
  request sequences; and
- real-curl fixed-target and security oracle preparation.

The following integration is serialized under one Runtime owner because the
same request and capability cross all of them:

- `http_plan_admission.cpp`;
- `http_paginated_scan.cpp`;
- `fixed_github_user_bearer_authenticator.cpp`;
- `curl_http_transport.cpp`; and
- package source/target registration.

Shared headers and test support have one writer at a time. No temporary
relation-name branch in Query, raw encoded-value parser, caller-selected URL,
duplicated Link logic, or locally constructed `ScanPlan` is an acceptable
bridge while a provider interface is pending.

## Interaction exit audit

The Remote Runtime collaboration becomes X-as-a-Service only when source and
test dependencies, not merely passing end-to-end SQL, show all of the
following:

1. Runtime admission consumes only public immutable `ScanPlan` accessors and
   `ScanAuthorization`; it imports no DuckDB filter/adapter type, Connector
   catalog builder, or Semantics-private constructor.
2. Admission produces one immutable request profile, and first-page building,
   continuation building, Link validation, bearer decoration, and transport
   validation all consume that profile without re-reading or re-deriving input
   from a predicate, output column, relation explanation, raw plan operation,
   encoded query parameter, or received target.
3. DuckDB-free request, pagination, security, budget, cancellation, close,
   failure, and existing-profile oracles pass against the public plan fixture
   service.
4. Query consumes `ScanExecutor`/`BatchStream` without Runtime `internal/`
   headers and contains no request-input, pagination, authorization-placement,
   network, or transport-policy implementation.
5. Runtime's focused target links against bounded provider libraries rather
   than directly listing Semantics production sources; whole-product
   composition remains in explicitly named integration targets.
6. The accepted request forms are documented beside Runtime APIs and state
   machines, and all temporary collaboration hooks or trial-only coupling are
   removed.

If one of these conditions fails, the interaction remains Collaboration and
the dependency or interface must be corrected before handoff.

## Documentation obligations after RFC acceptance

- Update the native mapping and execution/pagination sections of
  `docs/RUNTIME_CONTRACTS.md` to describe the accepted typed request input,
  admission order, canonical reconstruction, Link invariant, actual resource
  accounting, error/redaction behavior, and unchanged stream lifecycle.
- Supply Runtime-owned wording for `docs/ARCHITECTURE.md` and
  `docs/CONNECTOR_SPECIFICATIONS.md` propagation without taking ownership of
  relational proof or connector-author semantics. The lead integrates the
  coherent shared contract.
- Update `src/runtime/README.md` and source/test inventories for any new focused
  module or target.
- Beside the typed input and admission services, document the exact
  `visibility=private` input, six-column schema, immutable admitted-profile
  output, canonical encoding, validation-before-side-effects, allocation and
  error ownership, lifetime, and the explicit absence of relational meaning.
- Beside Link state, document the immutable non-pagination field invariant,
  structural query comparison, query-order handling, typed page-only output,
  terminal transitions, and why the received URL is never sent.
- Beside plan admission, bearer decoration, and curl target validation,
  document the three-stage least-authority checks and which layer owns each
  redacted failure.
- Beside `PaginatedBatchStream`, keep ownership, lock/atomic behavior,
  backpressure, one deadline, cancellation/close idempotence, release ordering,
  and late-failure semantics current.
- Update diagnostics, examples, release notes, and roadmap evidence only
  through the lead-owned product integration; Runtime does not claim explain
  accuracy or SQL equivalence in its package documentation.

## Verification and handoff

Run narrow evidence first:

- the new request-input target, if created;
- `duckdb_api_http_scan_executor_policy_tests`;
- `duckdb_api_link_pagination_tests`;
- `duckdb_api_http_scan_pagination_tests`;
- `duckdb_api_scan_resource_accounting_tests`;
- `duckdb_api_authorization_contract_tests`;
- `duckdb_api_json_root_array_decoder_tests`;
- `duckdb_api_curl_http_transport_tests`;
- `duckdb_api_curl_http_pagination_tests`;
- `duckdb_api_curl_link_metadata_tests`;
- `duckdb_api_curl_http_budget_tests`;
- `duckdb_api_curl_http_lifecycle_tests`; and
- `duckdb_api_curl_tls_security_tests` and
  `duckdb_api_curl_transfer_policy_tests`.

Then run the supported product-cell gates required by `AGENTS.md`:

```sh
make build
make test
make demo
scripts/verify-source-identities.py
python3 -I -B scripts/test-native-dependencies.py
make verify
ruby scripts/validate-agent-assets.rb
git diff --check
git diff --cached --check
```

The lead additionally runs the active goal's authoritative community/product
gates and audits the final source-target dependency graph. Because this change
touches request construction, credentials, network policy, pagination,
resources, cancellation, and close, independent adversarial review is required
before completion. Review findings are accepted only with concrete source or
oracle evidence; every accepted finding is fixed and reverified, and every
rejection is recorded with evidence.

Runtime handoff is complete only when the focused oracles and full relevant
gates pass, no unsupported plan can reach transport, the selective controlled
trace is smaller while the unselective trace is unchanged, every interaction
exit condition is evidenced by actual includes and link targets, shared
contracts agree with the Accepted RFC, and no unrelated change is present.

## Explicit non-work

This workstream does not:

- establish or accept the RFC, revise the selected public/shared input type or
  spelling, or prove GitHub-to-DuckDB predicate equivalence;
- build `ScanRequest`/`ScanPlan`, classify predicates, choose exact versus
  superset accuracy, assign residual ownership, or implement fallback;
- parse SQL text or DuckDB filter objects, filter or stop on decoded
  `visibility`, or own explain output, local ordering, limit, or offset;
- add connector-package loading, YAML syntax, other predicate mappings,
  projection, providers, retries, rate-limit waits, caching, GraphQL, parallel
  pages, prefetch, resume, totals, or snapshot claims;
- widen network, credential, secret-storage, redirect, proxy, TLS, resource,
  concurrency, process-lifetime, or dynamic-unload policy; or
- edit another team's plan, root product/release integration, Git history, or
  goal status.

Evidence that requires excluded behavior returns to the lead for scope and RFC
assessment instead of being hidden in Runtime request code.

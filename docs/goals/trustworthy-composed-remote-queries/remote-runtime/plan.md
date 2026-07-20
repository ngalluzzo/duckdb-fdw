# Remote Runtime plan: trustworthy composed remote queries

## Outcome and charter disposition

Status: **Delivered; plan-interface interaction exited to X-as-a-Service**.

Remote Runtime supports Query Experience's active outcome by admitting and
executing the complete immutable `ScanPlan` produced by Relational Semantics.
Runtime will recognize every closed RFC 0010 classification while executing
only typed operation/input profiles it actually supports. In particular, an
exact label neither grants execution support nor changes an otherwise supported
request. Runtime does not prove or reclassify relational meaning.
Unsupported and ambiguous semantic decisions execute the unrestricted base
operation when Semantics supplies that complete fallback plan. Invalid
planning does not produce an executable plan or enter Runtime.

The Runtime boundary remains:

```text
complete immutable ScanPlan + moved authorization capability
    -> fail-closed executable admission
    -> one immutable admitted request profile
    -> bounded, cancelable BatchStream
```

Runtime's authority begins at executable support. Relational Semantics owns
exact/superset/unsupported/ambiguous classification, composition, fallback,
residual ownership, and deterministic reasons. Runtime does not inspect a
DuckDB expression, Connector proof, classification reason, plan snapshot, or
decoded value to decide which request to send.

## Admission and execution scope

### Semantic classifications are not request authority

Admission must recognize the accepted closed plan representation without
turning it into a second semantic decision function:

| Semantics outcome | Runtime behavior |
| --- | --- |
| Exact selected restriction | Reject it in the installed Runtime profile. The only validated Exact mapping belongs to a controlled non-installed operation, while labeling the installed GitHub Superset source Exact is an incoherent contract. Neither case may create request or credential authority. |
| Superset selected restriction | Validate the typed GitHub operation, conditional input, ownership/delegation envelope, schema, pagination, authorization, network, feature, and resource facts, then use only the admitted typed input to construct requests. |
| Unsupported fallback | Admit the complete base operation when its typed conditional input is absent and the remaining executable envelope is supported. Runtime does not recover a restriction from predicates or reasons. |
| Ambiguous fallback | Execute the same unrestricted typed form as unsupported fallback. Ambiguity is Semantics' explanation, not a Runtime branch or error. |
| Invalid decision | Semantics returns planning failure, so Query performs no Runtime open. If an unknown enum, incomplete plan, unsupported delegation, or malformed executable value is nevertheless presented to Runtime, admission fails deterministically before authorization materialization, DNS, socket creation, or transport observation. |

Runtime may reject an operation, input variant, schema, ownership delegation,
authorization placement, network capability, feature, pagination profile, or
budget it cannot execute. It must not repair the plan, downgrade an invalid
state to fallback, change exact to superset, correlate a reason string with an
input, or reproduce the implication/composition matrix in
`http_plan_admission.cpp`.

The current `HasExpectedRepositoryRelationalProfile`-style coupling is
therefore revised around executable capability rather than combinations of
remote predicate, accuracy, and residual labels. The accepted structured
classification remains available for explanation and exhaustive unknown-value
denial, but it is never parsed or used to synthesize request authority.

### Typed conditional-input authority

The sole predicate-derived execution authority is the accepted typed
conditional input on `ScanPlan`, scoped to its selected operation. Runtime
admits exactly these installed repository request forms:

1. no conditional input, producing the canonical unrestricted repository
   request; and
2. the closed `VISIBILITY_PRIVATE` input, producing the same canonical request
   with exactly one `visibility=private` non-pagination field.

The installed superset plan exercises the second executable form. A public
exact-plan fixture exercises it only if its typed operation is independently
supported by the active Runtime test profile; the classification alone can
never make a controlled operation executable. Otherwise it is denied as an
unsupported operation with zero I/O. Unsupported and ambiguous fallback
fixtures exercise the first form. An absent required payload, unknown typed
variant, duplicate/conflicting binding, input/operation mismatch,
caller-supplied encoded field, or attempt to delegate filtering, ordering,
limit, or offset to Runtime fails admission.

Successful admission produces one immutable
`AdmittedRepositoryRequestProfile`. First-page construction, continuation
reconstruction, Link validation, bearer decoration, strict decoding, and curl
target validation consume that profile or its exact derived request. They do
not re-read `ScanPlan` semantic fields. A received Link remains untrusted
metadata and contributes only the validated next typed page number.

### Zero-I/O invalid states

Two distinct zero-side-effect oracles are required:

- Semantics planning failures and DuckDB-pruned scans perform zero Runtime
  opens; Query owns those counters and product assertions.
- Runtime receives public invalid-plan fixtures for executable-boundary
  failures. `Open` rejects them with a bounded redacted policy/resource error,
  zero controlled-transport observations, zero socket connections, and no
  consumed or decorated bearer capability.

Unknown closed-enum values, missing structured fields, unsupported Runtime
ownership, widened network/resource authority, or inconsistent typed request
inputs cannot fall through to an older executable default. Failed admission
does not mutate a retained plan, admitted profile, executor, or later stream.

## Operational invariants preserved

- Executor open remains deterministic and performs no network I/O. The first
  pull begins the single retained scan deadline and the first possible remote
  request.
- Plans, authorization alternatives, admitted profiles, and pagination facts
  remain immutable or move-only as applicable. Each execution owns isolated
  state; an exact, selective, fallback, failed, or closed execution cannot leak
  authority into another stream.
- Pagination remains sequential, pull-driven, and one request at a time. No
  prefetch, parallel page work, remote bound, retry, cache, or provider is
  introduced.
- Existing page and scan ceilings, actual-use debits, one-attempt replay
  profile, at-most-64-row batches, strict six-column conversion, and
  release-before-next-page ordering remain unchanged.
- `BatchStream::Next == true` still means a nonempty schema-aligned batch;
  `false` alone means clean source exhaustion. A failure after emitted rows is
  terminal and stable on repeated pulls, never fallback or partial success.
- Cancellation checkpoints continue across request construction, transport,
  Link validation, decode, batch transfer, and later pulls. `Cancel`, `Close`,
  and destructors remain non-throwing; cancellation and close are idempotent;
  call-scoped `ExecutionControl` is never retained.
- Cancellation, early close, terminal error, or destruction releases request,
  response, decoded-page, Link, authorization, and admitted-profile state
  through the existing terminal path. No classification adds cleanup or
  resource authority.
- Credential placement, fixed HTTPS origin/path, TLS/DNS/redirect/proxy/netrc/
  cookie policy, header budgeting, and redaction are unchanged. Typed semantic
  facts and rejected values never enter public diagnostics.

## Source and test ownership

| Surface | Remote Runtime responsibility | Boundary constraint |
| --- | --- | --- |
| `src/include/duckdb_api/scan_plan.hpp`, Semantics planner/classifier, and Semantics fixture implementation | **Consume only.** Review the accepted accessors and fixture service for executable completeness. | Relational Semantics owns plan construction, structured classification, immutability, explanation, and valid/invalid fixture production. Runtime does not add friend access, setters, or private plan construction. |
| `src/include/duckdb_api/internal/runtime/execution/http_plan_admission.hpp` and `src/runtime/execution/http_plan_admission.cpp` | Own exhaustive fail-closed validation of Runtime-supported executable facts and construction of the sole immutable admitted profile. Remove semantic reclassification from request selection. | Classification, accuracy, remote/residual predicates, and reason text are not request authority. Unknown or unsupported executable states fail before side effects. |
| `src/runtime/execution/http_scan_executor.cpp` and `src/runtime/execution/http_paginated_scan.cpp` | Preserve open-before-I/O, typed dispatch, stream isolation, batching, accounting, cancellation, terminal failure, and close while consuming only the admitted profile during repository execution. | No Connector, Query, DuckDB adapter, or Semantics-private type enters Runtime. |
| Runtime authentication, pagination, policy, decoding, and transport sources | Regression-only unless the accepted plan representation requires a focused exhaustive enum check at admission. | Do not widen request forms, credentials, network policy, pagination, conversion, retries, resources, or lifecycle behavior. Later layers must not reclassify the plan. |
| `test/cpp/semantics/support/scan_plan_test_fixtures.hpp` and `duckdb_api_semantics_fixture_service` | **Consume only.** Request closed installed Superset, unsupported-fallback, ambiguous-fallback, and executable-invalid `ScanPlan` values through the public non-installable provider API. Exact remains a production-planner/law fixture, not a positive Runtime plan fixture. | Runtime tests must not include `scan_plan_test_access.hpp`, compile Semantics sources, construct Connector metadata, call the planner, or mutate private plan fields. |
| `test/cpp/runtime/execution/http_scan_executor_policy_tests.cpp` | Own the classification-independent admission matrix, executable counterexamples, redacted failure assertions, and zero-transport/zero-authorization evidence. | The oracle asserts executable outcomes and request profiles, not implication, exactness, composition, or explanation prose. |
| Existing Runtime pagination, resource, authorization, curl, security, and lifecycle tests | Own regression evidence that all accepted forms preserve request traces and the unchanged operational envelope. | Superset selected forms preserve the canonical typed request; fallback forms with no input preserve the canonical unrestricted behavior; Exact relabeling remains fail-closed. |
| `test/cpp/runtime/targets.cmake` and Runtime package target files, if changes are needed | Preserve the focused consumer dependency on `duckdb_api_semantics_fixture_service` and the public plan/runtime services. | Runtime targets must not directly list Semantics production or private fixture-construction sources. Root product composition remains lead-owned. |
| `src/runtime/README.md` and adjacent Runtime API/state-machine comments | Document the admitted execution forms, validation-before-side-effects, typed-input authority, lifetime, resource, cancellation, failure, and close rules. | Durable relational meaning remains in the shared contracts and Semantics APIs; Runtime docs do not claim SQL equivalence. |

No Runtime work is planned in Query's DuckDB adapter, Connector's catalog,
Semantics' classifier or fixture implementation, shared contract files, root
product/build integration, or another team's plan.

## Runtime-owned oracle plan

| Oracle | Required Runtime evidence |
| --- | --- |
| `duckdb_api_http_scan_executor_policy_tests` | A coherent installed Superset plan selects the sole `VISIBILITY_PRIVATE` request profile. Exact relabelings of GitHub's Superset source are denied before side effects, not reclassified; the controlled Exact operation remains at the planner/law boundary and never becomes a positive Runtime fixture. Unsupported and ambiguous fallbacks admit to the same no-input profile. Reason/snapshot text never changes the request. Unknown enum/input variants, invalid ownership or delegation, incomplete structured facts, operation/input mismatches, and all existing operation/schema/auth/network/feature/pagination/budget counterexamples fail at open with zero transport and no credential decoration. |
| `duckdb_api_scan_plan_fixture_tests` consumer-boundary assertions, owned by Semantics but required by Runtime handoff | The safe provider header exposes only immutable plan factories; the fixture service links the plan service but not Connector, Query, or planning services; Runtime targets link the fixture service rather than its production sources or private construction access. |
| `duckdb_api_link_pagination_tests` and `duckdb_api_http_scan_pagination_tests` | Every selected form preserves exactly one `visibility=private` field on every canonical request and accepted Link transition; both fallback classifications preserve its absence. Backpressure, empty nonterminal pages, 64/36 draining, malformed or authority-widening Link denial, late failures, stream isolation, cancellation, early close, and the 32-page ceiling remain unchanged. |
| `duckdb_api_scan_resource_accounting_tests` | Superset selected and fallback traces debit actual requests, pages, bytes, records, retained memory, and elapsed time against the same ceilings. A shorter selected trace changes usage only, and advertised continuation at a ceiling remains terminal. |
| Authorization, executor, decoder, and existing-profile regressions | The authorization alternative is moved once; bearer placement and redaction remain exact; anonymous search, authenticated user, and repository decoding remain compatible; missing, null, wrong-type, duplicate, or over-budget repository values fail strictly. |
| Curl transport, pagination, budget, security, and lifecycle targets | Real curl emits only the installed canonical selected or unrestricted target, one bounded bearer header per page, and no widened authority. Open remains socket-free; active cancellation and repeated close release one request without replay; executor recovery, process lifetime, installed-artifact isolation, and clean terminal behavior remain intact. |

Relational Semantics owns the production decision law matrix and proves exact,
superset, unsupported, ambiguous, invalid, Boolean, `NULL`, occurrence,
residual, ownership, and capability behavior. Query Experience owns actual
DuckDB equivalence, explanation, planning-failure/pruning Runtime-open counts,
prepare/copy lifecycle, local order/bounds, and the public demonstration.
Runtime's request and lifecycle matrix is necessary execution evidence, not a
substitute for either semantic or SQL equivalence.

## Dependencies, parallelization, and serialization

Runtime implementation begins after Relational Semantics publishes the
accepted `ScanPlan` accessors and public fixture factories for every installed
executable form and malformed executable counterexample. Connector's validated
Exact fixture remains behind Semantics' production-planner law service because
its controlled operation is deliberately non-installable; Runtime does not
consume Connector fixtures directly or construct a positive Exact plan.

Once that provider interface compiles, Runtime admission work may proceed in
parallel with Query's adapter and differential work. Within Runtime, these
read/write-disjoint tracks may proceed in parallel:

- admission and policy-test updates;
- pagination/resource regression expansion using published fixtures; and
- curl/security/lifecycle regression expansion using the same public fixtures.

Changes to the admitted-profile header, `http_plan_admission.cpp`, and shared
Runtime test-support request observations serialize under one Runtime owner.
Semantics owns the fixture header and implementation with one writer; Runtime
supplies required consumer cases but does not patch around a missing provider
variant. Root targets, shared contract propagation, release records, and final
composition serialize under the lead agent.

No locally constructed `ScanPlan`, test-only semantic setter, reason-string
parser, raw query-parameter bridge, or temporary include of provider-private
sources is an acceptable dependency workaround.

## Documentation obligations

- Supply Runtime-owned wording to the lead for the native mapping and
  admission/execution sections of `docs/RUNTIME_CONTRACTS.md`: closed
  structured classifications, classification-independent typed-input
  authority, zero-I/O invalid states, and unchanged stream/resource behavior.
- Supply execution-boundary wording for `docs/ARCHITECTURE.md` without taking
  ownership of relational proof, SQL behavior, or Connector declarations.
- Update `src/runtime/README.md` to route admission, fixture consumption, and
  classification-independent regression targets.
- Beside admission, document accepted inputs/outputs, exhaustive unknown-value
  denial, validation-before-side-effects, error/allocation ownership,
  immutability, concurrency/lifetime, and the fields that are explicitly not
  request authority.
- Keep pagination and stream comments current for canonical reconstruction,
  one deadline, release ordering, backpressure, terminal failure,
  cancellation, close idempotence, and destruction.
- Examples, explanation, diagnostics narrative, release notes, roadmap, root
  goal completion, and shared contract integration remain lead/Query owned.

## Verification and handoff

Run focused Runtime evidence first:

- `duckdb_api_http_scan_executor_policy_tests`;
- `duckdb_api_link_pagination_tests`;
- `duckdb_api_http_scan_pagination_tests`;
- `duckdb_api_scan_resource_accounting_tests`;
- `duckdb_api_authorization_contract_tests`;
- `duckdb_api_execution_contract_tests`;
- `duckdb_api_json_root_array_decoder_tests`;
- `duckdb_api_curl_http_transport_tests`;
- `duckdb_api_curl_http_pagination_tests`;
- `duckdb_api_curl_link_metadata_tests`;
- `duckdb_api_curl_http_budget_tests`;
- `duckdb_api_curl_http_lifecycle_tests`;
- `duckdb_api_curl_tls_security_tests`; and
- `duckdb_api_curl_transfer_policy_tests`.

Then run the relevant supported-cell gates from `AGENTS.md`:

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

The lead owns the goal-wide native/community gates and the final include/source/
link dependency audit. Because admission touches relational correctness and
guards credentials, network authority, resources, cancellation, and close,
independent adversarial review is required before completion.

## Observable interaction exits

The Runtime interaction is currently **Collaboration** for the changed plan and
fixture boundary. It becomes **X-as-a-Service** only when final source, target,
and oracle evidence shows all of the following:

1. Runtime production code includes only the public immutable plan/runtime
   APIs and imports no Connector, Query, DuckDB adapter, planner, or
   Semantics-private construction type.
2. Runtime's focused tests consume installed Superset, unsupported, ambiguous,
   and invalid cases only through `duckdb_api_semantics_fixture_service`; their
   targets do not list Semantics production sources or private fixture
   constructors.
3. One immutable admitted profile is the sole request authority after open.
   Superset alone admits the installed typed selected request; Exact relabeling
   fails; unsupported and ambiguous labels cannot add an input; reason,
   snapshot, predicate, residual, and decoded values are never parsed or
   reclassified.
4. Every invalid planning case yields zero Runtime opens in Query's oracle, and
   every malformed executable-plan fixture fails Runtime open with zero
   authorization materialization, DNS/socket work, or transport observation.
5. Focused request, pagination, resource, security, cancellation, close,
   terminal-failure, and existing-profile targets pass for selected and
   fallback forms, including real-curl coverage and repeated-use isolation.
6. Query consumes only `ScanExecutor`/`BatchStream` and contains no Runtime
   request construction, pagination, credential-placement, transport, or
   resource-accounting logic.
7. Adjacent API/state-machine documentation identifies ownership, lifetime,
   error authority, validation order, resource accounting, cancellation, and
   close behavior, and temporary collaboration hooks are absent.

If any condition remains unmet, the interaction remains open even when the
end-to-end SQL differential passes.

## Explicit non-work

This workstream does not:

- classify or compose predicates, prove exactness or occurrence preservation,
  choose fallback, assign residual ownership, or render explain output;
- construct `ScanRequest`/`ScanPlan`, expose plan mutation, or consume
  Connector/Query/Semantics-private test machinery;
- add a public mapping, request input, protocol, projection/order/bound
  pushdown, Runtime residual evaluator, provider, retry, cache, rate wait,
  prefetch, parallel pagination, resume, total, or snapshot guarantee;
- widen credential, secret, network, redirect, proxy, TLS, resource,
  concurrency, process-lifetime, or dynamic-unload policy; or
- edit shared contracts, code, build files, another team's plan, root goal,
  release integration, Git history, or goal status as part of this planning
  task.

Evidence requiring excluded authority returns to the lead for contract and RFC
assessment rather than being hidden in Runtime admission.

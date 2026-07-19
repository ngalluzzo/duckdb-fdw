# Relational Semantics plan: bounded authenticated repository traversal

## Outcome and status

Extend the permanent, deterministic `ScanRequest -> ScanPlan` service so the
fixed `github.authenticated_repositories` relation is planned as one bounded,
sequentially paginated base relation. Its base domain is the
duplicate-preserving bag of rows decoded from every accepted page during the
scan. Mutable traversal order is not DuckDB ordering, per-response cardinality
is not a limit, and pagination grants no filter, ordering, limit, or offset
authority outside DuckDB.

Status: **Implemented; provider interactions exited; final product gates open**.
RFC 0007 is Accepted and the parent goal is Active. Query Experience remains
accountable for the product outcome. Relational Semantics owns only the
offline semantic handoff and its proof; it does not own Connector declarations,
pagination mechanics, or the DuckDB adapter.

The semantic delta is:

- **Current:** a `ScanPlan` classifies one accepted response as the complete
  base domain, carries pagination only as `FeatureState`, and the planner
  rejects every enabled pagination declaration.
- **Delivered:** a `ScanPlan` distinguishes disabled pagination from one exact
  immutable sequential Link plan, defines the paginated scan-wide base domain,
  preserves per-response cardinality separately, carries explicit page and
  scan budgets, and rejects unsupported or contradictory pagination instead of
  executing page 1 as a complete-looking relation.

## Permanent source ownership and boundaries

| Artifact | Relational Semantics responsibility | Boundary evidence |
| --- | --- | --- |
| `src/include/duckdb_api/scan_plan.hpp` | Define the complete private pre-`1.0` planning API: explicit paginated base-domain meaning, immutable `PaginationPlan`, scoped resource plans, accessors, ownership classifications, and construction authority. Replace the pagination feature bit with a closed disabled-or-Link value. | Query and Runtime can consume the returned plan through const accessors; neither can default-construct, partially construct, assign, or mutate `PaginationPlan` or `ScanPlan`. No Connector or Runtime internal type crosses this header. |
| `src/scan_plan.cpp` | Implement value access, invariant-preserving names, and locale-independent safe explanation for pagination, base-domain scope, page/scan budgets, and no-delegation classification. | Snapshots contain only plan facts and safe logical-secret rendering; no response Link, received URL, credential, repository value, mutable page state, or Runtime counter can enter explanation. |
| `src/scan_planner.cpp` | Remain the sole side-effect-free validation and construction path from immutable `CompiledConnector` plus conservative `ScanRequest`. Validate cross-field pagination consistency, intersect Connector and host budgets, and fail closed on unsupported metadata. | Planning uses public const Connector accessors and Query's request only; it performs no I/O, secret lookup, response parsing, runtime construction, or relation-name/auth-profile inference. |
| `test/cpp/scan_plan_contract_tests.cpp` | Prove public type shape, immutability, three golden plans, base-domain/cardinality/limit distinctions, stable safe explanation, and preserved `0.4.0` plan behavior. | Tests consume exact selected relations and public accessors; they do not use Connector construction access or Runtime types. |
| `test/cpp/scan_planner_tests.cpp` | Prove deterministic selection, conservative construction, budget intersection, pagination contradictions, unavailable-capability behavior, no page-one fallback, and regressions for both existing relations. | The target remains independently runnable without DuckDB, transport, Secret Manager, or a live service. |
| `test/cpp/support/scan_plan_contract_test_support.hpp` | Replace credential-requirement selection helpers with exact stable relation lookup for all Semantics tests. | A catalog containing two required-credential relations cannot select by vector order or authentication profile. |
| `test/cpp/support/scan_plan_test_fixtures.hpp` and `.cpp` | Provide a non-installable Semantics test service with exact anonymous, authenticated-user, and authenticated-repositories plan factories. Migrate the ambiguous authenticated factory to an explicitly named authenticated-user factory. | Runtime consumers receive closed valid plan values, never planner-private setters, Connector test access, or arbitrary construction authority. |
| New `test/cpp/support/scan_plan_pagination_test_fixtures.cpp` and focused contract test | Own named invalid `PaginationPlan` states and prove each factory changes exactly the advertised fact. Keep pagination contradictions separate from unrelated providers/retry/cache feature mutations. | `ScanPlanTestAccess` remains implementation-only; consumer targets include only the safe fixture header and use enumerated factories. |

No new production module is planned. `PaginationPlan` has the same producer,
consumers, immutability, copy lifetime, construction path, and explanation
contract as the containing `ScanPlan`; keeping it in `scan_plan.*` preserves
one cohesive Semantics team API. Pagination-specific test construction is
split because valid pagination and its counterexamples now have a distinct
oracle family, while the existing representation/planner split remains intact.

Relational Semantics does not edit `ScanRequest`, Connector metadata, Runtime
execution, Query adapter/composition, root build files, release identities, or
authoritative contracts in this parallel workstream. The lead owns registration
of any new focused test source and coherent contract/version integration.

## `PaginationPlan` meaning

### Closed state and base domain

Every `ScanPlan` carries one immutable pagination value:

1. **Disabled:** no pagination strategy, continuation relation, page bindings,
   consistency claim, total/resume capability, target profile, or aggregate
   scan budget is present. The anonymous search-page and authenticated-user
   plans remain in this state.
2. **Sequential Link:** the repository plan records Link strategy, closed
   `next` relation, sequential dependency, mutable consistency, unsupported
   total and resume, typed page size `100`, first page `1`, required increment
   `1`, the immutable permitted transition profile, and explicit per-page and
   per-scan budgets.

The repository plan uses an explicit paginated JSON-record base-domain
classification. It means the duplicate-preserving bag of every row decoded
from each accepted source response until true source exhaustion. It does not
mean a snapshot, stable SQL order, total, deduplicated identity set, or rows
from only the first response. The existing JSON-path base-domain variant
retains its single-response meaning, and the root-object variant retains its
exactly-one-on-success meaning.

`PlannedCardinality::ZERO_TO_MANY` continues to describe one accepted page
response. It neither estimates the whole scan nor authorizes a remote/runtime
limit. The plan records `TRUE` as remote and residual predicate relative to the
paginated base domain, DuckDB as the owner of filter, ordering, limit, and
offset, and `NONE` for every remote/runtime ordering, limit, and offset
delegation. Ordinary DuckDB `LIMIT` may close the stream early; that lifecycle
event is not represented as a pushed or Runtime-owned limit.

### Typed transition facts

The plan carries every immutable fact Runtime needs to validate and reconstruct
a next request without importing `CompiledPagination`, parsing an explanation,
or inferring semantics from a received URL:

- closed Link/`next`, sequential, and mutable classifications;
- the typed page-size and page-number bindings, first page, and exact increment;
- the exact plan-derived scheme, host, port, path, and allowed query-field
  profile against which a typed next page is checked; and
- explicit denial of total, resume, parallelism, remote ordering, remote limit,
  retry, cache, and provider execution.

These are planning facts, not a parser or state machine. `PaginationPlan`
contains no physical Link field-value, response URL, percent-decoder rule,
current/seen page set, transport request, authorization header, credential,
deadline clock, mutable counter, cancellation state, or execution method.
Runtime owns those mechanics and may reject an unsupported executable profile,
but it must not choose strategy, consistency, base domain, ordering, or limit
meaning.

### Page and scan resource scope

The plan keeps one-attempt page authority distinct from scan-wide traversal:

- each page has one request attempt, one active request, and the accepted
  per-page header, wire, decompressed, record, string, decode-memory, batch, and
  remaining-deadline ceilings;
- the scan has at most 32 distinct page attempts/pages, 512 KiB headers,
  64 MiB wire and decompressed bytes, 3,200 decoded records, one retained page,
  one retained output batch, one active request, and one 30-second deadline;
  and
- aggregate attempts count distinct page replay units, not retries of a failed
  page. A per-page attempt count above one, scan concurrency above one, or a
  retry-enabled companion plan is contradictory.

The planner intersects relation declarations with host ceilings without
silently widening either. Per-scan ceilings cannot be below their corresponding
per-page ceiling, multiplication and addition constraints are overflow-safe,
page size cannot exceed the per-page record ceiling, and the accepted native
values cannot be weakened into truncation. The two existing relations retain
their accepted one-response, 64 KiB, three-or-one-record, 256-byte-string,
two-row-batch, and five-second effective plans even if host maxima rise to
admit the repository relation.

### Conservative validation and classification

Planning rejects before returning a plan when any of these facts is missing,
unsupported, or contradictory:

- enabled pagination on a root-object/exact-one operation, or a repository
  declaration that is absent or disabled;
- a non-Link strategy, independent dependency, stable/snapshot claim, total,
  resume, parallelism, retry, cache, or provider requirement;
- initial typed bindings that disagree with the structural operation, a target
  profile that differs from the selected operation/authentication destination,
  or a page size/start/increment outside the closed profile;
- zero, widened, inverted, overflowing, or internally inconsistent page/scan
  budgets; or
- a request/capability profile that exposes unavailable ordering or bounds, an
  incomplete projection, non-`TRUE` unavailable predicate, or missing required
  logical secret capability.

Validation may compare explicit typed Connector facts for consistency. It must
not discover pagination by scanning query parameter strings, relation names,
credential requirements, or catalog order. Failure never substitutes disabled
pagination or returns a plan for page 1 alone. The safe classification reason
identifies the paginated bag, mutable consistency, sequential dependency, and
DuckDB ownership without embedding request/response content.

## Semantics-owned oracle boundary

Focused evidence proves:

- the native repository plan is selected only by the exact requested relation,
  contains the five-column full projection, required logical secret reference,
  zero-to-many per-response cardinality, paginated bag domain, and exact
  sequential mutable plan;
- identical Connector/request inputs produce byte-identical plans and
  explanations across copies, locale, and hostile unrelated environment state;
- page-like fixed query fields on a disabled Connector fixture remain ordinary
  source identity and never enable pagination;
- the typed pagination declaration enables planning without relation-name,
  request-string, or credential-profile inference;
- every filter, ordering, limit, and offset stays in DuckDB, with no remote or
  Runtime delegation, no stable-order claim, and no whole-scan cardinality
  inferred from page size, page count, or budgets;
- per-page and per-scan bounds are distinct, one attempt per page is not retry,
  and smaller provider values narrow while every widening or scope inversion
  fails deterministically;
- unsupported/inconsistent pagination fails planning rather than falling back
  to the operation's first structural request;
- anonymous search-page and authenticated-user plans remain byte-for-byte or
  field-for-field equivalent except for the accepted `0.5.0` provenance and
  header identity changes owned by integration; and
- request, plan, snapshot, error, and fixture state exclude credential values,
  received Link values, repository data, Runtime handles, and DuckDB objects.

Semantics tests do not parse Link grammar, simulate an empty intermediate page,
issue requests, count Runtime events, or assert physical page order. Runtime's
request-sequence oracle proves traversal mechanics. Query's controlled SQL
oracle compares the resulting bag under explicit local `ORDER BY id` and proves
DuckDB filter/order/limit/offset behavior.

## Provider dependencies and dependency direction

| Participant | Relational Semantics consumes or provides | Must not cross the boundary | Readiness evidence |
| --- | --- | --- | --- |
| Connector Experience | Consumes exact relation lookup plus immutable `CompiledPagination`, structural operation, schema/auth policy, and scoped resource accessors. | Semantics does not construct Connector values, import test access, infer pagination from query fields/auth requirement, or reinterpret package/YAML syntax. | Generic paginated Connector fixture and direct catalog tests expose a coherent disabled-or-Link declaration through public const accessors. |
| Query Experience | Consumes exact relation identity, full projection, conservative capability profile, `TRUE` predicate, empty ordering, unset limit/offset, cancellation support, and logical secret reference from `ScanRequest`; provides one complete immutable `ScanPlan`. | Semantics does not call DuckDB, resolve secrets, add page state to `ScanRequest`, or prescribe adapter lifecycle. Query does not construct or mutate plan fields. | Request snapshots and bind tests prove exact repository selection and the unchanged conservative capability profile. |
| Remote Runtime | Provides public plan accessors and closed safe valid/invalid plan fixtures. | Runtime does not import Connector metadata, `ScanPlanTestAccess`, planner internals, or snapshots as authority; Semantics does not implement Link parsing, page state, transport, counters, or cancellation. | Runtime compiles and runs against the plan API and Semantics fixture service while independently proving execution behavior. |
| Lead-agent integration | Supplies this workstream's source/oracle evidence for root build registration, authoritative contract propagation, `0.5.0` activation, source identity, review, and gates. | This workstream does not edit root CMake, version/pin files, architecture/runtime/connector contracts, roadmap, changelog, product composition, or another team's plan. | Each new source is registered once, contracts agree, the product relation activates only after Runtime accepts the real plan, and cached/fresh gates pass. |

## Dependency overlap and disjoint parallel work

The current Semantics test service assumes there is exactly one relation with
`CompiledCredentialRequirement::REQUIRED` in both
`scan_plan_contract_test_support.hpp` and `scan_plan_test_fixtures.cpp`. RFC
0007 adds a second required-credential relation, so catalog activation would
make those helpers ambiguous or order-dependent. Relational Semantics owns the
correction: migrate tests and safe factories to exact stable relation
identifiers before the third native relation is integrated. Runtime owns only
its call-site migration after the renamed fixture API is fixed.

The current `FeaturePlanCounterexample::PAGINATION_ENABLED` also treats every
enabled pagination state as invalid. Once the repository plan is valid, that
boolean mutation no longer expresses an executable contract. Semantics removes
pagination from the catch-all feature fixture, adds typed pagination
counterexamples, and gives Runtime a valid exact repository-plan factory.
Runtime then tests support/denial against those closed values without mutating
the plan.

| Parallel track | Semantics-owned files | May proceed when | Must wait for or avoid |
| --- | --- | --- | --- |
| Plan representation | `scan_plan.hpp`, `scan_plan.cpp`, `scan_plan_contract_tests.cpp` | RFC 0007 fixes the semantic distinctions and budget values. | No Connector source edit; exact golden construction waits for public `CompiledPagination` accessors. |
| Planner mapping | `scan_planner.cpp`, `scan_planner_tests.cpp`, exact relation test helper | Connector freezes its const metadata/resource API and paginated fixture. | Do not parse query strings or activate the native relation to compensate for a missing provider fact. |
| Semantics test service | `scan_plan_test_fixtures.*`, new pagination counterexample source/test, existing feature fixture cleanup | The plan representation and exact fixture names are fixed. | Runtime consumer call sites migrate afterward; no simultaneous edits to the shared fixture header. |
| Connector provider work | No Semantics-owned files | May develop its catalog API, validation, native-disabled values, and consumer fixture in parallel. | Public third-relation activation waits for the real plan and Runtime path. |
| Runtime and Query consumers | No Semantics production files | Runtime parser/state-machine code and Query provider-fake lifecycle code are file-disjoint after the plan API is published. | Consumers do not add temporary plan constructors, relation-name branches, or pagination facts to Query to bypass the provider gate. |

Root source lists and test-target registration are integration overlaps owned
by the lead. The immutable plan header is a producer-owned serial handoff, not
a parallel writing surface for Runtime. Parallel implementation resumes after
its shape and safe fixture API compile.

## Sequencing and gates

1. **Governance gate — satisfied.** RFC 0007 is Accepted with product approval
   and the amended bag/ordering decision; the product goal is Active.
2. **Provider-shape gate.** Connector and Semantics freeze the public const
   declaration-to-plan mapping, scoped resource fields, disabled state, and
   safe explanations. Runtime confirms the plan carries every required
   execution fact without a Connector dependency. No public relation is added.
3. **Representation gate.** Land the immutable `PaginationPlan`, paginated base
   domain, accessors, documentation, safe explanation, and type/immutability
   oracles while preserving the two disabled plans.
4. **Planning gate.** Consume Connector's paginated fixture, remove unique-auth
   selection, construct the exact repository plan offline, reject every
   contradiction and page-one fallback, and pass the focused planner and plan
   contract targets.
5. **Fixture-service gate.** Publish exact closed valid factories and typed
   pagination counterexamples; pass the fixture implementation/consumer
   boundary target. Only then may Runtime treat the plan API as its normal test
   service.
6. **Runtime-consumer gate.** Runtime accepts the valid repository plan and
   rejects incompatible plan/profile intersections before I/O without reading
   Connector metadata or reclassifying relational facts. Query continues to
   retain the plan without making adapter decisions from `PaginationPlan`.
7. **Activation and propagation gate.** Lead integration adds the third native
   relation and `0.5.0` identities only with the real Runtime/Query path,
   updates all affected contracts and diagnostics, and preserves both existing
   relation regressions.
8. **Verification and exit gate.** Run the focused Semantics targets first,
   then `make build`, `make test`, `make demo`, source/dependency identity
   checks, a fresh native product cell, agent-asset validation, unstaged and
   staged diff checks, final declaration/include/construction/build/test audit,
   and required independent/adversarial review. Test names alone do not close
   an interaction.

## Contract traceability and documentation obligations

| Contract layer | Semantics evidence or handoff |
| --- | --- |
| Architecture | Supply the exact paginated bag, mutable consistency, no-order/no-limit, and fail-closed fallback wording for lead-owned propagation. |
| Connector syntax and status | Unaffected by Semantics implementation: native metadata remains distinct from inactive `duckdb_api/draft` authoring. Connector and lead record that status. |
| Schema and validation | Consume only Connector-validated schema/pagination facts; add planner cross-field rejection rather than a second Connector constructor. |
| Compiled IR | `PaginationPlan` preserves strategy, dependency, consistency, total/resume denial, transition profile, and page/scan resource scope without Connector representation leakage. |
| Planning | Focused positive, negative, boundary, determinism, conservative-capability, and no-I/O oracles prove construction and ownership. |
| Execution | Runtime receives complete typed facts and owns enforcement; no semantic choice is deferred implicitly. |
| Diagnostics and explanation | Planner errors name a safe field such as `pagination.strategy`, `pagination.consistency`, `pagination.pages`, or a scoped budget; snapshots explain applied facts but never echo received Link/URL, credential, or row data. |
| Tests and fixtures | Semantics owns planner/plan/fixture laws; Runtime owns request sequences and state; Query owns DuckDB evaluation and product behavior. |

Adjacent `scan_plan.hpp` documentation must state purpose, producer/consumers,
construction authority, immutable copy lifetime, private compatibility status,
disabled/enabled invariants, scan-wide bag versus per-response cardinality,
mutable/no-order/no-total/no-resume meaning, one-attempt-per-page versus retry,
page/scan budget scope, and the absence of response or credential state.

Planner comments must explain why typed declaration consistency is checked,
why missing/unsupported pagination rejects instead of falling back to page 1,
why page size and aggregate budgets grant no limit authority, and why exact
relation identity replaces credential-profile selection. Comments must not
describe Link grammar, curl callbacks, DuckDB table-function lifecycle, or
secret resolution. `scan_plan.cpp` must render each semantic distinction once
and keep explanation separate from runtime authority.

## Explicit non-work

- `CompiledPagination` representation, native catalog construction,
  Connector validation/explanation, YAML/package syntax, or author experience.
- `ScanRequest` fields, table-function parameters, schema binding, DuckDB
  lifecycle, prepared-secret resolution, `DataChunk` production, or SQL/live
  product evidence.
- Physical Link capture/grammar, response normalization, next-page parsing,
  request reconstruction, page state, seen-page tracking, transport, bearer
  decoration, DNS/TLS policy enforcement, aggregate counters, empty-page
  same-pull mechanics, backpressure, cancellation, close, or destruction.
- Retry, rate-limit waiting, parallel pages, resume, cache, providers, GraphQL,
  deduplication, snapshot isolation, remote ordering/limit, caller URLs,
  headers, filters, page values, or a public native ABI.
- Root CMake/Makefile/runner edits, authoritative contract files, roadmap,
  changelog, version/source identities, release evidence, product composition,
  live-secret custody, Git integration, goal closure, or another team's plan.

## Observable interaction exits

- **Connector Experience — Exited to X-as-a-Service.** Exact disabled and
  paginated declarations compile through public const
  accessors into independently runnable golden plans; all contradiction and
  page-one-fallback counterexamples fail offline; Semantics does not construct
  Connector values, import test access, infer from query/auth/relation names,
  or require coordinated knowledge beyond the documented declaration.
- **Query Experience — Exited to X-as-a-Service.** Query constructs only the
  unchanged conservative `ScanRequest`, retains the complete immutable plan,
  does not inspect pagination to make adapter
  decisions, and controlled DuckDB evidence proves local filtering, explicit
  `ORDER BY`, limit, and offset over the duplicate-preserving bag with no
  planning-time I/O or page-one fallback.
- **Remote Runtime — Exited to X-as-a-Service.** Runtime production and test
  sources include only the public plan and safe fixture service, never
  Connector metadata or `ScanPlanTestAccess`; executor
  open validates the exact profile before I/O; independent runtime oracles
  execute sequentially without reclassifying base domain, consistency,
  ordering, limit, or budgets; and routine execution changes no longer require
  planner-internal knowledge.
- **Relational Semantics workstream — Implemented; cached exit evidence
  satisfied.** Focused plan, planner, and fixture targets prove all
  positive/negative properties; final source,
  includes, construction points, consumer call sites, build graph, adjacent
  documentation, contract propagation, preserved-relation regressions, and
  cached gates support the three provider exits with no unresolved semantic or
  dependency finding. The lead retains the fresh product cell, final review,
  Git integration, and goal closure.

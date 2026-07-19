# RFC 0007: Bound authenticated repository pagination

```yaml
rfc: "0007"
title: "Bound authenticated repository pagination"
status: "Accepted"
rfc_type: "Product"
sponsor_team: "Query Experience"
technical_decision_owner: "Lead agent"
product_approver: "ngalluzzo"
authors:
  - "Lead agent"
required_reviewers:
  - "pagination_rfc_query_review"
  - "pagination_rfc_connector_review"
  - "pagination_rfc_semantics_review"
  - "pagination_rfc_runtime_review"
affected_teams:
  - "Query Experience"
  - "Connector Experience"
  - "Relational Semantics"
  - "Remote Runtime"
linked_outcome_or_objective: "Bounded authenticated repository traversal"
supersedes: "none"
```

## Summary

Add a fixed `github.authenticated_repositories` relation that uses the named
temporary secret accepted by RFC 0006 and sequentially traverses GitHub's
`GET /user/repos` Link pagination. The runtime accepts only a bounded,
strictly increasing page sequence reconstructed inside the existing exact
HTTPS authority; it never follows a response URL as transport authority.
Retries, active rate-limit waiting, parallel pagination, resume, caller input,
and connector-package activation remain excluded.

## Sponsorship and context

- **RFC type:** Product.
- **Sponsoring team:** Query Experience.
- **Linked outcome or objective:** Bounded authenticated repository traversal.
- **Why now:** RFC 0006 proves one capability-scoped authenticated request, but
  a useful remote relation commonly spans more than one response. The approved
  product outcome asks whether the permanent extension can expose such a
  resource as one bounded, cancelable DuckDB relation without manual page
  handling or wider credential authority.

The affected DuckDB user can ask one SQL question over repositories visible to
their explicitly selected GitHub credential. GitHub documents
`GET /user/repos` as the authenticated-user repository collection, permits a
fine-grained token with repository Metadata read permission, and permits at
most 100 items per page. GitHub documents a response `Link` header with
`rel="next"` for another page and omission of the header when all results fit.

Primary sources:

- [List repositories for the authenticated user](https://docs.github.com/en/rest/repos/repos?apiVersion=2022-11-28)
- [Using pagination in the REST API](https://docs.github.com/en/rest/using-the-rest-api/using-pagination-in-the-rest-api?apiVersion=2022-11-28)

## Problem

The native `0.4.0` profile rejects every plan with pagination enabled. Its HTTP
transport counts response-header bytes but discards their values, and its
runtime stream performs exactly one request before decoding all rows for that
response. The existing authenticated relation therefore cannot represent the
page sequence exposed by `GET /user/repos`.

Blindly requesting the URL in a response `Link` header would be unsafe. It
would let a remote response select a later credential-bearing destination,
path, or query. Treating multiple requests as independent scans would also
duplicate or omit pages and disconnect cancellation and aggregate resource
accounting. Fetching every page eagerly would defeat backpressure and make a
DuckDB early stop ineffective.

Observed facts are:

- the current immutable catalog already has an inactive pagination boolean;
- `ScanPlan` carries only enabled or disabled pagination rather than a
  strategy, consistency, or request-patch contract;
- `HttpResponse` contains status and byte counts but no normalized Link value;
- the curl callback already observes response headers while enforcing a hard
  byte ceiling;
- `BatchStream` is pull-oriented and owns cancellation and idempotent close;
- the architecture and draft contracts already require sequential Link
  pagination by default; and
- the selected upstream collection is mutable and supplies no scan-wide
  snapshot guarantee.

No decision-critical feasibility unknown remains. Exact parser behavior,
state-machine transitions, resource enforcement, and late failure are required
delivery evidence.

## Decision drivers and invariants

- **Must preserve:** deterministic network-free bind and planning; immutable
  connector and plan snapshots; strict typed conversion; DuckDB ownership of
  filter, ordering, limit, and offset; the anonymous and authenticated-user
  relations from `0.4.0`; exact-name temporary-secret resolution; fixed bearer
  placement; post-DNS destination policy; bounded pull, cancellation, and
  idempotent close; and redacted failure containment.
- **Must enable:** a DuckDB user to query repository rows across every accepted
  page exposed during one sequential scan without supplying page state.
- **Must bound:** requests, pages, response headers, wire bytes, decompressed
  bytes, decoded records, extracted strings, decoded memory, output batches,
  wall time, and concurrency at both page and scan scope.
- **Must fail closed:** malformed Link syntax, multiple `rel="next"` targets,
  a non-HTTPS origin, another host or port, a path other than `/user/repos`, an
  unrecognized or repeated query field, an invalid page number, a non-increasing
  page, a cycle, budget exhaustion, schema failure, cancellation, or a late
  non-success response cannot become clean source exhaustion.
- **Must not introduce:** retries; rate-limit sleeps; parallel page fetching;
  saved resume state; automatic replay; redirects; caller URLs, headers, page
  sizes, or filters; connector YAML loading; cache; provider execution;
  GraphQL; environment or persistent secret providers; a public native ABI; or
  a snapshot-isolation claim.

## Proposed decision

The native GitHub catalog adds a fixed `authenticated_repositories` relation
with one many-row REST operation and an explicit sequential Link-pagination
declaration. Connector metadata declares the strategy, `rel="next"`, page size
100, mutable consistency, no total, no resume, and the closed request profile.
It contains no response URL, secret name, credential value, or DuckDB object.

Relational Semantics converts that declaration into an immutable pagination
plan. The plan says that the base domain is the duplicate-preserving bag of
rows decoded from every accepted page observed during the scan; the source is
mutable, traversal order is not a DuckDB ordering guarantee, and no remote or
runtime ordering or limit is delegated. Pagination changes source traversal,
not relational ownership.

Remote Runtime owns one sequential state machine. It starts with page 1,
requests at most one page at a time, decodes and yields bounded batches from
that page, and asks for another page only after the current decoded page is
consumed. A successful `BatchStream::Next` result always contains at least one
row. If an accepted page is empty and has a valid `rel="next"`, the same pull
continues through the next page under the same deadline and aggregate budgets
until it can return a nonempty batch, report true exhaustion, or raise a
terminal error. This is required because DuckDB treats a zero-cardinality table
function chunk as finished and would not issue another pull. Absence of
`rel="next"` ends the stream. Cancellation, close, terminal failure, or an
exhausted scan budget prevents another request.

The transport returns only the bounded response metadata needed by the
protocol service: the physical `Link` field-values in receipt order. It does
not return a raw dependency response or make a pagination decision. The
Link-pagination service parses the combined field-value grammar, requires zero
or one `rel="next"` target, validates it against the plan, extracts a typed next
page number, and reconstructs the next request from the immutable operation.
It never sends the received URL string.

For this relation, the accepted next target is exactly:

- scheme `https`;
- host `api.github.com`;
- port 443, whether omitted or explicit;
- path `/user/repos`;
- one `per_page=100` query field; and
- one positive decimal `page=N` field where `N` is exactly the current page
  plus one.

Fragments, user information, alternate encodings of authority, empty query
fields, duplicate fields, unknown fields, and every other target fail as
pagination-policy errors. Percent decoding is not used to recover authority,
path, or field names. The parser retains seen page identities as a second cycle
defense even though exact increment already excludes ordinary cycles.

Each page has one request attempt. Pagination is not retry: a later page is a
new replay unit identified by its validated page number, while a failed page is
never attempted again. The authorization capability remains owned by the scan
and decorates each validated page request with the same fixed host/header
policy. It is released on completion, cancellation, failure, close, or
destruction.

The effective native execution envelope is explicit and may only be narrowed
by private test composition:

| Resource | Per-page ceiling | Per-scan ceiling |
| --- | ---: | ---: |
| Request attempts | 1 | 32 |
| Pages | 1 | 32 |
| Header bytes | 16 KiB | 512 KiB |
| Wire response bytes | 8 MiB | 64 MiB |
| Decompressed response bytes | 8 MiB | 64 MiB |
| Decoded records | 100 | 3,200 |
| Extracted string bytes per value | 512 | 512 |
| Decoded page memory | 2 MiB | 2 MiB retained at once |
| Output batch rows | 64 | 64 retained per returned batch |
| Wall time | remaining scan deadline | 30 seconds |
| Active page requests | 1 | 1 |

Crossing any ceiling fails with a redacted resource error. A scan never
silently truncates at the last page or row permitted by a budget. The fixed
ceiling intentionally bounds unusually large accounts; raising it is a later
resource-policy decision, not a query option.

### Public behavior

The user reuses the explicit temporary secret accepted by RFC 0006:

```sql
CREATE TEMPORARY SECRET github_default (
    TYPE duckdb_api,
    PROVIDER config,
    TOKEN 'github-token-value'
);
```

The new query is:

```sql
SELECT id, full_name, private, fork, archived
FROM duckdb_api_scan(
    connector := 'github',
    relation := 'authenticated_repositories',
    secret := 'github_default'
);
```

Each accepted repository object produces one row with required, non-null
`id BIGINT`, `full_name VARCHAR`, `private BOOLEAN`, `fork BOOLEAN`, and
`archived BOOLEAN`. Required extraction does not add DuckDB-visible `NOT NULL`
metadata. The initial request is structurally fixed:

```text
GET /user/repos?per_page=100&page=1 HTTP/1.1
Host: api.github.com
Accept: application/vnd.github+json
Authorization: Bearer [resolved token]
User-Agent: duckdb-api/0.5.0
X-GitHub-Api-Version: 2022-11-28
```

HTTP `200` with no next link completes the relation. HTTP `401` and `403`
retain the authentication/authorization failures from RFC 0006. Any other
non-2xx response, invalid page body, or invalid next link fails the query. A
failure after earlier batches were delivered cannot retract data already
observed by a streaming client, but the stream never reports successful
completion and DuckDB reports the statement failure.

The collection is not snapshot isolated. Repository creation, deletion,
renaming, or permission changes during traversal can make the upstream page
sequence omit or repeat a logical repository. The extension promises faithful
bounded traversal of the accepted sequence, not a point-in-time account
snapshot. It does not deduplicate IDs because doing so would add memory,
ordering, and mutable-source semantics not supplied by GitHub.

Ordinary DuckDB `LIMIT` may cause the engine to stop pulling and close the
stream before source exhaustion. That is cancellation of unnecessary work, not
remote limit pushdown and not evidence that the full source was traversed.

The existing `github.authenticated_user` and
`github.duckdb_login_search_page` SQL and schemas remain supported. Supplying
or omitting `secret` retains their RFC 0006 behavior.

### Shared interfaces

- **Connector Experience provides:** immutable `CompiledPagination` metadata
  rather than an inactive boolean, plus the repository schema, structural
  request, mutable consistency, sequential dependency, page size, next
  relation, and relation-owned ceilings. This is native product metadata; it
  does not activate package syntax or connector-author compatibility.
- **Query Experience provides:** the new fixed relation through the existing
  table function and named-secret path, consumes a bounded `BatchStream`, and
  translates pagination, resource, cancellation, and late-page failures once
  at the DuckDB boundary. The adapter does not parse Link fields, construct
  page requests, or accumulate all rows.
- **Relational Semantics provides:** an immutable `PaginationPlan` with
  sequential dependency, mutable consistency, no total or resume claim, no
  relational delegation, and explicit page and scan budgets. It remains
  offline and contains no response metadata or credential bytes.
- **Remote Runtime provides:** bounded Link metadata capture, the independently
  testable Link parser and page state machine, one-page-at-a-time execution,
  aggregate accounting, cancellation and close, and structured redacted
  failures. Runtime consumes the plan without reclassifying base-domain,
  ordering, or limit meaning.

`HttpResponse` gains a bounded normalized metadata value rather than exposing
libcurl or a general header map. `BatchStream` retains its existing pull,
cancel, and close methods, and strengthens its return contract so `true`
always accompanies a schema-aligned nonempty batch while `false` means clean
source exhaustion. Pagination is normally observable through repeated pulls;
empty nonterminal pages are crossed inside one pull and do not add a
DuckDB-specific runtime method. These private pre-`1.0` team interfaces are not
public native ABIs.

### Operational behavior

- Bind, describe, explain, and prepare perform no secret lookup and no network
  I/O. Execution resolves the temporary secret once and uses one immutable
  authorization capability for the whole scan.
- Executor open validates the complete plan/profile intersection without
  opening a socket. The first pull starts page 1.
- Only one page body and its extracted rows are retained at a time. The next
  request cannot begin while unconsumed decoded rows remain. An empty page
  releases its body and metadata before the same pull advances to its validated
  next page.
- Each request revalidates the fixed plan-derived destination before bearer
  decoration. Response metadata can narrow the next page number but cannot
  grant authority.
- Redirects, proxy, netrc, cookies, environment lookup, filesystem credential
  lookup, caller headers, and arbitrary destinations remain disabled.
- TLS peer and hostname verification, post-DNS address policy, and request
  header budgeting apply independently to every page.
- One steady-clock deadline and aggregate counters belong to the scan. A new
  page receives only the remaining time and budget.
- A `Link` value is counted in the ordinary header ceiling before capture. The
  captured representation is also charged to decoded memory and released
  before the next response replaces it.
- `429`, rate-limit `403`, and every other late non-success response fail
  immediately. This RFC records no sleep, retry, or fallback behavior.
- `Cancel` publishes cancellation before waiting for an active pull; `Close`
  is idempotent and prevents later requests. Stream destruction releases the
  capability, transport state, page metadata, decoded rows, and counters.
- Dynamic unload and reload remain unsupported.

## Topology impact

| Team | Role in this RFC | Interface or charter impact | Interaction mode | Exit condition |
| --- | --- | --- | --- | --- |
| Query Experience | Sponsor and product-outcome owner | Adds the query-visible relation and consumes bounded multi-page execution through the existing DuckDB lifecycle | Collaboration | The accepted SQL succeeds and fails coherently; adapter source depends only on documented Connector, Semantics, and Runtime APIs and contains no pagination mechanics |
| Connector Experience | Immutable metadata provider | Replaces the pagination boolean with an explicit native declaration and relation-owned ceilings without activating YAML | Collaboration, then X-as-a-Service | The catalog compiles and explains the repository relation deterministically; consumers neither construct metadata internals nor infer pagination from request fields |
| Relational Semantics | Planning provider | Adds an explicit immutable pagination plan, base-domain meaning, mutable consistency, and conservative no-delegation classification | Collaboration, then X-as-a-Service | Planner oracles prove offline selection, budgets, and DuckDB relational ownership; Runtime executes without reclassifying those facts |
| Remote Runtime | Sequential pagination service provider | Adds normalized Link metadata, validation, the page state machine, aggregate accounting, and repeated authorized requests | Collaboration, then X-as-a-Service | DuckDB-free runtime tests prove sequences, denial, bounds, cancellation, close, and late failure; Query consumes only `BatchStream` |

No accountability boundary or charter moves. Connector owns declarations,
Semantics owns their relational meaning, Runtime owns protocol execution, and
Query owns the DuckDB result experience. Collaboration remains open until the
final source and test dependency audit demonstrates those interfaces.

## Correctness, security, and lifecycle analysis

- **Relational semantics and conservative fallback:** The base domain is the
  duplicate-preserving bag of rows from every accepted sequential source
  response. Pagination does not imply a stable SQL order, exact snapshot,
  total, or remote limit. Remote and residual predicates remain `TRUE`; DuckDB
  retains filtering, ordering, limit, and offset. A relation with inconsistent
  or unsupported pagination metadata fails planning rather than falling back
  to page 1.
- **Authentication, credentials, network policy, and privacy:** The named
  temporary secret and fixed bearer placement from RFC 0006 are unchanged.
  Link data supplies no authority. The parser validates before request
  construction, request construction validates before bearer decoration, and
  transport validates before socket creation. Repository names and private
  flags are query data by product intent but cannot enter repository fixtures,
  evidence, or diagnostics from the live token.
- **Resource budgets, backpressure, and cancellation:** Per-page and aggregate
  counters use overflow-safe addition. One decoded page and one output batch
  are retained. Pulling controls forward progress, with empty nonterminal pages
  crossed only inside the active pull; cancel, close, deadline, or budget
  exhaustion prevents another request and aborts an active transfer.
- **Replay units, retries, caching, and duplicate prevention:** Each validated
  page number is one replay unit and has exactly one attempt. There is no retry
  or cache. The runtime detects repeated pagination state but does not
  deduplicate source rows or replay a failed page.
- **Concurrency, immutability, and state ownership:** One scan owns one
  immutable plan, one moved authorization capability, one mutable page state
  machine, one deadline, and aggregate counters behind the stream's existing
  synchronization. Separate scans share only immutable executor and transport
  services. Concurrency is one page request per scan.
- **FFI, initialization, reload, shutdown, and failure containment:** The
  DuckDB callback and exception boundary remain unchanged. Global
  initialization resolves one secret and opens one stream without I/O; local
  pulls may issue pages. Cancellation maps once to DuckDB interruption and all
  other failures map once to a redacted DuckDB error. Dynamic reload remains
  unsupported.
- **Diagnostics, redaction, metrics, and progress:** Errors may name a safe
  stage and policy field such as `pagination.next`, `pages`, or
  `response_bytes`; they do not include Link contents, repository data,
  destination strings received from the server, or credential material.
  Progress remains unknown because GitHub supplies no accepted total.

## Compatibility and migration

This is an additive pre-`1.0` SQL relation. No migration is required for the
two existing relations or temporary secrets. The native connector identity and
extension version advance coherently to `0.5.0`; immutable `0.4.0` release
artifacts do not change.

The supported DuckDB/platform cell remains the one already declared for the
native preview. Missing adapter capabilities retain conservative DuckDB-owned
relational behavior. Runtime profiles that cannot execute the exact
pagination, schema, authorization, network, or resource contract reject the
plan before I/O; they never execute page 1 as a degraded substitute.

Rollback means using the immutable `0.4.0` artifact, where the new relation is
absent. There is no persisted pagination state or stored-data migration.

## Evidence and bounded trials

| Question or claim | Evidence required | Method or fixture | Result and limitations |
| --- | --- | --- | --- |
| GitHub exposes the required collection and token permission | Current primary-source endpoint contract | Official GitHub REST documentation linked above | Confirmed; live compatibility can still drift and therefore remains a release check |
| GitHub communicates continuation through Link metadata | Current primary-source pagination contract | Official GitHub pagination documentation linked above | Confirmed; the product accepts only the fixed page-number shape for this relation |
| Existing transport can observe bounded response headers | Source-level seam | Current curl header callback and deterministic transport boundary | Confirmed; it currently counts and discards values, so implementation must add a narrow normalized handoff |
| Link data cannot widen credential authority | Negative request-sequence oracle | Controlled responses covering alternate scheme, host, port, path, query, user info, fragment, duplicates, and encoding tricks | Pending delivery evidence; required before goal completion, not before this decision |
| Multi-page traversal preserves pull, cancellation, and aggregate bounds | Deterministic state-machine and DuckDB end-to-end oracle | Controlled three-page source plus early close, cancellation, deadline, overflow, and late-failure variants | Pending delivery evidence; required before goal completion |
| An empty nonterminal page cannot truncate the DuckDB relation | Nonempty-success `BatchStream` contract and SQL result oracle | Controlled sequence with an empty middle page carrying a valid next link | Pending delivery evidence; required before goal completion |
| Live endpoint remains compatible | Minimal privacy-safe compatibility check | Fresh artifact with the PM-provided short-lived fine-grained token; record schema, count, page/request count, and no row values | Pending delivery evidence; not the correctness oracle |

No disposable implementation trial is needed. The current callback and stream
seams establish feasibility; the first executable work is the permanent
metadata, plan, runtime service, and DuckDB relation proved by deterministic
fixtures.

## Alternatives considered

### Expose `page` as a SQL argument

This would be simple and would not need a state machine, but it makes users
manually stitch relations, cannot guarantee no gaps or duplicates, and avoids
the product risk rather than resolving it. It also makes remote pagination a
public query input before general connector semantics are ready.

### Follow the received Link URL directly

This matches common HTTP-client examples and is flexible across upstream
changes. It gives response data too much authority, risks credential
forwarding, and makes path/query policy difficult to explain and test. The
proposal instead extracts one typed page transition and reconstructs the
request from immutable authority.

### Buffer every page before returning a row

This could hide late failure from streaming clients until completion and make
deduplication possible. It defeats backpressure, requires memory proportional
to the full collection, delays the first row, and makes early close or limit
unable to save remote work.

### Deduplicate repository IDs

This could reduce visible repetition under mutation but cannot recover omitted
rows or create a snapshot. It adds scan-wide state and changes the upstream
sequence without a principled consistency contract. The proposal states the
mutable-source limitation honestly.

### Include retries and rate-limit waiting

Those capabilities may improve completion under transient failures, but each
has independent replay, latency, cancellation, observability, and policy
decisions. Bundling them would obscure whether pagination itself works and
would violate the approved goal boundary.

### Retain the current one-request behavior

This avoids new complexity but leaves the central multi-page product mechanism
unproven and forces users to handle pagination outside DuckDB.

## Drawbacks and failure modes

- The fixed 32-page and 30-second envelope rejects very large or slow account
  traversals rather than completing them. Remote Runtime owns clear resource
  failure, and a later product decision can revisit the envelope.
- Strict next-target validation can fail after an upstream pagination-format
  change even if a generic HTTP client would continue. This is deliberate
  fail-closed compatibility behavior.
- Mutable upstream data can repeat or omit a logical repository. Query
  Experience owns documentation of this visible limitation; Relational
  Semantics prevents stronger claims.
- A late page can fail after earlier batches have crossed the streaming
  boundary. Runtime owns terminal failure and never signals clean completion;
  Query owns faithful DuckDB error translation.
- Capturing Link metadata increases transport and parser surface. Remote
  Runtime owns narrow header capture, grammar coverage, safe errors, and
  lifecycle cleanup.
- The fixed relation and page-number Link profile are not a generic connector
  authoring implementation. Connector Experience must keep native acceptance
  metadata distinct from future package compatibility.

## Acceptance and verification

- **End-to-end demonstration:** The accepted SQL with an explicit local
  `ORDER BY id` over a controlled three-page source returns exactly the
  expected typed row bag in deterministic order. A separate Runtime oracle
  proves the request/page transition sequence. Single-page termination, an
  empty middle page with a valid next link, a malformed next link,
  cancellation, early close, aggregate exhaustion, `401`, `403`, and
  late-page failure produce the specified behavior. A live check records only
  schema and aggregates.
- **Automated oracle:** Connector snapshot and validation tests; offline
  `ScanRequest -> ScanPlan` snapshots and counterexamples; independent Link
  parser/state-machine request-sequence tests; transport header-budget tests;
  runtime backpressure, resource, authorization, cancellation, close, and
  failure tests; DuckDB adapter lifecycle and SQL product tests; regressions for
  both `0.4.0` relations.
- **Quality gates:** `make build`, `make test`, `make demo`,
  `scripts/verify-source-identities.py`,
  `python3 -I -B scripts/test-native-dependencies.py`, a fresh
  `scripts/run-native-product-tests.sh` build root,
  `ruby scripts/validate-agent-assets.rb`, `git diff --check`, and
  `git diff --cached --check`.
- **Independent review:** Query Experience public/lifecycle review, Connector
  Experience metadata-boundary review, Relational Semantics base-domain and
  limit review, Remote Runtime pagination/security/resource/lifecycle review,
  followed by at least two fresh adversarial perspectives over the final code.
- **Interaction exit:** Final declarations, includes, construction points,
  build targets, focused tests, and adjacent API documentation show Query using
  only `CompiledConnector`, `ScanPlan`, and `BatchStream`; Runtime independently
  exercises pagination without DuckDB; and no consumer constructs or
  reinterprets another team's internals.

## Contract propagation

| Source of truth or artifact | Impact | Required update | Completion evidence |
| --- | --- | --- | --- |
| `docs/ARCHITECTURE.md` | Affected | Replace the `0.4.0` one-request native profile with the accepted `0.5.0` bounded repository traversal and its mutable-source and authority limits | Pending implementation |
| `docs/CONNECTOR_SPECIFICATIONS.md` | Affected only in implementation status | Keep `duckdb_api/draft` syntax non-public; record the fixed native Link subset and its stricter accepted target contract without claiming general package compilation | Pending implementation |
| `docs/RUNTIME_CONTRACTS.md` | Affected | Map native `CompiledPagination`, `PaginationPlan`, normalized Link metadata, per-page/aggregate budgets, and the sequential `BatchStream` state machine | Pending implementation |
| `docs/TEAM_TOPOLOGY.md` and active charters | Not affected | Existing accountability and team APIs already assign these responsibilities | This RFC's topology and final interaction-exit audit |
| `docs/PRODUCT_DELIVERY.md`, `AGENTS.md`, and skills | Not affected | Existing goal, RFC, topology, contract-change, review, and pagination invariants govern the work | Agent-asset validation |
| `ROADMAP.md` | Affected | Narrow `0.5.0` to bounded traversal and remove retry/rate-limit waiting from its acceptance promise | Pending implementation |
| Examples, diagnostics, fixtures, tests, and release notes | Affected | Add the SQL narrative, safe pagination/resource diagnostics, deterministic sequences, privacy-safe live evidence, public changelog, and version identity | Pending implementation |

The RFC records rationale. The updated contracts and executable evidence define
the delivered behavior.

## Unresolved questions

None. Resource values and the strict page transition are technical decisions
of this RFC; later expansion requires new evidence and the normal RFC trigger
assessment.

## Review record

| Required reviewer | Team | Result | Evidence or objection | Disposition by decision owner |
| --- | --- | --- | --- | --- |
| `pagination_rfc_query_review` | Query Experience | Approved | Initial objection: an empty nonterminal page could produce a zero-cardinality DuckDB chunk, which DuckDB treats as exhaustion | Accepted and resolved: successful `BatchStream::Next` is nonempty, empty nonterminal pages advance within the same pull, and the SQL oracle covers the counterexample |
| `pagination_rfc_connector_review` | Connector Experience | Approved | Explicit immutable native pagination metadata preserves the `CompiledConnector` boundary without activating package syntax | Accepted; delivery must prove deterministic catalog validation and explanation while keeping native metadata distinct from package compatibility |
| `pagination_rfc_semantics_review` | Relational Semantics | Approved | Initial objection: “page order” in the SQL oracle conflicted with DuckDB-owned ordering and no ordering delegation | Accepted and resolved: the base domain is a duplicate-preserving bag, the SQL oracle uses local `ORDER BY id`, and Runtime separately proves request sequence |
| `pagination_rfc_runtime_review` | Remote Runtime | Approved | Typed next-page reconstruction, fixed bearer authority, per-page and aggregate bounds, one deadline, nonempty pull semantics, cancellation, and close align with the runtime contracts | Accepted; delivery must prove Link grammar denials, aggregate accounting, cancellation, close, late failure, redaction, and capability release |

## Decision and rationale

- **Technical decision owner:** Lead agent.
- **Product approval:** ngalluzzo approved the
  `github.authenticated_repositories` relation, five-column schema, sequential
  bounded traversal, and exclusion of retries and active rate-limit handling
  on July 18, 2026.
- **Rationale:** Accepted. The decision is the thinnest permanent path from the
  proven RFC 0006 authorization capability to a useful multi-page relation. It
  preserves DuckDB relational ownership, reconstructs later requests from
  immutable least-authority metadata, bounds every page and scan, and keeps
  retries and rate-limit waiting as independent product decisions. Primary
  sources establish the upstream endpoint and Link mechanism; current source
  establishes the transport and pull-lifecycle seams; deterministic delivery
  evidence is specified for every meaningful failure path.
- **Material objections:** Query Experience's empty-page truncation objection
  was resolved by the nonempty-success `BatchStream` contract, same-pull empty
  page traversal, and a required DuckDB oracle. Relational Semantics' ordering
  objection was resolved by defining a duplicate-preserving bag, retaining all
  ordering in DuckDB, adding explicit `ORDER BY id` to deterministic SQL
  verification, and separating the Runtime request-sequence oracle. Both
  reviewers approved the amended decision. No other material objection
  remained.
- **Superseded by:** Not applicable.

Acceptance is not implementation completion.

## Follow-on goals

| Outcome or objective | Accountable team | Supporting teams and interaction modes | Activation condition |
| --- | --- | --- | --- |
| Bounded authenticated repository traversal | Query Experience | Connector Experience, Relational Semantics, and Remote Runtime in Collaboration, then X-as-a-Service under the exit conditions above | RFC 0007 Accepted and the approved product goal activated through `docs/PRODUCT_DELIVERY.md` |

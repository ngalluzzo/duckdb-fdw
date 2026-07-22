# RFC 0019: Add short-page-terminated REST pagination to duckdb_api/v1

```yaml
rfc: "0019"
title: "Add short-page-terminated REST pagination to duckdb_api/v1"
status: "Accepted"
rfc_type: "Product"
sponsor_team: "Connector Experience"
technical_decision_owner: "Lead agent"
product_approver: "Nic Galluzzo"
authors:
  - "Lead agent"
required_reviewers:
  - "Connector Experience"
  - "Query Experience"
  - "Remote Runtime"
  - "Relational Semantics"
  - "Engineering Enablement"
affected_teams:
  - "Connector Experience"
  - "Query Experience"
  - "Remote Runtime"
  - "Relational Semantics"
  - "Engineering Enablement"
linked_outcome_or_objective: "docs/goals/short-page-pagination.md: the third proactive capability-gap closure ahead of ROADMAP.md's 1.0.0 ten-provider gate."
supersedes: "Not applicable"
```

## Summary

Adds a third REST pagination strategy, `short_page`, to
`duckdb_api/v1`'s closed pagination set alongside `disabled`, `link_next`,
and `response_next`. Unlike the other two paginated strategies, `short_page`
requires no explicit continuation signal from the server at all: Runtime
requests successive pages using the same author-declared page-number/
page-size query bindings `link_next` already models, and infers exhaustion
purely from the just-decoded page containing fewer rows than the declared
page size (or zero rows), bounded as always by `max_pages_per_scan`. This
covers plain `?page=N&page_size=M` / `?offset=N&limit=M` REST APIs that
signal "no more pages" only by returning a short or empty page — a shape
neither `link_next` (requires a `Link` header) nor `response_next` (requires
a body-embedded next-page URL) can represent today.

## Sponsorship and context

- **RFC type:** Product. The decision changes `duckdb_api/v1`'s closed
  connector-package pagination contract, a public, author-facing surface.
- **Sponsoring team:** Connector Experience, which owns
  `docs/CONNECTOR_SPECIFICATIONS.md` and the package schema/compiler this
  decision extends.
- **Linked outcome or objective:** `docs/goals/short-page-pagination.md` —
  the third proactive capability-gap closure ahead of `ROADMAP.md`'s 1.0.0
  gate requiring 10 connector providers (2 exist today: GitHub, Rick and
  Morty). A fresh architecture-maturity re-assessment, run after RFC 0018's
  `api_key` credential shipped, ranked this the highest-leverage remaining
  gap among the candidates considered (basic auth, non-JSON response
  bodies, retry/backoff): it is explicitly pre-named in
  `release/1.0.0/freeze.json`'s exclusions
  (`pagination_body_url_offset_or_cursor_in_body_strategies`), and judged
  the pagination shape most likely to block connector providers #3–#10
  among free/hobby-tier REST APIs.
- **Why now:** Deciding now — before a third or later connector provider is
  authored against this exact shape — lets the project include it
  deliberately rather than discover the gap mid-authorship the way RFC
  0016's `response_next` gap was discovered while authoring
  `connectors/rickandmorty`.

## Problem

`docs/CONNECTOR_SPECIFICATIONS.md`'s REST pagination grammar accepts exactly
three strategies today: `disabled`, `link_next` (continuation signaled by an
HTTP `Link: rel=next` response header), and `response_next` (continuation
signaled by a declared JSON path to a next-page URL in the response body,
RFC 0016). Both paginated strategies require the server to actively signal
"here is the next page" or "there is no next page" on every response. A
large share of real, especially free/hobby-tier, REST APIs instead use plain
numeric pagination with **no explicit continuation signal at all** — the
client just keeps requesting `?page=N` (or `?offset=N&limit=M`) until a
response comes back with fewer records than requested, or empty. Examples of
this shape are common among lightweight public APIs that were not built with
HATEOAS-style pagination in mind.

A connector author targeting such an API cannot declare either paginated
strategy today: there is no `Link` header to require, and there is no
next-page URL anywhere in the body to extract a JSON path from — some such
APIs return no continuation metadata whatsoever, only the page's data array
(or a page whose only observable size signal is its own row count). The
author's only option today is `strategy: disabled`, which caps the relation
at exactly one page and silently truncates any dataset larger than one
page — not a diagnostic-quality limitation but a silent one, since
`disabled` is a fully legitimate, intentional choice for genuinely
unpaginated APIs and gives no signal that a *different* API merely lacks a
signal `duckdb_api/v1` knows how to use.

This is not a defect: RFC 0013 scoped pagination to what GitHub actually
needed (`link_next`), and RFC 0016 extended it to what Rick and Morty
actually needed (`response_next`). `release/1.0.0/freeze.json`'s
`pagination_body_url_offset_or_cursor_in_body_strategies` exclusion already
names this class of gap as a deliberate, pre-recorded omission awaiting a
later accepted RFC — this is that RFC, or the decision not to grant one yet.

## Decision drivers and invariants

- **Must preserve:** every invariant `link_next`/`response_next` currently
  enforce — sequential, bounded, cancelable pagination; no ordering,
  snapshot, parallelism, resume, deduplication, retry, or cache guarantee; a
  reconstructed continuation request stays on the exact operation origin and
  path; resource ceilings intersect host limits; RFC 0009/0012/0013/0014's
  accepted interfaces.
- **Must enable:** representing a real, common REST pagination shape (no
  explicit continuation signal, termination inferred from page size) without
  granting the package any new network authority beyond what `link_next`
  already grants — Runtime still only ever requests the exact,
  locally-computed next page URL; nothing server-supplied is ever
  dereferenced as a literal fetch target (there is, in fact, less trust
  surface here than `link_next`/`response_next`, since there is no external
  signal to validate at all).
- **Must not introduce:** a capability that can loop indefinitely if a
  server never returns a short page (`max_pages_per_scan` must remain a hard
  backstop, not merely an expected common case); a declared total-count- or
  has-more-field termination signal (explicitly out of scope for this RFC —
  see Alternatives — reserved for a follow-on if a real connector author
  needs it, per the product manager's 2026-07-22 scope decision recorded in
  `docs/goals/short-page-pagination.md`).

## Proposed decision

### The key architectural fact this proposal rests on

`link_next` and `response_next` already reconstruct every continuation
request locally from the relation's declared `page_number_parameter`,
`first_page`, and `page_increment` (`src/connector/pagination_declaration.cpp`);
the server-supplied signal (`Link` header or body URL) is only ever used to
decide *whether* to issue that already-fully-determined next request, never
to determine its shape. `short_page` keeps the identical reconstruction
model and removes the external signal entirely: after Runtime decodes a
page's rows (already required to extract relation rows on every strategy),
it compares the decoded row count against the relation's declared
`page_size` (now a **required** field for this strategy, unlike its optional
status under RFC 0017 for `link_next`/`response_next` — termination is
undefined without it). If the count is less than `page_size`, or exactly
zero, pagination is exhausted; otherwise Runtime requests
`page_number + page_increment` next, up to `max_pages_per_scan`. This
requires no new schema field beyond what `link_next` already has, and,
unlike `response_next`, requires no target-validation/reconstruct-and-verify
step at all, since there is no external string to validate — the design is
strictly narrower and simpler than `response_next`'s.

`page_increment` already covers both common real-world idioms without any
new field: an author sets `page_increment: 1` for page-number APIs
(`?page=1,2,3...`) or `page_increment` equal to `page_size` for true
offset/limit APIs (`?offset=0,20,40...`), exactly as `link_next` already
allows today.

### Public behavior

Adds one new accepted value, `short_page`, to `pagination.strategy` in
`docs/CONNECTOR_SPECIFICATIONS.md`'s REST operations grammar. Its required
field set mirrors `link_next`'s exactly (`dependency`, `consistency`,
`target_scope`, `page_size_parameter`, `page_size`, `page_number_parameter`,
`first_page`, `page_increment`, `max_pages_per_scan`), with one difference:
`page_size_parameter`/`page_size` are **required**, not optional, since the
termination rule depends on comparing against a known page size. No existing
accepted package, including `connectors/github` and `connectors/rickandmorty`
as currently authored, changes behavior — this is an addition to the closed
set, not a change to `disabled`, `link_next`, or `response_next`. `EXPLAIN`
output gains `short_page` as a possible `pagination` fact value alongside the
existing three.

### Shared interfaces

- **Connector Experience:** `connector-package-v1.schema.json` gains a new
  closed sibling shape (`shortPagePagination`, matching the existing
  `disabledPagination`/`linkPagination`/`responsePagination` `$defs`-per-
  `oneOf`-branch idiom), identical to `linkPagination`'s required-field set
  except `page_size_parameter`/`page_size` move from optional to required.
  `CompiledPaginationStrategy` (`src/include/duckdb_api/
  compiled_protocol_operation.hpp:378`) gains `SHORT_PAGE`. **Correction
  from Connector Experience review:** `short_page`'s required-field set
  (identical to `LINK_HEADER`'s, just with `page_size` required) means a
  constructor "mirroring the `LINK_HEADER` one" would have the exact same
  parameter-type list as the existing `LINK_HEADER` constructor
  (`std::string, std::uint64_t, std::string, std::uint64_t, std::uint64_t,
  std::uint64_t` — `compiled_protocol_operation.hpp`/`pagination_declaration.cpp:79-96`),
  which C++ cannot overload on. `CompiledPagination` instead gains a named
  static factory, `CompiledPagination::ShortPage(...)`, following the
  disambiguation pattern `CompiledPagination::Disabled()` already
  establishes for the zero-arg case, backed by a private tagged constructor
  or an internal delegating helper — not a fourth public overload.
  `ValidatePagination`'s capability-profile and typed-binding checks
  (`src/connector/pagination_declaration.cpp:216-247`) extend their
  strategy-membership tests to include `SHORT_PAGE`. Implementation should
  also fix `docs/CONNECTOR_SPECIFICATIONS.md:385-400`'s pre-existing
  staleness (it still describes `response_next` as "pending 0.10.0
  implementation" although 0.10.0 has shipped) while touching this section,
  flagged by Connector Experience review as a drive-by fix, not new scope.
- **Relational Semantics:** genuinely affected, by direct analogy to RFC
  0016's correction. Three exhaustive, fail-closed switches over pagination
  strategy throw an uncaught `std::logic_error` on an unhandled value and
  each need a `SHORT_PAGE` arm: `PlanBaseDomain`
  (`src/semantics/scan_planner_internal.hpp:106-131`), `ValidatePagination`
  (`src/semantics/scan_planner_validation.cpp:104-196`), and
  `PaginationStrategyName` (`src/semantics/scan_plan_explain.cpp:221-233`).
  **Resolved by Relational Semantics review:** `short_page` groups into
  `PlanBaseDomain`'s existing `LINK_HEADER`/`RESPONSE_NEXT_URL` branch, not
  a new one. That branch's condition never inspects which termination
  signal is used — the domain shape is fully determined by
  `response_source` alone (per `scan_plan.hpp:57-58`'s documented
  duplicate-preserving-bag semantics), and `link_next`/`response_next`
  already prove two distinct termination mechanisms share one classification.
  Giving `short_page` its own `BaseDomain` branch would encode pagination
  mechanics into relational classification, which is out of this team's
  scope per its charter. Implementation extends the existing `||` condition
  to include `SHORT_PAGE`.
- **Remote Runtime:** the primary implementer. `PlannedPaginationStrategy`
  (`src/include/duckdb_api/scan_plan.hpp:266`) gains `SHORT_PAGE`. The
  pagination-state machine needs a new entry point that advances
  unconditionally by `page_increment` and reports exhaustion from a decoded
  row count rather than a validated target string — `LinkPaginationState`
  (`src/include/duckdb_api/internal/runtime/pagination/
  link_pagination.hpp`) already separates `Advance` (header-sourced) from
  `AdvanceBody` (body-sourced); this RFC adds a third entry point,
  `AdvanceByCount(std::size_t decoded_row_count)`, on the same state object.
  **Resolved by Remote Runtime review:** this reuses
  `LinkPaginationState`'s existing `current_page`/`seen_pages`/`exhausted`/
  `failed` bookkeeping cleanly — none of those fields store a validated
  target string (that validation is a local computation inside
  `ValidateNextTarget`, called only from `Advance`/`AdvanceBody`), so no
  parallel state class is needed. The call site in
  `src/runtime/execution/http_paginated_scan.cpp` (the strategy-dispatch
  branch at lines 294-296, which already chooses `AdvanceBody` vs. `Advance`)
  gains a third branch calling `AdvanceByCount(page.rows.size())`, where
  `page` is the `JsonDecodedPage` returned by `DecodeJsonPage(...)` at line
  286-289 — available immediately after decode, well before the dispatch —
  so no new decode pass or resource cost is introduced.
- **Query Experience:** by direct analogy to RFC 0016's correction,
  `PaginationStrategyName` (`src/query/duckdb/scan_plan_explanation.cpp:199-211`,
  throwing `InternalException` at line 210) is a second exhaustive switch,
  distinct from and downstream of Semantics' own copy, and needs its own
  `short_page`/`SHORT_PAGE` arm. **Correction from Query Experience
  review:** `AddPaginationFacts` (lines 259-296) is not itself a switch — an
  if/else chain whose final `else` assumes `GRAPHQL_CURSOR` and calls the
  unguarded `pagination.GraphqlCursor()` accessor; it only fails closed
  today because it calls the exhaustive `PaginationStrategyName` first.
  Implementation must add an explicit `short_page` branch to
  `AddPaginationFacts` as well, not rely on the generic fallthrough.

### Operational behavior

No new resource, cancellation, or backpressure model — `max_pages_per_scan`
and the existing per-page/per-scan byte and record ceilings apply unchanged
and remain the sole backstop against a server that never returns a short
page (a legitimate but resource-bounded outcome, not a hang). No new
credential or network-policy authority: every request this strategy issues
is fully determined by the relation's own declared, locally-computed page
sequence before the response is even received — there is no server-supplied
value of any kind involved in deciding the next request's shape, only
whether to issue it at all.

## Topology impact

| Team | Role in this RFC | Interface or charter impact | Interaction mode | Exit condition |
| --- | --- | --- | --- | --- |
| Connector Experience | Sponsor and schema/compiler owner | New `short_page` schema branch, `CompiledPaginationStrategy::SHORT_PAGE`, a named `CompiledPagination::ShortPage(...)` static factory requiring (not permitting-optional) `page_size_parameter`/`page_size`; no charter text change | X-as-a-Service (existing) | Package authors can declare `short_page` and get the same diagnostic quality as `link_next` |
| Remote Runtime | Primary implementer | New `AdvanceByCount` entry point (third method) on the existing pagination-state object, confirmed a clean reuse by review; a new branch in the paginated-scan executor reusing the already-computed decoded row count; no new resource cost or decode pass; no charter text change | X-as-a-Service (reuse confirmed clean during RFC review, no collaboration phase needed) | The count-terminated fixture-coverage oracle (first/multi-page, short-page termination, empty-page termination, `max_pages_per_scan` exhaustion) passes without a second decode pass or a new resource-budget category |
| Relational Semantics | Required implementation participant, not a passive reviewer | `PlanBaseDomain`, `ValidatePagination`, and `PaginationStrategyName` (`src/semantics/scan_plan_explain.cpp`) each require a `SHORT_PAGE` arm; base occurrence domain confirmed during review to group with `link_next`/`response_next`'s existing branch | X-as-a-Service (classification decided during RFC review) | A property test proves `short_page`'s `BaseDomain` classification behaves consistently under the same fixture rows used for `link_next`/`response_next` |
| Query Experience | Required implementation participant | `PaginationStrategyName` and `AddPaginationFacts` (`src/query/duckdb/scan_plan_explanation.cpp`) both require an explicit `short_page` arm/branch; `EXPLAIN`'s `"Pagination Strategy"` fact gains a fourth accepted value | X-as-a-Service (existing) | A concrete test, in the shape of `graphql_adapter_contract_tests.cpp`'s existing real-`EXPLAIN` assertion, asserts the literal `"short_page"` string in actual `EXPLAIN` output |
| Engineering Enablement | Facilitator | Helps establish the fixture-coverage variant set, distinguishing termination-on-short-page from termination-on-empty-page as separate categories (the two are not interchangeable: a page with `0 < count < page_size` versus `count == 0` may expose different decoder edge cases) | Facilitation | Connector Experience, Remote Runtime, Relational Semantics, and Query Experience maintain the corrected oracle independently |

Cognitive load is distributed across four teams, each extending one existing
exhaustive switch or state object it already owns, by direct analogy to how
RFC 0016 added `response_next` — not a new pattern for any of them.

## Correctness, security, and lifecycle analysis

- **Relational semantics and conservative fallback:** Affected. Pagination
  strategy carries no relational authority (no predicate, ordering, or limit
  change) directly, but `PlanBaseDomain`'s classification of `short_page`'s
  base occurrence domain is a real decision Relational Semantics must make
  deliberately, exactly as RFC 0016 required for `response_next`. Row
  cardinality across pages remains bounded by `max_records_per_scan`
  regardless of strategy once that classification is made correctly.
- **Authentication, credentials, network policy, and privacy:** Not
  affected. No credential handling changes; the strategy introduces no new
  authority since every request it issues is fully locally determined before
  any response is received — there is, in fact, no server-supplied signal
  of any kind to validate, which is a strictly narrower trust surface than
  `link_next`/`response_next`'s reconstruct-and-verify model.
- **Resource budgets, backpressure, and cancellation:** `max_pages_per_scan`
  and existing per-page/per-scan ceilings and cancellation checkpoints apply
  unchanged. `max_pages_per_scan` is the sole backstop against a
  never-short-page server; this must be verified as an explicit fixture
  case (`max_pages_exhausted` without a short page ever occurring), not
  merely assumed to already be covered by the existing ceiling
  implementation.
- **Replay, retries, caching, and duplicate prevention:** The existing
  `seen_pages` replay-rejection bookkeeping in the pagination-state object
  is reused verbatim; there is no external target string for this strategy
  to replay-check against, since the page sequence is entirely
  self-determined.
- **Concurrency, immutability, and state ownership:** No change; pagination
  state ownership and `max_concurrent_pages` semantics are identical.
- **FFI, initialization, reload, shutdown, and failure containment:** Not
  affected.
- **Diagnostics, redaction, metrics, and progress reporting:** Reuses
  existing diagnostic codes for the new compile-time field-requirement
  checks (mirroring `link_next`'s); no new redaction concern, since no
  server-supplied value is ever retained or compared.

## Compatibility and migration

Additive only. No existing accepted package (`connectors/github`,
`connectors/rickandmorty`) changes behavior or requires a version bump.
Whether either existing package's relations could adopt `short_page` instead
of their current strategy is a separate, later decision for that package's
own version bump under RFC 0013's compatibility table — not decided here.

## Evidence and bounded trials

| Question or claim | Evidence required | Method or fixture | Result and limitations |
| --- | --- | --- | --- |
| The termination mechanism (compare decoded row count against declared page size) is available without a new decode pass | Direct code inspection, corrected during Remote Runtime review | Read `src/runtime/execution/http_paginated_scan.cpp:286-289` (`DecodeJsonPage` returns `page`, a `JsonDecodedPage` whose `rows` field is populated) and `:294-296` (the strategy-dispatch branch) | Confirmed: `page.rows.size()` is available immediately after decode, at line 289, strictly before the dispatch branch at 294-296 that calls `Advance`/`AdvanceBody` today; a `SHORT_PAGE` branch can reuse it directly with no new parse or resource cost. (The RFC's original citation of lines 234-244 was imprecise — that range is inside `ProduceBatch`, a later, separate call — corrected here.) |
| `page_increment` already models both page-number and offset/limit idioms without a new field | Direct code inspection | Read `src/connector/pagination_declaration.cpp:79-96` (the `LINK_HEADER` constructor) | Confirmed: `page_increment` is a free-standing positive-integer field independent of `page_size`; an author sets it to `1` for page-number APIs or equal to `page_size` for offset/limit APIs, exactly as `link_next` already permits today. No new schema field is needed to cover both idioms. |
| No real package today needs a declared total-count/has-more termination signal | Absence of counter-evidence; PM scope decision | Repository inspection (`connectors/github`, `connectors/rickandmorty` are the only two real packages) plus explicit product-manager decision recorded 2026-07-22 in `docs/goals/short-page-pagination.md` | No pending decision-critical gap for this RFC's scope; the product manager explicitly deferred total-count/has-more support to a future RFC rather than including it here. |

## Alternatives considered

1. **Add `short_page`, scoped to page-size-comparison termination only
   (proposed).** Benefit: represents a real, evidenced gap with strictly
   less trust surface than `link_next`/`response_next` (no external signal
   to validate at all); reuses the existing pagination-state object and
   decode pass with no new resource cost. Drawback: still an addition to the
   closed pagination contract, requiring cross-team exhaustive-switch
   updates before it can ship.
2. **Also support a declared total-count or has-more response field as an
   alternative termination signal in this same RFC.** Benefit: covers a
   broader class of real-world APIs that expose an explicit exhaustion
   signal alongside numeric pagination. Drawback: roughly doubles this RFC's
   scope (a second termination code path, its own fixture-coverage set, its
   own exhaustive-switch arms) before any real connector author has asked
   for it. The product manager explicitly deferred this to a follow-on RFC
   (see Evidence and bounded trials).
3. **Add a general opaque-cursor-in-body strategy instead** (extract a
   value from the body, use it directly to build the next request, mirroring
   GraphQL's existing cursor model, and matching what
   `pagination_body_url_offset_or_cursor_in_body_strategies` also names).
   Benefit: covers Stripe/Shopify-style cursor pagination. Drawback: RFC
   0016 already considered and deliberately excluded this — it has a
   materially different, less conservative trust flow (extract-and-use
   rather than reconstruct-and-verify) deserving its own dedicated safety
   analysis. Not selected here either; still out of scope, unaffected by
   this RFC's acceptance either way.
4. **Retain current behavior (no new strategy).** Rejected: leaves a
   pre-named, evidenced gap undocumented rather than deciding it, and
   `disabled` pagination's silent one-page truncation for an API that
   actually has more pages is a correctness trap for connector authors, not
   a neutral default.

## Drawbacks and failure modes

- Termination-on-short-page and termination-on-empty-page are related but
  distinct fixture categories (`0 < count < page_size` versus `count == 0`);
  treating them as one case in the oracle would under-test a real boundary
  a server could hit either way.
- If a server's last page happens to return exactly `page_size` rows (an
  exact multiple of the page size), one extra request is made that returns
  zero rows before the strategy recognizes exhaustion — this is expected,
  bounded behavior (identical to how `link_next`-style pagination already
  behaves when a server's `Link` header briefly persists past the true last
  page), not a defect, but should be an explicit fixture case
  (`exact_multiple_page_boundary`) so it is proven rather than assumed.
- Omitting Relational Semantics' or Query Experience's arm is not a
  cosmetic gap — both switches are exhaustive and throw an uncaught,
  crash-class exception on an unhandled enum value today, so a compiled
  generation using `short_page` that reaches either unextended switch fails
  closed as a crash, not as a typed diagnostic, until both are updated.
- `short_page` trades away a correctness signal `link_next`/`response_next`
  have: their reconstruct-and-verify model detects server-side pagination
  *drift* (an off-sequence Link header or body URL) as a typed `POLICY`
  failure. `short_page` has no equivalent cross-check — there is no
  external signal to compare against — so a server that silently changes
  its page ordering underneath a scan cannot be distinguished from normal
  operation. This is an accepted, disclosed tradeoff for the reduced trust
  surface (flagged by Remote Runtime review), not a defect, but connector
  authors should understand it is a real difference from the other two
  strategies.

## Acceptance and verification

- **End-to-end demonstration:** A new fixture relation declares `short_page`
  pagination against a recorded multi-page transcript (including a
  short-terminal page and, separately, an empty-terminal page) and returns
  all rows across pages through the existing `duckdb_api_load_connector`
  path.
- **Automated oracle:** fixture-coverage variants: `first_page`,
  `multi_page`, `termination_on_short_page`, `termination_on_empty_page`,
  `exact_multiple_page_boundary`, `max_pages_exhausted` (verified with a
  transcript that never returns a short page), plus schema/compiler
  rejection variants (missing required field, non-positive page-size/page-
  increment/first-page, `page_size_parameter`/`page_number_parameter`
  collision). A Relational Semantics property test proving `short_page`'s
  `BaseDomain` classification is a deliberate choice, consistent under the
  same fixture rows used for `link_next`/`response_next`. **Corrected by
  Query Experience review:** a vague "Query-owned differential test" is
  insufficient — RFC 0016 used identical phrasing for `response_next` and
  it was never delivered (only `"graphql_cursor"` is asserted against real
  `EXPLAIN` output anywhere in the repository today, in
  `test/cpp/query/duckdb/graphql_adapter_contract_tests.cpp`; `"disabled"`,
  `"link_header"`, and `"response_next"` are untested against actual
  `EXPLAIN` text). This RFC instead requires a concrete test, in the same
  shape as `graphql_adapter_contract_tests.cpp`'s existing real-`EXPLAIN`
  assertion, that runs an `EXPLAIN` query against a `short_page`-paginated
  relation and asserts the literal `"short_page"` string in its output —
  not merely a call to an internal `PaginationStrategyName`/
  `AddPaginationFacts` function.
- **Quality gates:** `make build`, `make test`, `make demo`; the existing
  package/fixture/coverage gates extended to the new strategy;
  `scripts/verify-source-identities.py`, `scripts/verify-public-surface-inventory.py`,
  `scripts/verify-contract-freeze.py` and their tests.
- **Independent review:** `$topology-consult` review from all five required
  reviewers, recorded below; `$adversarial-review` during implementation
  given this touches an exhaustive-switch surface and network-request
  construction, per `AGENTS.md`.
- **Interaction exit:** Remote Runtime's `AdvanceByCount` entry point passes
  the full termination/boundary fixture set without Connector Experience
  needing pagination-state-internal knowledge.

## Contract propagation

| Source of truth or artifact | Impact | Required update | Completion evidence |
| --- | --- | --- | --- |
| `docs/ARCHITECTURE.md` | Not affected | No architectural-invariant change | Not applicable |
| `docs/CONNECTOR_SPECIFICATIONS.md` | Affected | Add `short_page` to the REST pagination grammar, its required-field set, and the termination rule | Pending implementation |
| `docs/RUNTIME_CONTRACTS.md` | Affected | Document `AdvanceByCount`'s termination rule and its resource-neutral reuse of the existing decode pass | Pending implementation |
| `docs/TEAM_TOPOLOGY.md` and active charters | Not affected | No interface or accountability boundary moves | Not applicable |
| `docs/PRODUCT_DELIVERY.md`, `AGENTS.md`, and skills | Not affected | No change | Not applicable |
| `ROADMAP.md` | Affected | Record the new pagination strategy under the appropriate upcoming minor version | Pending implementation |
| `release/1.0.0/freeze.json` and `release/1.0.0/freeze.md` | Affected | **Corrected by Engineering Enablement review, then delivered directly:** `pagination_body_url_offset_or_cursor_in_body_strategies` is a `MANDATORY_EXCLUSIONS` entry enforced by `scripts/contract_freeze.py`'s `test_mandatory_exclusion_removed_fails` — its *key* can never be removed or renamed; only its reason prose was narrowed. Since implementation landed in the same change that accepted this RFC (following RFC 0018's precedent, not RFC 0016's original candidate-then-implement-later sequencing), `short_page` graduated directly into the schema-closed `pagination_strategies.rest` set with no `accepted_candidate_revisions` interval | Completed: `pagination_strategies.rest` is `{disabled, link_next, response_next, short_page}`; `test/python/contract_freeze_tests.py`'s hardcoded schema-closed-set expectations updated to match |
| Examples, diagnostics, fixtures, and tests | Affected | New fixture-coverage variant set, `BaseDomain` property test, `EXPLAIN` differential test | Pending implementation |

## Unresolved questions

- Non-blocking: exact strategy name (`short_page` vs. an alternative
  spelling such as `count_terminated` or `page_count`) — cosmetic, resolved
  at implementation time, following RFC 0016's precedent for `next_url_path`
  naming.
- Non-blocking: whether `connectors/rickandmorty`'s existing relations could
  be migrated to `short_page` — a separate, later decision, not required for
  this RFC's acceptance.
- Both decision-critical questions originally reserved to Relational
  Semantics (`PlanBaseDomain` classification) and Remote Runtime
  (`AdvanceByCount` reuse) were resolved during review — see Shared
  interfaces above. No decision-critical question remains open.

## Review record

| Required reviewer | Team | Result | Evidence or objection | Disposition by decision owner |
| --- | --- | --- | --- | --- |
| Connector Experience perspective | Connector Experience | Objected (Unsound) | Verified the schema/`oneOf` extension is clean, and the RFC 0017 optional-vs-required page-size asymmetry is correctly reasoned (`pagination_declaration.cpp:85-95`, `:233`). Found a real compile-blocking defect: a fourth `CompiledPagination` constructor "mirroring `LINK_HEADER`" would have an identical parameter-type list to the existing `LINK_HEADER` constructor — a duplicate-declaration error, not a style issue. Also flagged pre-existing staleness in `docs/CONNECTOR_SPECIFICATIONS.md:385-400` (still describes `response_next` as pending 0.10.0, which has shipped). | Accepted; RFC's Shared interfaces section revised to replace the fourth-constructor design with a named static factory (`CompiledPagination::ShortPage(...)`), following the existing `Disabled()` precedent. The `CONNECTOR_SPECIFICATIONS.md` staleness is folded in as a drive-by fix during implementation, not new scope. |
| Query Experience perspective | Query Experience | Objected (Needs evidence) | Confirmed `PaginationStrategyName` (`scan_plan_explanation.cpp:199-211`) is a second exhaustive switch needing a `SHORT_PAGE` arm, and found `AddPaginationFacts` (lines 259-296) is an if/else chain whose unguarded final `else` assumes `GRAPHQL_CURSOR` — needs its own explicit `short_page` branch, not implicit fallthrough. Central objection: verified by direct test-file inspection that RFC 0016's identical "Query-owned differential test" acceptance criterion for `response_next` was never actually delivered — only `"graphql_cursor"` is asserted against real `EXPLAIN` output anywhere in the repository (`graphql_adapter_contract_tests.cpp`); `"disabled"`/`"link_header"`/`"response_next"` are untested against actual `EXPLAIN` text. Repeating the same vague phrasing would repeat the gap. | Accepted; RFC's Shared interfaces section revised to require an explicit `AddPaginationFacts` branch, and Acceptance and verification revised to require a concrete, named-shape test (mirroring `graphql_adapter_contract_tests.cpp`'s real-`EXPLAIN` assertion) rather than an unspecified differential test. |
| Remote Runtime perspective | Remote Runtime | Approved | Confirmed the termination mechanism needs no new decode pass, with a citation correction: the decoded row count is available at `http_paginated_scan.cpp:286-289` (right after `DecodeJsonPage`), not the originally cited `234-244` (which is inside a later, separate `ProduceBatch` call). Resolved the reserved `AdvanceByCount` reuse question in favor of a third method on the existing `LinkPaginationState` class — none of its bookkeeping fields store a validated target string, so no parallel state class is needed. Confirmed `max_pages_per_scan` is enforced unconditionally in `scan_resource_accounting.cpp:90-91` regardless of strategy. Confirmed the reduced-trust-surface claim, with one non-blocking nuance: `short_page` loses the pagination-drift cross-check `link_next`/`response_next`'s reconstruct-and-verify model provides. | Accepted; RFC's Shared interfaces section revised with the corrected citation and the resolved `AdvanceByCount`-reuse design; Drawbacks section revised to disclose the lost pagination-drift cross-check as an accepted, explicit tradeoff. |
| Relational Semantics perspective | Relational Semantics | Approved | Confirmed `PlannedPaginationStrategy`/`CompiledPaginationStrategy` are closed enums and all three switches are exhaustive/fail-closed, with corrected line ranges (`ValidatePagination` is `scan_planner_validation.cpp:104-196`, not `108-247` as originally cited). Resolved the reserved `PlanBaseDomain` classification question directly from the function's logic: it groups by `response_source` alone, never by termination-signal mechanism, so `short_page` belongs in the existing `LINK_HEADER`/`RESPONSE_NEXT_URL` branch, not a new one — giving it a separate branch would encode pagination mechanics into relational classification, outside this team's charter scope. | Accepted; RFC's Shared interfaces section revised with the corrected citation and the resolved `BaseDomain`-grouping decision, stated as a design fact rather than an open question. |
| Engineering Enablement perspective | Engineering Enablement | Objected (Needs evidence) | Verified the fixture-coverage variant set against `fixture-coverage-v1.json`'s existing `link_next`/`response_next` sets and confirmed it is complete and correctly scoped (correctly omits every target-validation variant, since there is no external signal to validate; confirmed `termination_on_short_page`/`termination_on_empty_page` are genuinely distinct decoder-exercising cases, and `exact_multiple_page_boundary` is a real, distinct case). Central objection: `pagination_body_url_offset_or_cursor_in_body_strategies` is a `MANDATORY_EXCLUSIONS` entry whose key `scripts/contract_freeze.py` forbids ever removing or renaming; the RFC's proposed "narrow the exclusion" directly conflicts with that invariant. | Accepted; RFC's Contract propagation section revised to leave the exclusion's key untouched (only its reason prose narrowed), and to graduate `short_page` directly into `pagination_strategies.rest` since implementation landed in the same change as acceptance (RFC 0018's precedent, not RFC 0016's original sequencing) — no `accepted_candidate_revisions` entry was needed. |

All five required reviewers returned a disposition. Two approved outright
(Relational Semantics, Remote Runtime) after resolving the two questions
this RFC had deliberately left open rather than deciding unilaterally. Three
raised objections that materially corrected the RFC's technical content
(a compile-blocking constructor design, an unenforceable acceptance
criterion repeating an RFC 0016 gap, and a freeze-artifact mechanism that
would have violated a mandatory-exclusion invariant) — all three were
dispositioned by directly revising the RFC's text, matching the precedent
RFC 0016's and RFC 0018's reviews set. No reviewer objected to the
underlying design (a page-size-comparison termination strategy reusing the
existing reconstruction model) as unsafe or contract-violating.

## Decision and rationale

- **Technical decision owner:** Lead agent.
- **Product approval:** Received — Nic Galluzzo, 2026-07-22. The one
  reserved product decision this RFC depends on (termination-signal scope:
  short-page-only in this first cut, not also a declared total-count/
  has-more field) was resolved by the product manager on 2026-07-22,
  recorded in `docs/goals/short-page-pagination.md`'s Reserved product
  decisions section.
- **Decision:** **Accepted.** `short_page` is added to `duckdb_api/v1`'s
  closed REST pagination set, with the corrected design from the review
  record above: a named static factory (not a fourth overloaded
  constructor) for the compiled IR, `PlanBaseDomain` grouped with the
  existing `LINK_HEADER`/`RESPONSE_NEXT_URL` branch, `AdvanceByCount` as a
  third method on `LinkPaginationState`, a concrete real-`EXPLAIN` test
  requirement (not a vague differential-test phrase), and direct
  graduation into `pagination_strategies.rest` with the exclusion's key
  left untouched (not a mandatory-exclusion key edit, and no
  `accepted_candidate_revisions` interval, since implementation landed in
  the same change as acceptance).
- **Rationale:** The core design — reusing `link_next`'s existing page-
  reconstruction model and removing the external continuation signal
  entirely, inferring exhaustion from a decoded row count already computed
  by the existing decode pass — is sound and unobjected-to by any reviewer.
  Every objection raised was about implementation-plan completeness or
  correctness (a compile error, an unenforceable acceptance criterion, a
  freeze-artifact mechanism conflicting with a mandatory invariant), each
  independently verified against source and each directly correctable in
  the RFC's text without weakening the design. This mirrors RFC 0016's and
  RFC 0018's pattern: substantive review sharpens the plan rather than
  blocking a sound decision.
- **Material objections:** All three objections (Connector Experience,
  Query Experience, Engineering Enablement) were dispositioned by directly
  revising the RFC's technical content — see the Review record table above
  for each disposition. None was rejected; none required returning to
  Draft, since none identified a decision-critical fact the RFC could not
  resolve from its own reviewers' direct evidence.
- **Superseded by:** Not applicable.

## Follow-on goals

| Outcome or objective | Accountable team | Supporting teams and interaction modes | Activation condition |
| --- | --- | --- | --- |
| Implement `short_page` pagination (schema, compiler, compiled IR, Relational Semantics switches and `BaseDomain` classification, Remote Runtime `AdvanceByCount` and executor wiring, Query `EXPLAIN` arm, corrected fixture-coverage set) | Connector Experience | Remote Runtime (Collaboration), Relational Semantics (Collaboration), Query Experience (X-as-a-Service), Engineering Enablement (Facilitation) | This RFC accepted; `docs/goals/short-page-pagination.md` activated |
| Add a declared total-count/has-more termination signal as an alternative to short-page inference | Connector Experience | Remote Runtime, Relational Semantics, Query Experience | Not activated by this RFC — explicitly deferred pending a real connector author's need |
| Add a general opaque-cursor-in-body REST pagination strategy | Connector Experience | Remote Runtime, Relational Semantics, Query Experience | Not activated by this RFC — remains out of scope pending a dedicated safety analysis, as RFC 0016 also recorded |

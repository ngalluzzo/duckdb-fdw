# Goal: Short-page-terminated REST pagination

## PM brief

### Outcome

For a connector author, enable declaring numeric page-number or offset-based
REST pagination — continuation inferred from the server returning fewer
records than the declared page size (or an empty page), rather than
requiring an explicit next-page signal (a `Link` header or a next-page URL in
the response body) — so packages targeting APIs that paginate this way can be
authored and loaded through the same v1 path as `link_next` and
`response_next`.

### Why now

This is the third proactive capability-gap closure ahead of `ROADMAP.md`'s
1.0.0 gate requiring 10 connector providers (2 exist today: GitHub, Rick and
Morty). A fresh architecture-maturity re-assessment, run after `api_key`
shipped, found this is the highest-leverage remaining gap:
`release/1.0.0/freeze.json`'s exclusions already pre-name it
(`pagination_body_url_offset_or_cursor_in_body_strategies`), and it is judged
the pagination shape most likely to block connector providers #3–#10 among
free/hobby-tier REST APIs — plain `?page=N`/`?offset=N&limit=M` with no
explicit continuation signal is a very common shape neither `link_next` nor
`response_next` can represent today.

### Product guardrails

- Must: preserve every existing pagination invariant (`disabled`, `link_next`,
  `response_next` behavior; `max_pages_per_scan` and resource-ceiling
  enforcement; sequential, bounded, cancelable execution) for the new
  strategy too.
- Must not: silently loop forever if a server never returns a short or empty
  page — `max_pages_per_scan` must still be the hard backstop.
- Preserve: the closed-schema/RFC-gated evolution model — this is a new
  `pagination.strategy` enum value, a durable public contract change needing
  its own RFC per `docs/RFC_PROCESS.md`, following the same shape RFC 0016
  (`response_next`) and RFC 0018 (`api_key`) used.

### Success signals

- A connector author can declare the new strategy with author-declared
  page-number (or offset) and page-size parameter names, and have it
  validate, compile, plan, explain, and execute exactly like `link_next` and
  `response_next` do today.
- Termination behaves correctly and safely: scanning stops when a page comes
  back with fewer records than the declared page size (or empty), and
  `max_pages_per_scan` still bounds worst-case behavior if a server never
  returns a short page.
- Existing bearer/api_key-authenticated, `link_next`, and `response_next`
  packages (GitHub, Rick and Morty) are unaffected — no compatibility break,
  no version bump for unrelated connectors.

### Reserved product decisions

- **Resolved (2026-07-22):** termination is inferred purely from "fewer
  records than the declared page size" (or an empty page) in this first cut.
  An optional declared total-count/has-more response field is explicitly out
  of scope for this goal and deferred to a follow-on RFC if a real connector
  author needs it.

## Agent commitment

### Observable interpretation

A connector author writing a manifest for an API that exposes plain
`?page=N&page_size=M` (or `?offset=N&limit=M`) pagination with no `Link`
header and no next-page URL in the body declares the new pagination strategy
with an author-declared page-number parameter name, an author-declared
page-size parameter name, a first-page/first-offset value, a page increment,
and a `max_pages_per_scan` ceiling — the same shape `link_next` already
requires, minus any header/body continuation-signal field. The relation
compiles, plans, `EXPLAIN`s, and executes: Remote Runtime requests
successive pages until a page returns fewer rows than the declared page size
(or zero rows) or `max_pages_per_scan` is reached, whichever comes first. An
author who omits a required field, supplies a non-positive page-size/page-
increment value, or lets `page_size_parameter` collide with
`page_number_parameter` gets the same field-precise diagnostic quality
`link_next` already gives.

### Acceptance evidence

- Demonstration: a new fixture relation (or an adopted `connectors/
  rickandmorty` relation, if its actual pagination shape fits — to be
  confirmed during implementation) declares the new strategy against a
  recorded multi-page transcript and returns all rows across pages through
  the existing `duckdb_api_load_connector` path.
- Automated oracle: fixture-coverage variants mirroring `link_next`'s
  (`first_page`, `multi_page`, `termination_on_short_page`,
  `termination_on_empty_page`, `max_pages_exhausted`, plus the schema/compiler
  rejection variants: missing field, non-positive page-size/increment,
  page-size/page-number parameter-name collision); a Relational Semantics
  property test proving the new strategy's `BaseDomain` classification
  matches `link_next`'s under the same fixture rows; a Query-owned
  differential `EXPLAIN` test.
- Quality gates: `make build`, `make test`, `make demo`; source-identity,
  public-surface-inventory, and contract-freeze scripts and their tests.
- Independent review: `$adversarial-review` with at least two perspectives
  (this touches network-policy-adjacent request construction and an
  exhaustive-switch surface across three teams), per `AGENTS.md`.

### Contract and invariant impact

- `docs/CONNECTOR_SPECIFICATIONS.md`'s REST pagination grammar and
  `docs/RUNTIME_CONTRACTS.md`'s pagination-execution description.
- `CompiledPaginationStrategy`, `PlannedPaginationStrategy`, and every
  exhaustive switch over them (`PlanBaseDomain`, `ValidatePagination`,
  `PaginationStrategyName` in both `src/semantics/scan_plan_explain.cpp` and
  `src/query/duckdb/scan_plan_explanation.cpp`) — each throws an uncaught
  `std::logic_error`/`InternalException` on an unhandled strategy today.
- Resource-ceiling and cancellation invariants (`max_pages_per_scan`,
  per-page/per-scan byte and record budgets) must apply unchanged.
- `release/1.0.0/freeze.json`'s `pagination_body_url_offset_or_cursor_in_body_strategies`
  exclusion, which must be narrowed or reworded once this graduates.

### Team and RFC routing

- Accountable stream: Connector Experience (a connector author declaring and
  loading a new pagination shape).
- Supporting interactions: Remote Runtime (Collaboration, primary
  implementer of the termination logic), Relational Semantics (Collaboration,
  `BaseDomain` classification decision), Query Experience (X-as-a-Service,
  `EXPLAIN` arm), Engineering Enablement (Facilitation, fixture-coverage
  completeness).
- RFC: required (new public connector-package contract, a shared
  `CompiledConnector`/`ScanPlan` interface change, and a pre-named
  `release/1.0.0/freeze.json` exclusion being narrowed). Drafted as RFC 0019;
  status tracked there.

### Unknowns and first trial

- Unknown: whether `connectors/rickandmorty`'s actual pagination could adopt
  this strategy instead of `disabled`/`response_next` — not required for
  acceptance, worth checking opportunistically during implementation.
- Unknown: the exact fixture-coverage variant set Engineering Enablement will
  require once it reviews the draft (RFC 0016's precedent shows this is
  routinely corrected during review).
- No trial needed before the RFC: the termination mechanism (compare decoded
  row count against the declared page size) is already directly observable
  in the existing pagination execution loop (`decoded.Rows().size()` in
  `src/runtime/execution/http_paginated_scan.cpp`), so feasibility is already
  established by direct source inspection, not a live unknown.

### Delivery path

1. RFC 0019 accepted (schema, compiled IR, and cross-team obligations
   decided).
2. Implement: schema branch, compiler decode/validation, compiled IR enum
   value, Relational Semantics switches and `BaseDomain` classification,
   Remote Runtime termination logic and pagination-state entry point, Query
   `EXPLAIN` arm, fixture coverage, freeze-artifact update.
3. Adversarial review, quality gates, commit.

## Governance

Follow docs/PRODUCT_DELIVERY.md. Pursue: a connector author can declare
numeric page-number/offset REST pagination terminated by a short or empty
page, with no explicit continuation signal required from the server.
Completion requires: the acceptance evidence in this goal's Agent commitment
section, RFC 0019's Acceptance and verification section, and independent
adversarial review given this touches an exhaustive-switch surface and
network-request construction.
Preserve: every `link_next`/`response_next` pagination invariant;
`max_pages_per_scan` as an unconditional backstop; AGENTS.md and the relevant
architecture/connector/runtime contracts.
Governance: Accountable stream is Connector Experience. RFC 0019 accepted
2026-07-22 (`docs/rfcs/0019-add-short-page-pagination.md`) — five-team
topology-consult review complete, three objections dispositioned by revising
the RFC's technical content, no reviewer objected to the underlying design.

## Completion record

### Delivered

`kind: short_page` (`strategy: short_page`) is implemented end to end for
`duckdb_api/v1`: a new closed schema shape (`shortPagePagination`, identical
field set to `linkPagination` except `page_size_parameter`/`page_size` are
required, not optional), the discriminated schema decoder branch,
`CompiledPaginationStrategy::SHORT_PAGE` with a named static factory
(`CompiledPagination::ShortPage`/`CompiledModelBuilder::ShortPagePagination`)
rather than a fourth overloaded constructor (the required-field set is
otherwise identical to `link_next`'s, which C++ cannot overload on),
`PlannedPaginationStrategy::SHORT_PAGE`, `PlanBaseDomain` grouped with the
existing `link_next`/`response_next` branch (domain depends only on response
source, never on termination mechanism), `ValidatePagination` and
`PaginationStrategyName` arms across both Relational Semantics and Query
Experience's independent copies, `LinkPaginationState::AdvanceByCount` — a
third entry point reusing the existing pagination-state object with no new
decode pass (the decoded row count was already computed for row production)
and no reconstruct-and-verify step at all (there is no external signal to
validate), wired into the paginated-scan executor's strategy dispatch, and a
generalized `ExpectedRestDomain`/`HasSupportedRestPagination` admission path.
A predicate mapping colliding with `short_page`'s page-size/page-number query
fields is now rejected — a pre-existing gap for `response_next` too, found
and fixed alongside this work since both strategies share the same
structural query bindings. `connectors/github` and `connectors/rickandmorty`
are unaffected (no version bump; full existing test suite green).

### Evidence

- `make build`/`make test` green, including 4 new end-to-end executor tests
  (short-page termination, empty-page termination, exact-multiple-page
  boundary, `max_pages_per_scan` exhaustion without a short page ever
  occurring), 4 new `LinkPaginationState::AdvanceByCount` unit tests, a
  Relational Semantics `BaseDomain`-equivalence property test, 4 new
  schema/compiler contract tests (valid compile, missing page size, name
  collision, non-positive increment), and a real-`EXPLAIN` test asserting
  the literal `short_page` string in actual DuckDB output (closing the gap
  RFC 0016 left for `response_next`, per Query Experience's RFC review
  finding).
- `scripts/verify-source-identities.py`, `scripts/verify-public-surface-inventory.py`
  + its test, `scripts/verify-contract-freeze.py` + its test all pass;
  `release/1.0.0/freeze.json`'s `pagination_strategies.rest` is
  `{disabled, link_next, response_next, short_page}`.
- `$topology-consult` RFC review (5 perspectives) and `$adversarial-review`
  both completed; findings dispositioned by revising the RFC/implementation,
  not by weakening any check.

### Material decisions and deviations

- **Direct graduation, no `accepted_candidate_revisions` interval.**
  Implementation landed in the same change that accepted RFC 0019, following
  RFC 0018's precedent (not RFC 0016's original candidate-then-implement-
  later sequencing). `short_page` was added straight to
  `pagination_strategies.rest`; the mandatory exclusion
  `pagination_body_url_offset_or_cursor_in_body_strategies` keeps its exact
  key (permanent per `scripts/contract_freeze.py`) with only its reason
  prose narrowed.
- **Bundled, adjacent bug fix.** `predicate_declaration.cpp`'s
  `ValidatePredicateMappings` only checked for a predicate colliding with
  pagination's page-size/page-number query fields when the strategy was
  `LINK_HEADER` — a pre-existing gap that also silently affected
  `response_next` (never checked either). Widened to cover all three
  paginated strategies, since `short_page` would otherwise inherit the same
  gap.
- **Termination scope resolved by product manager before drafting.**
  Short-page-only inference (no declared total-count/has-more field) was
  confirmed via `AskUserQuestion` on 2026-07-22 before the RFC was drafted,
  recorded in this goal's Reserved product decisions section above.

### Product options discovered

- A declared total-count/has-more termination signal, as an alternative to
  short-page inference, was explicitly deferred pending a real connector
  author's need (see RFC 0019's Follow-on goals).
- A general opaque-cursor-in-body REST pagination strategy remains out of
  scope, pending its own dedicated safety analysis (RFC 0016's and RFC
  0019's shared Alternatives finding).
- Whether `connectors/rickandmorty`'s existing relations could adopt
  `short_page` instead of their current strategy was not pursued — a
  separate, later decision, not required for this goal's acceptance.

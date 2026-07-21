# RFC 0016: Decide body-signaled REST pagination for duckdb_api/v1

```yaml
rfc: "0016"
title: "Decide body-signaled REST pagination for duckdb_api/v1"
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
linked_outcome_or_objective: "docs/goals/public-connector-authoring-candidate.md (0.9.0): resolve the pagination gap this goal's first trial surfaced before the 1.0.0 public contract is enumerated and frozen."
supersedes: "Not applicable"
```

## Revision history

- **2026-07-21 (initial):** Draft proposed the `response_next` design as a
  reconstruct-and-verify REST pagination strategy architecturally identical to
  `link_next` except for the continuation signal's source. Decision proposed:
  accept the design and defer implementation to a post-`1.0.0` `MINOR`, gated
  on a scoping spike (decoder single-pass-vs-second-parse and encoding-
  normalization rule). Five-team review produced four `Objected (Needs
  evidence)` rows, all about timing/completeness rather than design soundness;
  each was dispositioned by directly revising the RFC's technical content.
- **2026-07-21 (PM re-direction, this revision):** The product manager (Nic
  Galluzzo) re-prioritized `1.0.0`: it will not ship until the project supports
  at least 10 different API providers (two exist today). The `1.0.0` freeze
  pressure that drove every original timing objection is therefore gone for the
  foreseeable future. PM directed that `response_next` be locked in (RFC
  accepted) and shipped now in the next minor release (`0.10.0`, per
  `ROADMAP.md`'s pre-1.0 versioning model) rather than deferred to a
  post-`1.0.0` `MINOR`. The design itself, the cross-team corrections review
  produced, and the scoping-spike-first sequencing are all unchanged. The four
  substantive reviewers were re-consulted under `$topology-consult` and each
  returned `Approved` (see *PM re-direction re-consultation* below). Status
  moved to `Accepted` with this revision.

## Summary

`connectors/rickandmorty`, the second independently authored package delivered
under the active `0.9.0` goal, could not represent the Rick and Morty API's
native pagination (`info.next`/`info.prev` absolute URLs embedded in the JSON
response body) using `duckdb_api/v1`'s closed REST pagination set (`disabled`
or `link_next`), so both its relations used `disabled` pagination instead.
This RFC decides whether to add a narrow new strategy — body-signaled,
page-number-reconstructed pagination, architecturally identical to `link_next`
except the continuation signal is read from a declared JSON path instead of an
HTTP `Link` header — and, either way, whether the `1.0.0` frozen contract
includes or explicitly excludes it.

## Sponsorship and context

- **RFC type:** Product. The decision changes `duckdb_api/v1`'s closed
  connector-package pagination contract, a public, author-facing surface.
- **Sponsoring team:** Connector Experience, which owns
  `docs/CONNECTOR_SPECIFICATIONS.md` and the package schema/compiler this
  decision extends.
- **Linked outcome or objective:** The active `0.9.0` goal's delivery path
  step 3 is to "enumerate and freeze the complete `1.0.0` public contract...
  [including] explicit exclusions." This RFC resolves the one concrete gap
  that goal's first trial found, so that enumeration can proceed on a
  decided basis rather than a silent omission.
- **Why now:** The gap was discovered by direct evidence (a second real
  package's native shape), not speculation, and `0.9.0`'s own guardrail
  forbids expanding capability inside that goal. Deciding now — before the
  `1.0.0` contract is frozen — is the only way to include this deliberately
  rather than by accident, and the only way to exclude it deliberately rather
  than by omission.

## Problem

`docs/CONNECTOR_SPECIFICATIONS.md`'s REST operations section states:
*"Pagination is either disabled or sequential mutable `Link: rel=next`."*
`connectors/rickandmorty/relations/character_search.yaml` needs a genuinely
different shape: the Rick and Morty API returns

```json
{"info": {"count": 2, "pages": 1, "next": "https://rickandmortyapi.com/api/character?page=2", "prev": null}, "results": [...]}
```

with the continuation URL in the body, not a response header. Because v1 has
no way to declare this, `connectors/rickandmorty` uses `pagination: strategy:
disabled` for both relations (see the package's own README and
`docs/goals/public-connector-authoring-candidate.md`'s completion record),
meaning the second package proves anonymous auth, a different response
envelope, and a declared relation `input`, but not pagination generalization
— one of the three axes `0.9.0`'s own success signal named.

This is not a defect: `RFC 0013` scoped pagination deliberately to what
GitHub — the only real package it had evidence against — actually needed
(`link_next` and GraphQL Relay cursors). `docs/CONNECTOR_SPECIFICATIONS.md`'s
compatibility boundary does not list body-embedded pagination as an explicit
exclusion; it is simply absent, and that section's own governing rule is
*"Adding any such capability requires a later accepted contract."* This RFC is
that later accepted contract, or the decision not to grant one yet.

## Decision drivers and invariants

- **Must preserve:** every invariant `link_next` currently enforces —
  sequential, bounded, cancelable pagination; no ordering, snapshot,
  parallelism, resume, deduplication, retry, or cache guarantee; an accepted
  continuation stays on the exact operation origin and path; resource
  ceilings intersect host limits; RFC 0009/0012/0013/0014's accepted
  interfaces.
- **Must enable:** representing a real, common REST pagination shape (a
  body-embedded continuation URL) without granting the package any new
  network authority beyond what `link_next` already grants.
- **Must not introduce:** a capability that treats server-supplied content as
  a literal fetch target without verification (see Decision drivers below on
  why this RFC deliberately scopes to reconstruct-and-verify, not
  extract-and-follow); a new capability that lands inside the `0.9.0` goal's
  own "does not expand" boundary — any acceptance here is a decision, not an
  implementation, and implementation is routed to a separate goal (see
  Follow-on goals).

## Proposed decision

### The key architectural fact this proposal rests on

`link_next` is not "follow the URL the server gives you." Runtime
reconstructs every continuation request locally from the relation's declared
`page_number_parameter`, `first_page`, and `page_increment`; the received
`Link` header's target is parsed and compared, byte-for-byte, against that
locally reconstructed request, and rejected on any mismatch, replay, or
malformed value (`src/runtime/pagination/link_pagination.cpp`,
`ValidateNextTarget`). The header is a *verified signal*, never a
dereferenced instruction. Rick and Morty's actual `info.next` value —
`https://rickandmortyapi.com/api/character?page=2` — is exactly this shape:
same origin, same path, an incrementing `page` query parameter identical to
what `page_number_parameter`/`first_page`/`page_increment` already model.

This RFC proposes **`response_next`**: a REST pagination strategy identical to
`link_next` in every respect — `dependency: sequential`, `consistency:
mutable`, `target_scope: exact_operation_origin_and_path`,
`page_size_parameter`, `page_size`, `page_number_parameter`, `first_page`,
`page_increment`, `max_pages_per_scan` all required and behaving exactly as
today — except the continuation signal is read from a declared JSON path in
the decoded response body (a new required `next_url_path` field, validated
under the existing `json_path_v1` extractor grammar, e.g. `$.info.next`)
instead of an HTTP `Link` header. JSON `null` or an absent path at runtime
means "no next page." A present value is validated and reconstructed against
exactly the same rules `ValidateNextTarget` already applies to a Link header
value: well-formed absolute HTTPS URL, exact origin match, exact path match,
and an exact query-multiset match against the locally reconstructed next
page — generalizing that function to accept either a header-sourced or a
body-sourced target string, not adding a second, divergent validation path.

**Deliberately not proposed:** a general "extract an opaque cursor from the
body and use it directly to build the next request" strategy (the shape
GraphQL's existing Relay cursor pagination already uses internally). That
model has a materially different trust flow — the extracted value drives the
next request directly rather than being verified-then-discarded — and
deserves its own dedicated safety analysis if and when a real package needs
it. See Alternatives.

### Public behavior

Adds one new accepted value to `pagination.strategy` in
`docs/CONNECTOR_SPECIFICATIONS.md`'s REST operations grammar, plus one new
required field (`next_url_path`) when that strategy is selected. No existing
accepted package, including `connectors/github` and `connectors/rickandmorty`
as currently authored, changes behavior — this is an addition to the closed
set, not a change to `disabled` or `link_next`. `EXPLAIN` output gains
`response_next` as a possible `pagination` fact value alongside `disabled`
and `link_next`.

### Shared interfaces

- **Connector Experience:** `package_rest_schema.cpp`'s
  `DecodeRestPaginationSchema` gains a `response_next` branch requiring
  `next_url_path` (and rejecting the header-oriented fields' absence exactly
  as it does today for `link_next`, since the field set is otherwise
  identical); `CompiledPaginationStrategy` gains `RESPONSE_NEXT_URL`;
  `CompiledPagination` gains the compiled extractor for `next_url_path`.
- **Relational Semantics:** genuinely affected, corrected from an earlier
  draft of this RFC that claimed otherwise. Three of Semantics' own
  exhaustive, fail-closed switches over pagination strategy must gain a
  `RESPONSE_NEXT_URL` arm or a compiled generation using it throws an
  uncaught `std::logic_error` rather than a typed `PlanningError`:
  `PlanBaseDomain` (`src/semantics/scan_planner_internal.hpp`, which decides
  the base occurrence domain — the exact artifact the predicate-exactness
  law in `docs/CONNECTOR_SPECIFICATIONS.md` depends on), `ValidatePagination`
  (`src/semantics/scan_planner_validation.cpp`), and `PaginationStrategyName`
  (`src/semantics/scan_plan_explain.cpp`, which builds `ScanPlan`'s own
  explanation facts per `docs/RUNTIME_CONTRACTS.md`). Whether
  `response_next`'s base occurrence domain is identical to `link_next`'s
  (i.e., whether body-signaled continuation preserves the same
  occurrence/duplication guarantees) is a live semantic decision reserved to
  Relational Semantics, not something Connector Experience or Remote Runtime
  can decide on Semantics' behalf.
- **Remote Runtime:** the primary implementer. After decoding a page's body
  (already required to extract relation rows), Runtime also extracts
  `next_url_path`'s value, then applies a generalized target-validation step
  to decide continuation. `ValidateNextTarget` itself
  (`src/runtime/pagination/link_pagination.cpp`) is already source-agnostic —
  it compares a plain string against a locally reconstructed expectation and
  has no dependency on the Link-header grammar — so the reconstruct-and-verify
  core reuses cleanly. Two things do not reuse for free and must be resolved
  before implementation, not assumed away: (1) `LinkPaginationState::Advance`
  is hard-wired to header field values and needs a sibling entry point, not a
  signature change to the header path; (2) whether `next_url_path` extraction
  requires a second parse of the already-decoded body or can share the
  decoder's single pass is an open architecture question with real resource-
  budget consequences, not yet resolved by this RFC (see Drawbacks). A body-
  extracted candidate string can also diverge byte-for-byte from what a
  functionally identical Link-header value would contain (JSON `\uXXXX`
  unescaping versus percent-encoding), which byte-exact comparison would
  reject as malformed even though the URL is legitimate — a genuinely new
  correctness edge case `link_next` does not have, requiring an explicit
  normalization rule before the byte-exact comparison, not silent reuse of
  today's comparison as-is.
- **Query Experience:** genuinely affected, corrected from an earlier draft
  of this RFC that understated this. DuckDB's user-facing `EXPLAIN` output
  is rendered by Query's own `PaginationStrategyName`/`AddPaginationFacts`
  (`src/query/duckdb/scan_plan_explanation.cpp`), a second exhaustive switch
  distinct from (and downstream of) Semantics' own `scan_plan_explain.cpp`
  copy — both must gain a `response_next`/`RESPONSE_NEXT_URL` arm. Query's
  actual `EXPLAIN` vocabulary today is `"disabled"`, `"link_header"`, and
  `"graphql_cursor"` (confirmed against
  `test/cpp/query/duckdb/graphql_adapter_contract_tests.cpp`), not the
  author-facing YAML strategy names this RFC's earlier draft conflated it
  with.

### Operational behavior

No new resource, cancellation, or backpressure model — `max_pages_per_scan`
and the existing per-page/per-scan byte and record ceilings apply unchanged.
No new credential or network-policy authority: the target-validation rule is
identical to `link_next`'s (exact origin and path), so a compromised or
malicious response body can cause pagination to terminate or fail, never to
redirect the scan off the declared origin.

## Topology impact

| Team | Role in this RFC | Interface or charter impact | Interaction mode | Exit condition |
| --- | --- | --- | --- | --- |
| Connector Experience | Sponsor and schema/compiler owner | New `response_next` schema branch and compiled IR field; no charter text change | X-as-a-Service (existing) | Package authors can declare `response_next` and get the same diagnostic quality as `link_next` |
| Remote Runtime | Primary implementer | New sibling pagination-state entry point (not a header-path signature change); resolves the single-pass-vs-second-parse decoder question; defines an explicit encoding-normalization rule before byte-exact target comparison; no charter text change | Collaboration until the decoder question and encoding rule are proven, then X-as-a-Service | The generalized validator passes the same malformed/replay/cross-origin fixture oracles for both header- and body-sourced targets, including at least one encoding-divergence case |
| Relational Semantics | Required implementation participant, not a passive reviewer | `PlanBaseDomain`, `ValidatePagination`, and `PaginationStrategyName` (`src/semantics/scan_plan_explain.cpp`) each require a `RESPONSE_NEXT_URL` arm; base-occurrence-domain equivalence to `link_next` is a live semantic decision reserved to this team | Collaboration | A property test proves `response_next`'s `BaseDomain` classification behaves identically to `link_next`'s under the same fixture rows |
| Query Experience | Required implementation participant | `PaginationStrategyName`/`AddPaginationFacts` (`src/query/duckdb/scan_plan_explanation.cpp`) requires a `response_next` arm; `EXPLAIN`'s `"Pagination Strategy"` fact gains a third accepted value alongside `"disabled"`/`"link_header"`/`"graphql_cursor"` | X-as-a-Service (existing) | A Query-owned differential test (mirroring `graphql_adapter_contract_tests.cpp`) asserts the new `EXPLAIN` value and row shape |
| Engineering Enablement | Facilitator | Helps establish the fixture-coverage variant set, corrected to include a non-string/wrong-type `next_url_path` variant (`link_next`'s set alone is insufficient — see Acceptance and verification) | Facilitation | Connector Experience, Remote Runtime, Relational Semantics, and Query Experience maintain the corrected oracle independently |

Review corrected two real gaps in an earlier draft of this table: Relational
Semantics and Query Experience are each required implementation
participants with their own exhaustive switches to extend, not unaffected or
minimally affected bystanders. Cognitive load is genuinely distributed across
four teams (Connector Experience, Remote Runtime, Relational Semantics, Query
Experience), each extending one existing exhaustive switch it already owns by
direct analogy to `link_next`'s existing arm — not a new pattern for any of
them, but real, non-trivial work in four places, not one.

## Correctness, security, and lifecycle analysis

- **Relational semantics and conservative fallback:** Affected, corrected
  from an earlier draft that claimed otherwise. Pagination strategy carries
  no *relational* authority (no predicate, ordering, or limit change), but
  `PlanBaseDomain`'s classification of `response_next`'s base occurrence
  domain is a real decision Relational Semantics must make deliberately —
  GraphQL cursor pagination already proves pagination strategy reaches this
  layer's planning code today, contradicting the "entirely below Semantics"
  assumption. Row cardinality across pages remains bounded by
  `max_records_per_scan` regardless of strategy once that classification is
  made correctly.
- **Authentication, credentials, network policy, and privacy:** The
  target-validation rule (exact origin and path) is unchanged in principle
  from `link_next`; a body-embedded URL is never treated as network
  authority beyond what the declared operation origin already grants. No
  credential handling changes. The encoding-normalization question below is
  a correctness, not an authority, concern — a rejected legitimate
  continuation fails closed (scan stops with a diagnostic), it does not
  grant excess authority.
- **Resource budgets, backpressure, and cancellation:** `max_pages_per_scan`
  and existing per-page/per-scan ceilings and cancellation checkpoints apply
  unchanged in principle, but whether extracting `next_url_path` requires a
  second parse of the decoded body (added CPU/latency per page) or shares
  the existing single decode pass is unresolved by this RFC and must be
  answered before implementation, not assumed to be free.
- **Replay, retries, caching, and duplicate prevention:** Reuses `link_next`'s
  `seen_pages` replay-rejection check verbatim, generalized to the new
  target-string source. A body-extracted candidate string can be a
  byte-different-but-equivalent encoding of the same logical URL a
  functionally identical Link header would carry (JSON `\uXXXX` escaping
  versus percent-encoding); byte-exact comparison without an explicit
  normalization rule would reject a legitimate continuation as malformed.
  This is a genuinely new correctness edge case `link_next` does not have
  and must be designed against, not discovered in production.
- **Concurrency, immutability, and state ownership:** No change; pagination
  state ownership and `max_concurrent_pages` semantics are identical.
- **FFI, initialization, reload, shutdown, and failure containment:** Not
  affected.
- **Diagnostics, redaction, metrics, and progress reporting:** Reuses
  existing diagnostic codes (`DUCKDB_API_MISSING_FIELD`,
  `DUCKDB_API_INVALID_EXTRACTOR`, `DUCKDB_API_POLICY_WIDENING`) for the new
  compile-time field; reuses the existing `ErrorStage::POLICY` /
  `"pagination.next"` runtime failure shape.

## Compatibility and migration

Additive only. No existing accepted package (`connectors/github`,
`connectors/rickandmorty`) changes behavior or requires a version bump.
`connectors/rickandmorty`'s `character_search` and `pilot_episode` relations
are not modified by this RFC; adopting `response_next` for them, if desired,
is a separate change to that package (a `MINOR` version bump under RFC
0013's compatibility table, since it would change an existing relation's
pagination fact) tracked as a follow-on, not decided here.

## Evidence and bounded trials

| Question or claim | Evidence required | Method or fixture | Result and limitations |
| --- | --- | --- | --- |
| Rick and Morty's actual pagination shape fits the proposed reconstruct-and-verify model | Live API response inspection | Direct `curl` against `rickandmortyapi.com/api/character` (recorded in `connectors/rickandmorty/fixtures/`) | Confirmed: `info.next` is the exact origin and path with an incrementing `page` query parameter, exactly the shape `page_number_parameter`/`first_page`/`page_increment` already model. No opaque cursor is used by this particular API. |
| `link_next` never dereferences server-supplied content as a literal fetch target | Direct code inspection | Read `src/runtime/pagination/link_pagination.cpp`'s `ValidateNextTarget` and `ParseTransition` | Confirmed: the Link header value is parsed and compared against a locally reconstructed request; it is never used as the literal request target. The proposed `response_next` strategy reuses this exact model. |
| A generalized opaque-cursor body strategy would need a different trust model | Comparison against existing GraphQL cursor pagination | Read `src/include/duckdb_api/internal/runtime/pagination/graphql_cursor_pagination.hpp` and `src/runtime/execution/graphql_paginated_scan.cpp` | Confirmed: GraphQL's cursor value is extracted from the body and used directly to construct the next request's variables, a materially different flow-of-trust than reconstruct-and-verify. Deliberately out of scope here; flagged for a future RFC if a real package needs it. |
| No real package today needs the opaque-cursor shape | Absence of counter-evidence | Repository inspection: `connectors/github` (Link header, GraphQL cursor) and `connectors/rickandmorty` (fits reconstruct-and-verify) are the only two real packages in the repository | No pending decision-critical gap; if a third package later needs opaque-cursor pagination, that is new evidence for a new RFC, not something this one must anticipate. |

## Alternatives considered

1. **Add `response_next` now, scoped to reconstruct-and-verify (proposed).**
   Benefit: represents a real, evidenced gap with no new authority beyond
   `link_next`'s existing model; keeps the design conservative. Drawback:
   still an addition to the closed `1.0.0`-candidate contract, requiring
   Remote Runtime implementation work before the freeze if it is to be
   included in `1.0.0` itself.
2. **Add a general opaque-cursor body-pagination strategy now** (extract a
   value, use it directly in the next request, mirroring GraphQL's cursor
   model for REST). Benefit: covers a broader class of real-world APIs
   (Stripe-style, Shopify-style cursor pagination). Drawback: a materially
   different, less conservative trust model needing its own dedicated
   security analysis; no real package in this repository currently needs it,
   so there is no bounded trial to prove it against. Not selected for this
   RFC; recorded as explicitly out of scope, revisit when evidenced.
3. **Explicitly exclude body-signaled pagination from `1.0.0` and defer.**
   Benefit: strictly consistent with `0.9.0`'s "prove and freeze, do not
   expand" guardrail; zero implementation risk before the freeze. Drawback:
   `connectors/rickandmorty` permanently cannot exercise its most natural
   pagination path under `1.0.0`, and this exact gap resurfaces the next time
   a package needs it, at higher cost (mid-flight again, exactly the pattern
   RFC 0015 fixed for topology). This alternative is presented to reviewers
   as genuinely live, not foreclosed — see Unresolved questions and
   Follow-on goals for how the timing question is kept separate from the
   design question.
4. **Retain current behavior (no new strategy, no exclusion recorded).**
   Rejected: leaves the gap undocumented rather than deciding it, which is
   what discovering this gap during `0.9.0` was supposed to prevent.

## Drawbacks and failure modes

- `ValidateNextTarget`'s generalization must not weaken either call path;
  a shared implementation that is correct for headers but subtly wrong for
  body-extracted strings (different encoding/whitespace handling) would be
  worse than two separate, clearly-scoped implementations. Remote Runtime
  owns this design choice during implementation, gated by the
  encoding-normalization rule and the single-pass-extraction question this
  RFC surfaces but does not resolve.
- Review found the cross-team surface is larger than an earlier draft of
  this RFC scoped: four teams (Connector Experience, Remote Runtime,
  Relational Semantics, Query Experience) each extend one exhaustive switch
  they own, not one team extending a schema plus one team implementing
  execution. Omitting Relational Semantics' or Query Experience's arm is not
  a cosmetic gap — both switches are exhaustive and throw an uncaught,
  crash-class exception (`std::logic_error`/`InternalException`) on an
  unhandled enum value today, so a compiled generation using `response_next`
  that reaches either unextended switch fails closed as a crash, not as a
  typed diagnostic, until both are updated.
- The fixture-coverage variant set proposed for `link_next` is insufficient
  as a template on its own: it has no category for `next_url_path` resolving
  to a present, non-null, non-string value (a JSON number, object, or array),
  which cannot reach `malformed_target_rejected` (that category validates an
  already-extracted string). GraphQL cursor pagination's own coverage set
  already has the analogous category (`missing_cursor_rejected`); the
  `response_next` set needs an equivalent (see Acceptance and verification).
- If accepted for `1.0.0` inclusion, Remote Runtime carries new
  implementation and fixture work — now understood to include a
  decoder-architecture question that may itself require a scoping spike —
  before the freeze can close, on a timeline this RFC does not set.
  Relational Semantics and Query Experience each carry smaller, but real,
  implementation and test work in parallel.
- If excluded from `1.0.0`, the gap is deliberately deferred, not fixed;
  authors targeting APIs with this pagination shape must still use
  `disabled` pagination (a real but disclosed limitation).

## Acceptance and verification

- **End-to-end demonstration:** `connectors/rickandmorty`'s `character_search`
  relation (or a new fixture relation) declares `response_next` pagination
  against a recorded multi-page transcript and returns all rows across pages
  through the same `duckdb_api_load_connector` path already proven.
- **Automated oracle:** fixture-coverage keys mirroring `link_next`'s
  (`first_page`, `multi_page`, `termination`, `encoded_target`,
  `malformed_target_rejected`, `replayed_target_rejected`,
  `max_pages_exhausted`), **plus one new category `link_next` structurally
  cannot have and this RFC's earlier draft omitted**: a present, non-null,
  non-string value at `next_url_path` (e.g. `next_field_wrong_type_rejected`)
  — justified by direct analogy to GraphQL cursor pagination's own
  `missing_cursor_rejected` category, which exists precisely because a
  body-extracted continuation signal has a failure mode header extraction
  does not. Also required: a differential test proving identical behavior
  between a header-sourced and a body-sourced target string given the same
  underlying page sequence, including at least one case where the two are
  byte-different-but-equivalent encodings of the same logical URL (JSON
  `\uXXXX` escaping versus percent-encoding), and a Relational Semantics
  property test proving `response_next`'s `BaseDomain` classification
  matches `link_next`'s under the same fixture rows.
- **Quality gates:** `make build`, `make test`, `make demo`; the existing
  package/fixture/coverage gates extended to the new strategy.
- **Independent review:** `$topology-consult` review from all five required
  reviewers, recorded below.
- **Interaction exit:** Remote Runtime's generalized validator passes the
  full malformed/replay/cross-origin oracle set for both target sources
  without Connector Experience needing transport-internal knowledge.

## Contract propagation

| Source of truth or artifact | Impact | Required update | Completion evidence |
| --- | --- | --- | --- |
| `docs/ARCHITECTURE.md` | Not affected | No architectural-invariant change | Not applicable |
| `docs/CONNECTOR_SPECIFICATIONS.md` | Affected by this acceptance (decision recorded now); full grammar reference lands with `0.10.0` | Note in the REST operations section that `response_next` is accepted by RFC 0016 and pending `0.10.0` implementation; the closed-set grammar (`{disabled, link_next}`) and the explicit-exclusion prose remain accurate until `0.10.0` ships | Completed by this RFC's acceptance (the note is added in the same change) |
| `docs/RUNTIME_CONTRACTS.md` | Affected by `0.10.0` implementation | Document the generalized target-validation rule covering both header and body sources | Pending the `0.10.0` implementation goal |
| `docs/TEAM_TOPOLOGY.md` and active charters | Not affected | No interface or accountability boundary moves | Not applicable |
| `docs/PRODUCT_DELIVERY.md`, `AGENTS.md`, and skills | Not affected | No change | Not applicable |
| `ROADMAP.md` | Affected by this acceptance | Record the ≥10-API-provider `1.0.0` release gate that dissolves the freeze-pressure rationale for this and any future `1.0.0`-candidate decision | Completed by this RFC's acceptance (the release-gate note is added in the same change) |
| `release/1.0.0/freeze.json` and `release/1.0.0/freeze.md` | Affected by this acceptance | Add a new `accepted_candidate_revisions` section recording `response_next` as decided-but-pending-`0.10.0`-implementation; the schema-closed `pagination_strategies.rest` set stays at `{disabled, link_next}` and `pagination_body_url_offset_or_cursor_in_body_strategies` remains a mandatory exclusion of the `1.0.0` boundary until `0.10.0` graduates the strategy | Completed by this RFC's acceptance (the section is added in the same change, with mutation coverage in `test/python/contract_freeze_tests.py`) |
| Examples, diagnostics, fixtures, and tests | Affected by `0.10.0` implementation | New fixture-coverage variant set, differential header/body target tests, the `BaseDomain`-equivalence property test | Pending the `0.10.0` implementation goal |
| `1.0.0` frozen public contract (0.9.0's own deliverable) | Not affected by this acceptance | The `1.0.0` candidate freeze records `response_next` as a pending candidate revision; the frozen boundary itself is unchanged | The candidate-revision section is the completion evidence; the boundary change happens when `0.10.0` ships and the candidate freeze is re-cut |

## Unresolved questions

- **Decision-critical, resolved by review:** does `response_next` land
  inside the `1.0.0` frozen contract now, or is it explicitly excluded from
  `1.0.0` and reserved for a later minor version? **Resolved by PM
  re-direction (2026-07-21):** the `1.0.0` versus post-`1.0.0` framing no
  longer applies in its original form, because `1.0.0` is no longer imminent
  (PM gated it on ≥10 API providers). `response_next` is implemented in
  `0.10.0` and graduates into the schema-closed set when `0.10.0` ships. The
  `1.0.0` candidate freeze records it under `accepted_candidate_revisions`
  until then; `1.0.0`'s eventual publication will inherit the closed set
  `0.10.0` ships with, not the `0.9.0` snapshot.
- Non-blocking: exact field name (`next_url_path` vs. an alternative
  spelling) — cosmetic, resolved at implementation time.
- Non-blocking: whether a future opaque-cursor REST strategy should be
  named and scoped now for discoverability, or left entirely unaddressed
  until a real package needs it — deferred, not this RFC's to answer. Remote
  Runtime's review noted the real distinguishing safety property for such a
  strategy would be "destination fixed regardless of cursor content, only a
  request variable is attacker-influenced" (as GraphQL cursor pagination
  already demonstrates) — useful framing for whoever picks this up later,
  not a decision this RFC makes.
- **Decision-critical for the `0.10.0` implementation goal (carried there as
  its first required outputs, the scoping spike):** whether `next_url_path`
  extraction requires a second parse of the decoded body or can share the
  decoder's single pass, and the exact encoding-normalization rule for
  comparing a body-extracted target against a reconstructed expectation. The
  original RFC review required both be answered before implementation
  commits; that requirement is preserved unchanged by the PM re-direction.

## Review record

| Required reviewer | Team | Result | Evidence or objection | Disposition by decision owner |
| --- | --- | --- | --- | --- |
| Connector Experience perspective | Connector Experience | Approved | Verified `link_next`'s current schema fields and `ValidateNextTarget`'s reconstruct-and-verify behavior directly against source; confirmed `response_next` fits the closed-schema pattern cleanly with no special-casing needed and existing diagnostic codes give adequate field-precise author errors. Recommended inclusion in `1.0.0` now, reasoning primarily from the schema/compiler side being straightforward. | Accepted as accurate for the schema/compiler surface specifically; the fuller cross-team surface the other four reviews uncovered changes the timing conclusion (see Rationale), not the design's soundness. |
| Query Experience perspective | Query Experience | Objected (Needs evidence) | Correctly identified that an earlier draft of this RFC mischaracterized Query's affected surface: the actual `EXPLAIN`-rendering code is `PaginationStrategyName`/`AddPaginationFacts` in `src/query/duckdb/scan_plan_explanation.cpp`, an exhaustive switch distinct from the author-facing YAML vocabulary the draft cited, and distinct from (downstream of) Relational Semantics' own same-named function. Recommended excluding `response_next` from `1.0.0` and landing it as a fast-follow `MINOR` once Remote Runtime's validator and Query's own `EXPLAIN` arm/test exist and are proven. | Accepted; RFC's Shared interfaces and Topology impact sections corrected to name the exact file/function and add a required Query-owned differential test to the Follow-on goal. |
| Remote Runtime perspective | Remote Runtime | Objected (Needs evidence) | Confirmed `ValidateNextTarget`'s reconstruct-and-verify core reuses cleanly and could not construct a counterexample granting new network authority, but identified two real open questions this RFC had not resolved: whether `next_url_path` extraction needs a second body parse (resource/budget consequence), and a genuinely new correctness edge case — JSON `\uXXXX` unescaping versus percent-encoding can make a body-extracted target byte-different from an equivalent header value, which byte-exact comparison would wrongly reject. Conditionally supported `1.0.0` inclusion, contingent on treating the decoder question as a timeboxed spike gating the follow-on goal. | Accepted; RFC's Shared interfaces, Correctness analysis, and Drawbacks sections corrected to state both open questions explicitly, and a dedicated scoping-spike goal added to Follow-on goals ahead of the implementation goal. |
| Relational Semantics perspective | Relational Semantics | Objected (Needs evidence) | Identified that an earlier draft's "Relational Semantics: Not affected" claim was incorrect: `PlanBaseDomain`, `ValidatePagination`, and `PaginationStrategyName` (`src/semantics/scan_plan_explain.cpp`) are exhaustive, fail-closed switches that throw an uncaught `std::logic_error` on an unhandled pagination strategy, and GraphQL cursor pagination already proves pagination strategy reaches Semantics' planning code today. Required Relational Semantics be a named implementation participant with a `BaseDomain`-equivalence property test, not an unaffected reviewer. Supported inclusion only if the Follow-on goal is amended accordingly; otherwise preferred explicit exclusion. | Accepted; this was the most consequential correction — RFC's Shared interfaces, Topology impact, and Follow-on goals sections all revised to name Relational Semantics as a required Collaboration participant with the property-test requirement. |
| Engineering Enablement perspective | Engineering Enablement | Objected (Needs evidence) | Identified that "mirror `link_next`'s fixture-coverage set exactly" omits a category `link_next` structurally cannot have: a present, non-null, non-string value at `next_url_path`, which cannot reach `malformed_target_rejected` (that category validates an already-extracted string, not a type mismatch at extraction). Cited GraphQL cursor pagination's own `missing_cursor_rejected` category as the closer, already-accepted precedent. Recommended excluding `response_next` from `1.0.0`, given the validator's generalization was still undecided and the oracle itself was incomplete as proposed. | Accepted; a new `next_field_wrong_type_rejected`-equivalent coverage category added to the Acceptance and verification section, justified by the cited GraphQL precedent. |

Four of five reviewers converged independently on excluding `response_next`
from `1.0.0` and landing it as a fast-follow `MINOR` (Query Experience,
Remote Runtime conditionally, Relational Semantics conditionally, Engineering
Enablement); only Connector Experience recommended immediate inclusion, and
its reasoning addressed only the schema/compiler surface, which review
confirmed is in fact the smallest part of the actual cross-team cost. No
reviewer objected to the underlying design (`response_next` as a
reconstruct-and-verify strategy analogous to `link_next`) — every objection
was about completeness of the plan (missing team participation, a missing
oracle category, unresolved implementation questions), which this revision
resolves directly in the RFC text, not about the design being unsafe or
contract-violating.

## PM re-direction re-consultation (2026-07-21)

After the original five-team review produced the proposed *defer-to-post-
`1.0.0`* disposition, the product manager (Nic Galluzzo) re-prioritized
`1.0.0`: it will not ship until the project supports at least 10 different
API providers (two exist today). The freeze pressure that drove every
original timing objection is therefore gone for the foreseeable future, and
PM directed that `response_next` be locked in (RFC accepted) and shipped in
the next minor release (`0.10.0`) rather than deferred. Because this changes
the decision the original review objected to, the four substantive reviewers
were re-consulted under `$topology-consult` with fresh reviewer contexts
that received the PM direction, the original objection, and the unchanged
RFC text, and were asked whether the revised timing resolves or escalates
the original concern. The sponsoring team (Connector Experience) originally
`Approved` and its approval carries forward: the revised decision moves in
the direction it preferred (immediate inclusion).

| Required reviewer | Team | Original result | Re-consultation result | Evidence | Disposition |
| --- | --- | --- | --- | --- | --- |
| Connector Experience perspective | Connector Experience | Approved | Approved (carries forward) | Sponsoring team; immediate-inclusion preference now adopted. Schema/compiler surface extension unchanged by version label. | Accepted; designee remains Connector Experience as the `0.10.0` accountable team |
| Query Experience perspective | Query Experience | Objected (Needs evidence) | Approved | Original objection was timing-only ("compress into `1.0.0` freeze"). `1.0.0` is now far away (≥10-provider gate); the Query-owned `EXPLAIN` switch extension and required differential test land as primary work, not a race against a freeze. Raised one cross-team note: pre-1.0 versioning model requires `0.Y.0` for capability additions, satisfied by `0.10.0` (PM-confirmed). | Accepted; versioning concern resolved by PM's `0.10.0` direction |
| Remote Runtime perspective | Remote Runtime | Objected (Needs evidence) | Approved | `ValidateNextTarget` (`src/runtime/pagination/link_pagination.cpp:47-137`) is source-agnostic and reuses cleanly under either release label. `LinkPaginationState::Advance` (`:211-235`) is the genuine non-reuse point; the sibling-entry-point requirement is invariant to version numbering. The two original open questions (decoder single-pass-vs-second-parse; JSON `\uXXXX`-vs-percent-encoding byte divergence) carry forward as the gating scoping spike's first outputs, exactly as originally required. Removing freeze pressure strictly reduces schedule-compression hazard without weakening any invariant. | Accepted; spike-first sequencing preserved |
| Relational Semantics perspective | Relational Semantics | Objected (Needs evidence) | Approved | The amendment the original objection required is preserved verbatim: Follow-on goals names Relational Semantics as Collaboration participant with the `BaseDomain` property test; Shared interfaces reserves the base-occurrence-domain equivalence question to this team; Topology impact records the property-test exit. All three exhaustive switches confirmed fail-closed today (`scan_planner_internal.hpp:93`, `scan_planner_validation.cpp:125`, `scan_plan_explain.cpp:230`) and require a `RESPONSE_NEXT_URL` arm or crash with uncaught `std::logic_error`; that obligation is unchanged by version numbering. | Accepted; the conditional support condition is satisfied |
| Engineering Enablement perspective | Engineering Enablement | Objected (Needs evidence) | Approved (with required action on freeze lifecycle) | Original oracle concern resolved: the corrected coverage category (`next_field_wrong_type_rejected`) plus the GraphQL `missing_cursor_rejected` precedent (`fixture-coverage-v1.json:133`) close the gap. Required action on the new `accepted_candidate_revisions` freeze section: (a) a documented semantic distinguishing it from `exclusions`/`not_yet_frozen`/`fast_follows`; (b) mutation coverage in `contract_freeze_tests.py`; (c) a graduation rule for when an entry moves into the closed set. RFC 0016 itself stays out of `rfc_authorities.accepted` (which binds the `1.0.0` boundary directly) since `response_next` is excluded from `1.0.0`. | Accepted; required action committed in this acceptance's contract propagation (the new section lands with semantic, mutation tests, and graduation rule) |

All five required reviewers approved the revised decision. Each confirmed
the revised timing satisfies rather than escalates the original concern; no
new objection was raised. Two cross-team observations surfaced (versioning
semantics and freeze-lifecycle discipline), both addressed by this RFC's
acceptance: the versioning choice is `0.10.0` (PM-confirmed, follows
`ROADMAP.md`'s pre-1.0 rule), and the new freeze section lands with the
documentation, mutation coverage, and graduation rule Engineering Enablement
required.

## Decision and rationale

- **Technical decision owner:** Lead agent.
- **Product approval:** **Received — Nic Galluzzo, 2026-07-21.** The product
  manager directed (1) that `1.0.0` will not ship until the project supports
  at least 10 different API providers (two exist today), dissolving the
  `1.0.0`-freeze-pressure rationale that drove every original timing
  objection; and (2) that `response_next` be locked in (this RFC accepted) and
  shipped in the next minor release (`0.10.0`) rather than deferred to a
  post-`1.0.0` `MINOR`. The versioning choice follows `ROADMAP.md`'s pre-1.0
  rule that backward-compatible capability additions are `0.Y.0` minors, not
  `0.Y.Z` patches; the active goal therefore targets `0.10.0`, not `0.9.1`.
- **Decision:** The `response_next` design is **accepted**, and
  **implementation is committed for `0.10.0`** (not deferred to a
  post-`1.0.0` `MINOR`). The scoping spike (decoder single-pass-vs-second-parse
  question and encoding-normalization rule) remains the gating first output of
  the `0.10.0` implementation goal exactly as the original Remote Runtime
  review required. The freeze artifact records `response_next` as an
  **accepted candidate revision pending implementation** in a new
  `accepted_candidate_revisions` section (distinct from permanent exclusions,
  evidence-derived `not_yet_frozen`, and discovered-gap `fast_follows`):
  decided, but not yet graduated into the schema-closed `pagination_strategies`
  set, which stays at `{disabled, link_next}` until `0.10.0` ships.
- **Rationale:** The design itself — a REST pagination strategy identical to
  `link_next` except its continuation signal is read from a declared JSON body
  path, reusing the same reconstruct-and-verify safety model — is sound and
  unobjected-to by any reviewer in either the original or re-consultation
  review. What changed between the initial proposed decision (defer to
  post-`1.0.0` `MINOR`) and this accepted decision (implement now in
  `0.10.0`) is *only the product context*: the PM dissolved the time pressure
  that drove every original timing objection. With `1.0.0` gated on 10+
  providers, the cross-team implementation surface (two additional exhaustive
  switches in Relational Semantics, one in Query Experience, the unresolved
  decoder-architecture question, the fixture-coverage oracle, and the
  encoding-correctness edge case) is no longer compressed against a freeze
  window — exactly what four of five reviewers asked for. The RFC's text
  corrections review produced (naming Relational Semantics and Query Experience
  as required implementation participants, adding the new fixture-coverage
  category, reserving the `BaseDomain`-equivalence decision to Relational
  Semantics) carry forward verbatim into the `0.10.0` goal. This keeps faith
  with `docs/CONNECTOR_SPECIFICATIONS.md`'s own rule — "adding any such
  capability requires a later accepted contract" — this RFC is that contract,
  and it now commits to implementation rather than deferring it.
- **Material objections:** All four original objections (Query Experience,
  Remote Runtime, Relational Semantics, Engineering Enablement) were
  dispositioned by directly revising the RFC's technical content (see Review
  record table above for each disposition) rather than by overriding them; none
  was rejected. The PM re-direction re-consultation (below) returned
  `Approved` from all four substantive reviewers, each confirming the revised
  timing satisfies rather than escalates the original concern. Connector
  Experience's original approval and its preference for immediate inclusion
  are recorded and respected as the sponsoring team's view; the original
  decision owner weighed it against the fuller evidence the other four reviews
  supplied specifically because they, not Connector Experience, hold the
  interfaces that evidence concerns. The PM's re-direction reconciles both:
  inclusion proceeds, on the sequenced, corrected, spike-first plan the four
  reviewers specified.
- **Superseded by:** Not applicable.

## Follow-on goals

| Outcome or objective | Accountable team | Supporting teams and interaction modes | Activation condition |
| --- | --- | --- | --- |
| Resolve the decoder single-pass-vs-second-parse question and define the encoding-normalization rule (scoping spike) | Remote Runtime | Engineering Enablement (Facilitation) | This RFC accepted (2026-07-21). Runs as the gating first work item of the `0.10.0` implementation goal below; no separate goal activation. |
| Implement `response_next` pagination and ship it in `0.10.0` (schema, compiler, generalized Runtime validator and pagination-state entry point, `BaseDomain` classification and property test, `EXPLAIN` arm and differential test, corrected fixture-coverage set, adoption in `connectors/rickandmorty`'s `character_search` relation) | Connector Experience | Remote Runtime (Collaboration), Relational Semantics (Collaboration), Query Experience (X-as-a-Service for `EXPLAIN`), Engineering Enablement (Facilitation) | This RFC accepted as the `0.10.0` implementation authority; the scoping spike above complete; PM-approved product goal activates delivery. The active goal at `docs/goals/public-connector-authoring-candidate.md` does not absorb this work — `0.9.0`'s guardrail forbids expanding capability inside that goal. A new `0.10.0` goal brief must be drafted (under `$draft-product-goal`) before implementation begins. |
| Graduate `response_next` from `accepted_candidate_revisions` into the schema-closed `pagination_strategies.rest` set | Connector Experience | Engineering Enablement (Facilitation, for the freeze lifecycle) | `0.10.0` ships with the live schema/decoder/IR/switches/test-oracle evidence. Graduation is mechanical: a new freeze snapshot is produced for `0.10.0`'s release view, and the candidate-revision entry is removed because the strategy is now part of the closed set proper. |

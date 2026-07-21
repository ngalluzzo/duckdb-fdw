# Goal: Body-signaled REST pagination

Follow `docs/PRODUCT_DELIVERY.md`.

Status: **Active**. [RFC 0016](../rfcs/0016-decide-body-signaled-rest-pagination.md)
is Accepted (2026-07-21) and is this goal's authority. The `1.0.0` candidate
freeze at [`release/1.0.0/freeze.json`](../../release/1.0.0/freeze.json)
records `response_next` under `accepted_candidate_revisions` until this goal
ships and graduates the strategy into the schema-closed
`pagination_strategies.rest` set.

## PM brief

### Outcome

For connector authors targeting REST APIs that embed pagination continuation
URLs in the response body (a common shape — Rick and Morty, many others),
enable declaring the `response_next` strategy so the v1 contract represents
the API's native pagination rather than degrading to single-page `disabled`.

### Why now

The `0.9.0` Rick and Morty trial produced direct evidence of the gap: a second
real package could not represent its API's `info.next` body-URL pagination
shape and fell back to `disabled`. RFC 0016 accepted the reconstruct-and-
verify design after the product manager re-prioritized `1.0.0` to require
≥10 supported API providers (two exist as of `0.9.0`), dissolving the freeze-
pressure rationale that originally deferred the design. `0.10.0` is now the
vehicle, not a post-`1.0.0` `MINOR`: locking in the design while the
evidence is fresh and before more packages accumulate that need this shape.

### Product guardrails

- Must: preserve every invariant `link_next` enforces today — sequential,
  mutable, exact-operation-origin-and-path continuation, bounded by
  `max_pages_per_scan` and existing per-page/per-scan ceilings, no ordering,
  snapshot, parallelism, resume, deduplication, retry, or cache guarantee.
- Must: preserve the reconstruct-and-verify trust model — a body-extracted
  URL is a verified signal that must match the locally reconstructed
  expectation, never a dereferenced fetch target. A malicious or malformed
  body can terminate or fail the scan; it cannot redirect off the declared
  origin.
- Must not: weaken the schema-closed set, the `1.0.0` freeze boundary, or
  any existing package's behavior. `response_next` is strictly additive.
- Preserve: RFC 0009/0012/0013/0014's accepted decisions; every invariant
  in `AGENTS.md`; the fail-closed behavior of every exhaustive switch
  (`std::logic_error` is replaced with a typed `RESPONSE_NEXT_URL` arm, not
  silenced).

### Success signals

- A connector author declares `pagination: strategy: response_next` with
  `next_url_path: $.info.next` (and the shared link-style fields) against a
  multi-page REST API; the package validates, compiles, explains, fixture-
  tests, and loads through the identical `0.9.0` path as `link_next`.
- `EXPLAIN` on the resulting scan reports a `response_next`/`response_url`
  pagination fact alongside the existing `disabled`/`link_header`/
  `graphql_cursor` values, with `next_url_path` body content redacted.
- `connectors/rickandmorty`'s `character_search` relation switches from
  `disabled` to `response_next` and traverses multiple pages through the
  same DuckDB-loadable path the GitHub `link_next` relation already uses.
- A malicious fixture supplying a body URL that diverges from the declared
  operation origin/path/query fails the scan with the existing
  `pagination.next` diagnostic shape — never silently redirects.

### Reserved product decisions

- None beyond the default ownership in `AGENTS.md`. Versioning (`0.10.0`
  minor, not `0.9.x` patch) was confirmed during RFC 0016's acceptance.

## Agent commitment

### Observable interpretation

A connector author who has read only `docs/CONNECTOR_SPECIFICATIONS.md` and
never seen the GitHub package writes a `response_next` declaration for a real
multi-page REST API (Rick and Morty's `info.next` shape). The package
validates, compiles, explains, fixture-tests, and loads through
`duckdb_api_load_connector` exactly as `link_next` already does; DuckDB
returns all rows across pages. A reviewer reads `EXPLAIN` output that names
the strategy and observes that body-extracted continuation URLs are validated
against the declared origin and path, never dereferenced as fetch targets.

### Acceptance evidence

- Demonstration: load a `response_next`-declared relation (the Rick and Morty
  `character_search` after adoption, or a fixture relation against a recorded
  multi-page transcript), query it through DuckDB, and observe all rows
  across pages returned through the same path `link_next` already uses.
- Automated oracle:
  - Fixture-coverage variant set mirroring `link_next`'s
    (`first_page`, `multi_page`, `termination`, `encoded_target`,
    `malformed_target_rejected`, `replayed_target_rejected`,
    `max_pages_exhausted`) **plus** the new `next_field_wrong_type_rejected`
    category `link_next` structurally cannot have (justified by GraphQL
    cursor pagination's `missing_cursor_rejected` precedent).
  - A differential test proving identical behavior between a header-sourced
    and a body-sourced target string given the same underlying page sequence,
    including at least one case where the two are byte-different-but-
    equivalent encodings of the same logical URL (JSON `\uXXXX` escaping
    versus percent-encoding).
  - A Relational Semantics property test proving `response_next`'s
    `BaseDomain` classification behaves identically to `link_next`'s under
    the same fixture rows.
  - A Query-owned differential test mirroring
    `test/cpp/query/duckdb/graphql_adapter_contract_tests.cpp` that asserts
    the new `EXPLAIN` value, the identical-to-`link_header` row shape, and
    redaction of `next_url_path` body content.
- Quality gates: `make build`, `make test`, `make demo`; the existing
  package/fixture/coverage gates extended to the new strategy; the
  source-identity, native-dependency, agent-asset, public-inventory, and
  contract-freeze gates; the fresh-root `make verify` for release evidence.
- Independent review: `$topology-consult` implementation-exit audit
  confirming each of the four team surfaces (Connector Experience, Remote
  Runtime, Relational Semantics, Query Experience) ships its bounded
  extension without consuming another team's internals; `$adversarial-
  review` covering relational correctness, network policy, replay safety,
  and lifecycle (the change touches pagination, runtime, and credentials-
  adjacent code paths).

### Contract and invariant impact

- Schema (`connector-package-v1.schema.json`): new `responsePagination` def
  + `oneOf` branch; the freeze's `pagination_strategies.rest` widens to
  `{disabled, link_next, response_next}` when `0.10.0` ships.
- Compiler (`package_rest_schema.cpp`, `package_operation_compiler.cpp`):
  new `response_next` decoder branch and IR compilation.
- Compiled IR (`CompiledPaginationStrategy::RESPONSE_NEXT_URL`,
  `CompiledPagination`): new strategy token and `next_url_path` extractor
  field.
- Relational Semantics: `PlanBaseDomain`, `ValidatePagination`, and the
  semantics `PaginationStrategyName` (`src/semantics/scan_plan_explain.cpp`)
  each gain a `RESPONSE_NEXT_URL` arm.
- Query Experience: `PaginationStrategyName`/`AddPaginationFacts`
  (`src/query/duckdb/scan_plan_explanation.cpp`) gains the `response_next`
  arm.
- Runtime (`src/runtime/pagination/`): `LinkPaginationState::Advance`
  sibling entry point for body-sourced targets; generalized
  `ValidateNextTarget` (already source-agnostic) wired to the new state
  machine entry; per-spike encoding-normalization rule applied before
  byte-exact comparison.
- `docs/RUNTIME_CONTRACTS.md`: documents the generalized target-validation
  rule covering both header and body sources.
- `release/1.0.0/freeze.json` and `release/1.0.0/freeze.md`: re-cut for
  `0.10.0`'s release view; the `response_next_rest_pagination`
  candidate-revision entry **graduates** into `pagination_strategies.rest`
  and is removed from `accepted_candidate_revisions`.
- Invariants preserved: `D -> R` predicate safety; exactly-one-owner
  residual; limit/offset after filtering and ordering; ordinary bind and
  planning network-free; connector policy narrows host policy; sequential
  pagination; replay-safe single attempt; immutable plans and snapshots;
  strict lossless conversion.

### Team and RFC routing

- Accountable stream: Connector Experience (acceptance ends with a connector
  author validating, compiling, explaining, fixture-testing, and loading a
  `response_next` package).
- Remote Runtime — **Collaboration.** Owns the scoping spike (decoder
  single-pass-vs-second-parse and encoding-normalization rule) and the
  generalized validator with its sibling pagination-state entry point.
  Exit when the generalized validator passes the full
  malformed/replay/cross-origin fixture oracle for both header- and body-
  sourced targets, including at least one encoding-divergence case, without
  Connector Experience needing transport-internal knowledge.
- Relational Semantics — **Collaboration.** Owns the `BaseDomain`-equivalence
  decision and property test, plus the three exhaustive switch arms. Exit
  when the property test passes and each arm is covered by direct test
  evidence.
- Query Experience — **X-as-a-Service.** Owns the `EXPLAIN` switch arm and
  the differential test. Exit when the new `EXPLAIN` value and row shape
  are self-maintained by Query-owned tests without Connector-Experience
  coordination.
- Engineering Enablement — **Facilitation.** Owns the freeze-graduation
  mechanics (the candidate-revision entry moves into the closed set when
  `0.10.0` ships) and the fixture-coverage variant set. Exit when the
  freeze gate and mutation suite tolerate the graduation cleanly.
- RFC: [RFC 0016](../rfcs/0016-decide-body-signaled-rest-pagination.md) is
  Accepted and is this goal's authority. No other RFC gate is open.

### Unknowns and first trial

- Unknown: does extracting `next_url_path` from the decoded body require a
  second parse, or can it share the decoder's existing single pass? And
  what is the exact normalization rule for comparing a JSON-`\uXXXX`-
  unescaped body-extracted target against the percent-encoded
  reconstructed expectation? Both questions were raised by the original
  Remote Runtime review and are decision-critical for implementation.
- Trial: the scoping spike is the gating first work item (delivery-path
  step 1 below). It produces a written decision recorded in
  `docs/RUNTIME_CONTRACTS.md` and as a comment at the validator site, not
  a public-behavior change. Implementation does not commit until the spike
  is complete.

### Delivery path

1. **Scoping spike (Remote Runtime, gating).** Resolve the decoder
   single-pass-vs-second-parse question and define the encoding-normalization
   rule. Output: a written decision recorded in `docs/RUNTIME_CONTRACTS.md`
   and at the validator site. No public-behavior change.
2. **Schema and compiled IR (Connector Experience).** JSON schema
   `responsePagination` def + `oneOf` branch; C++ decoder
   `DecodeRestPaginationSchema` branch; `CompiledPaginationStrategy` enum
   and `CompiledPagination` field; `package_operation_compiler.cpp` IR
   compilation. At this point a package declaring `response_next` validates
   and compiles but cannot yet plan or execute.
3. **Parallel implementation surfaces.**
   - Relational Semantics: three switch arms + `BaseDomain`-equivalence
     decision + property test.
   - Query Experience: `EXPLAIN` arm + differential test.
   - Remote Runtime: `Advance` sibling entry point + generalized
     `ValidateNextTarget` wired in + per-spike normalization applied.
   These are independent once step 2's IR exists and can be developed in
   parallel.
4. **Fixture oracle (Engineering Enablement facilitates).** New
   `response_next` fixture-coverage variant set with the
   `next_field_wrong_type_rejected` category; the differential header/body
   target test; the property test.
5. **Adoption.** `connectors/rickandmorty`'s `character_search` relation
   switches from `disabled` to `response_next`; the package version bumps
   `MINOR` under RFC 0013.
6. **Release cut (`0.10.0`).** Version bump
   (`extension_config.cmake`, `CMakeLists.txt`,
   `scripts/verify-source-identities.py`, release pins);
   `release/1.0.0/freeze.json` re-cut for `0.10.0`'s release view with the
   candidate-revision entry graduated; new `release/0.10.0/` directory with
   `pins.json` and `public_contract.json`; release notes; full
   `make verify` from a fresh build root.

This is an agent-owned working plan, not a durable product commitment.

## Completion record

_To be filled in when delivery completes._

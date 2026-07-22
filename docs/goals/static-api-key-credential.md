# Goal: Static API-key credential

Follow `docs/PRODUCT_DELIVERY.md`.

Status: **Active**. [RFC 0018](../rfcs/0018-add-static-api-key-credential.md)
is Accepted (2026-07-22) and is this goal's authority. The `1.0.0` candidate
freeze at [`release/1.0.0/freeze.json`](../../release/1.0.0/freeze.json) will
record `api_key` under `accepted_candidate_revisions` until this goal ships
and graduates the kind into the closed credential enumeration, alongside a
correction to the `authenticators_beyond_anonymous_and_capability_scoped_bearer`
mandatory exclusion that `api_key` otherwise textually contradicts.

## PM brief

### Outcome

For connector authors targeting APIs that gate access with a static API key
(as a fixed HTTP header or a fixed URL query parameter) rather than a bearer
token, enable declaring an `api_key` credential kind alongside the existing
`bearer` kind, so those packages can be authored and loaded through the same
`duckdb_api/v1` path.

### Why now

An architecture maturity assessment found `duckdb_api/v1` supports `bearer`
credentials only, ahead of `ROADMAP.md`'s `1.0.0` release gate requiring at
least 10 connector providers (two exist today). A large share of free/public
REST APIs gate access with a static key instead of a bearer token. Closing
this gap before choosing connector provider #3 avoids constraining that
choice to bearer-only APIs, the same pattern RFC 0016/`0.10.0` followed for
the pagination gap Rick and Morty surfaced.

### Product guardrails

- Must: preserve every invariant `bearer` enforces today — exact
  destination/authenticator/placement validation before decoration; the
  credential value never entering package source, compiled explanation,
  `ScanPlan`, diagnostics, fixtures, digests, or catalog introspection;
  connector policy narrowing but never widening host policy;
  `ScanAuthorization`'s move-only, non-copyable, non-renderable shape.
- Must not: introduce OAuth flows, token acquisition, refresh, expiry, or any
  dynamic/computed credential value; collapse or rename the existing
  `ScanAuthorization::Bearer`/`GithubUserBearer` factories (RFC 0018's Query
  Experience review found this would ripple across 40+ existing test call
  sites and remove an internal type-tag safety check for no required
  benefit).
- Preserve: RFC 0009/0012/0013/0014/0016's accepted decisions; every
  invariant in `AGENTS.md`; the fail-closed behavior of every exhaustive
  switch this touches (`std::logic_error` is replaced with real `API_KEY`
  arms, not silenced); the existing `bearer` schema branch and every
  accepted package's (`connectors/github`, `connectors/rickandmorty`)
  behavior, byte-for-byte.

### Success signals

- A connector author declares `kind: api_key` with `placement: header`
  (author-named header, e.g. `X-Api-Key`) or `placement: query`
  (author-named parameter, e.g. `api_key`) in a manifest, and it validates,
  compiles, explains, fixture-tests, and loads exactly like `kind: bearer`
  does today.
- `EXPLAIN` on the resulting scan reports an `api_key` authenticator fact and
  the declared header/query-parameter name, with the credential value never
  rendered — matching bearer's existing explanation and redaction shape.
- A package declaring an `api_key` header name that collides with an
  existing declared header, or a query-parameter name that collides with an
  existing declared query field, fails closed with a diagnostic rather than
  silently shadowing the other field.
- `connectors/github` (bearer-only) and `connectors/rickandmorty`
  (anonymous-only) are unaffected — no version bump, no behavior change.

### Reserved product decisions

- None beyond the default ownership in `AGENTS.md`. Both reserved decisions
  this goal depends on — supporting both header and query placement in the
  first cut, and author-declared (not fixed) header/parameter names — were
  confirmed by the product manager during this goal's shaping and carried
  into RFC 0018's acceptance.

## Agent commitment

### Observable interpretation

A connector author who has read only `docs/CONNECTOR_SPECIFICATIONS.md` and
never seen the GitHub package writes an `api_key` credential declaration
(header or query placement) for a real API that requires nothing but a
static key. The package validates, compiles, explains, fixture-tests, and
loads through `duckdb_api_load_connector` exactly as `bearer` already does;
DuckDB returns typed rows from an authenticated request. A reviewer reads
`EXPLAIN` output naming the credential kind and declared placement name and
confirms the value itself is never rendered anywhere — plan, diagnostics,
fixtures, digests, or catalog introspection.

### Acceptance evidence

- Demonstration: a new offline fixture package (parallel to `connectors/
  github`'s bearer fixtures) declares an `api_key` credential in both
  placements, loads through `duckdb_api_load_connector`, and a controlled
  fixture-transport observation proves the exact header or query parameter
  carries the value — never `Authorization`, never in diagnostics or
  `EXPLAIN`.
- Automated oracle:
  - Schema/compiler fixtures for `kind: api_key` using the closed
    sibling-`$defs` shapes (`apiKeyHeaderCredential`/`apiKeyQueryCredential`):
    valid header, valid query, missing `header_name`/`query_param`,
    wrong-placement field combination, invalid name grammar,
    policy-widening.
  - A Relational Semantics fixture proving an `api_key` relation both plans
    successfully (not rejected by `ValidateAuthentication`) and that
    `AuthenticatorName`/`PlacementName` render `api_key` for both placements
    without reaching the unhandled-enum crash path.
  - A Remote Runtime admission/execution fixture proving exact placement and
    full redaction for both header and query, structurally parallel to
    bearer's existing authorization tests, including the generalized
    `SameHeaders`-equivalent pre-authorization check.
  - A query-parameter-name-collision fixture (declared query name equals an
    existing fixed/input/conditional query field) proving admission-time
    rejection via Remote Runtime's existing `TryCopyPermanentQuery`/
    `TryCopyLegacyQuery` name-set mechanism, **plus** a symmetric
    header-name-collision fixture (declared header name equals an existing
    fixed header) exercising `CompileHeaders`'s existing case-insensitive
    uniqueness check.
  - A compatibility fixture proving `connectors/github` and
    `connectors/rickandmorty` are unaffected.
  - Mutation-test coverage in `test/python/contract_freeze_tests.py` for the
    new `accepted_candidate_revisions` entry and the corrected
    `authenticators_beyond_anonymous_and_capability_scoped_bearer` exclusion.
- Quality gates: `make build`, `make test`, `make demo`;
  `scripts/verify-source-identities.py`;
  `scripts/verify-public-surface-inventory.py` and its test;
  `scripts/verify-contract-freeze.py` and its test; the fresh-root
  `make verify` for release evidence.
- Independent review: `$topology-consult` implementation-exit audit
  confirming each of the four team surfaces (Connector Experience, Remote
  Runtime, Relational Semantics, Query Experience) ships its bounded
  extension without consuming another team's internals; `$adversarial-
  review` covering credential redaction, the query-placement collision and
  leakage risk RFC 0018 identified, and lifecycle behavior.

### Contract and invariant impact

- Schema (`connector-package-v1.schema.json`): `$defs/credential` gains two
  new closed sibling shapes, `apiKeyHeaderCredential` and
  `apiKeyQueryCredential`, referenced from its `oneOf` — matching the
  existing REST-pagination sibling-`$defs` idiom, not a conditionally
  required-field shape.
- Compiler (`package_manifest_schema.cpp`'s `DecodeCredential`,
  `package_http_schema.cpp`'s `IsHeaderName`/`IsHeaderValue`,
  `package_rest_schema.cpp`'s `IsQueryName`): new discriminated decode
  branch mirroring `DecodeAuth`/`DecodeQueryField`'s existing pattern.
- Compiled IR (`scan_plan.hpp`): `PlannedAuthenticator` gains `API_KEY`;
  `PlannedCredentialPlacement` gains `HEADER_NAMED`/`QUERY_NAMED`, each
  carrying the compiled author-declared name as plan data.
- Relational Semantics: `ValidateAuthentication`
  (`scan_planner_validation.cpp:341-374`) and `ScanPlanBuilder::Build`
  (`scan_planner.cpp:91-104`) must read the compiled connector's own
  credential kind instead of hard-coding `BEARER`/`AUTHORIZATION_HEADER`;
  `AuthenticatorName`/`PlacementName` (`scan_plan_explain.cpp`) gain arms.
- Remote Runtime: `HasAuthentication`/`HasSupportedRestAuthority`
  (`rest_authority_admission.cpp`) and GraphQL's `HasAuthority`
  (`graphql_plan_admission.cpp`) generalize their boolean output to a
  credential-requirement value; new `ApiKeyAuthenticator`
  (`src/runtime/authentication/`); `SameHeaders`'s hard-coded
  `"authorization"` exclusion (`bearer_authenticator.cpp:16-27`)
  generalizes to an arbitrary declared header name; the query-name-collision
  check already owned by `rest_request_materialization.cpp`'s
  `TryCopyPermanentQuery`/`TryCopyLegacyQuery` extends to the credential's
  name; `FormUrlEncode` needs exporting for reuse; `ScanAuthorization`
  (`authorization.hpp`) gains a third `API_KEY` alternative.
- Query Experience: `ResolveDuckdbApiSecret`
  (`secret_integration.cpp:156`) gains an additive kind-neutral credential
  factory; `CompiledQueryRegistrationView`'s public shape is unchanged —
  Runtime dispatches placement from the plan's `PlannedAuthenticator`.
- `docs/CONNECTOR_SPECIFICATIONS.md`: Credentials section documents the
  `api_key` shapes and the name-collision rule.
- `docs/RUNTIME_CONTRACTS.md`: documents the generalized authenticator/
  placement shape and the new `ApiKeyAuthenticator`.
- `release/1.0.0/freeze.json` and `release/1.0.0/freeze.md`: `api_key` is
  added as an `accepted_candidate_revisions` entry; the mandatory exclusion
  `authenticators_beyond_anonymous_and_capability_scoped_bearer` is reworded
  or scoped so it no longer textually contradicts the accepted revision.
- Invariants preserved: destination/placement validation before decoration;
  credential value never in package source, plans, diagnostics, fixtures,
  digests, or catalog introspection; connector policy narrows host policy;
  strict lossless conversion; immutable plans and generations.

### Team and RFC routing

- Accountable stream: Connector Experience (acceptance ends with a connector
  author declaring, validating, compiling, explaining, fixture-testing, and
  loading an `api_key` package).
- Relational Semantics — **Collaboration.** Owns `ValidateAuthentication`
  and `ScanPlanBuilder::Build`'s credential-kind-aware planning plus the two
  exhaustive explain-switch arms. Exit when a fixture proves an `api_key`
  relation plans successfully and explains correctly for both placements.
- Remote Runtime — **Collaboration.** Owns the generalized admission
  booleans, the new `ApiKeyAuthenticator`, the `SameHeaders` generalization,
  the query-name-collision extension, and the additive `ScanAuthorization`
  factory Query Experience calls. Exit when the generalized admission and
  authenticator pass the full bearer-equivalent fixture oracle for both
  placements, with the credential's query name never entering
  `query_bindings`/`query_parameters`.
- Query Experience — **X-as-a-Service.** The design is already confirmed by
  RFC 0018 review (additive factory, no registration-view change). Owns
  wiring `ResolveDuckdbApiSecret` to the new factory. Exit when this is
  proven end to end without renaming existing `Bearer`/`GithubUserBearer`
  call sites.
- Engineering Enablement — **Facilitation.** Owns the header- and
  query-name-collision fixture-coverage categories and the
  `release/1.0.0/freeze.json` exclusion correction plus its mutation-test
  coverage. Exit when Connector Experience, Remote Runtime, Relational
  Semantics, and Query Experience maintain the corrected oracle
  independently.
- RFC: [RFC 0018](../rfcs/0018-add-static-api-key-credential.md) is
  Accepted and is this goal's authority. No other RFC gate is open.

### Unknowns and first trial

None identified. `api_key` is structurally parallel to the already-implemented
and proven `bearer` kind, and RFC 0018's five-team review already resolved
every open design question (schema idiom, the two additional Relational
Semantics sites, the fourth Remote Runtime site and the real location of the
query-collision check, and the Query Experience factory design) before
acceptance. No bounded evidence trial is needed before implementation.

### Delivery path

1. **Schema and compiled IR (Connector Experience).** `$defs/credential`
   gains `apiKeyHeaderCredential`/`apiKeyQueryCredential`; `DecodeCredential`
   gains the discriminated decode branch; `PlannedAuthenticator`/
   `PlannedCredentialPlacement` gain their new values. At this point a
   package declaring `api_key` validates and compiles but cannot yet plan.
2. **Relational Semantics (Collaboration).** `ValidateAuthentication` and
   `ScanPlanBuilder::Build` read the compiled connector's credential kind;
   `AuthenticatorName`/`PlacementName` gain arms; fixture and property
   coverage proves successful planning and explanation.
3. **Remote Runtime (Collaboration).** Generalize the three
   `requires_bearer` admission booleans to a credential-requirement value;
   add `ApiKeyAuthenticator`; generalize `SameHeaders`; extend the existing
   query-name-collision check; export `FormUrlEncode`; add the
   `ScanAuthorization::API_KEY` alternative and the additive kind-neutral
   resolution factory Query Experience will call.
4. **Query Experience (X-as-a-Service).** Wire `ResolveDuckdbApiSecret` to
   the new additive factory.
5. **Fixture oracle (Engineering Enablement facilitates).** Header- and
   query-name-collision coverage categories; the compatibility fixture
   proving GitHub and Rick and Morty are unaffected; `release/1.0.0/
   freeze.json` exclusion correction and mutation-test coverage.
6. **Docs and release accounting.** Update `docs/CONNECTOR_SPECIFICATIONS.md`
   and `docs/RUNTIME_CONTRACTS.md`; confirm `ROADMAP.md`'s `0.11.0` section
   (already added at RFC acceptance) matches delivered behavior.

This is an agent-owned working plan, not a durable product commitment.

## Completion record

### Delivered

`kind: api_key` is implemented end to end for `duckdb_api/v1`: two closed
schema shapes (`apiKeyHeaderCredential`/`apiKeyQueryCredential`), the
discriminated `DecodeCredential` compiler branch, `CompiledAuthenticator::
API_KEY`/`CompiledCredentialPlacement::HEADER_NAMED`/`QUERY_NAMED` with a
carried `PlacementName()`, credential-kind-aware planning in
`ValidateAuthentication` and `ScanPlanBuilder::Build` (both previously
hard-coded to bearer), the corresponding `EXPLAIN`/snapshot arms in both
Relational Semantics and Connector Experience, a new `ApiKeyAuthenticator`
(header and query placement) alongside the unchanged `BearerAuthenticator`,
a generalized `RequiredCredential`-based admission model across REST
single-response and paginated profiles, an additive kind-neutral
`ScanAuthorization::Credential()` factory that Query's
`ResolveDuckdbApiSecret` now calls (Query never learns the target relation's
credential kind), and header-name/query-name collision checks against a
relation's other declared fields. A GraphQL-protocol relation with an
`api_key` credential is rejected at compile time with a precise diagnostic
rather than failing silently at execution. `connectors/github` and
`connectors/rickandmorty` are unaffected (no version bump; full existing
test suite green).

### Evidence

- `make build`/`make test` (debug profile): full suite green, including new
  compile-time validation coverage (`connector_catalog_contract_tests.cpp`),
  `ScanAuthorization::Credential()` capability tests
  (`authorization_contract_tests.cpp`), and a real admission-and-placement
  test proving `ApiKeyAuthenticator` places the declared header/query value
  correctly and that the query placement never enters
  `QueryParameters()`/`EXPLAIN` before authorization
  (`rest_plan_admission_tests.cpp`).
- `scripts/verify-source-identities.py`, `scripts/verify-native-product-sources.py`
  (via `make build`/`make verify`'s wrapped checks), `scripts/verify-contract-freeze.py`,
  `test/python/contract_freeze_tests.py`, `scripts/verify-public-surface-inventory.py`,
  `scripts/validate-agent-assets.py`: all pass.
- Two independent adversarial-review passes (`$adversarial-review`, Transport/policy
  and Lifecycle/validation-consistency perspectives) — see Material decisions below.

### Material decisions and deviations

- **Critical defect found and fixed during adversarial review:** the initial
  query-placement admission design reserved the credential's raw 8 KiB byte
  limit against the 8 KiB total target-length budget, which can never fit —
  query-placed `api_key` credentials admitted *zero* scans in the
  pre-review implementation, silently failing every query with a generic
  policy error. Fixed by removing the flawed pre-reservation (admission now
  only checks the name-collision invariant) and relying on
  `ApiKeyAuthenticator::AppendApiKey`'s existing, already-correct
  authorization-time byte/length check — the same design bearer's header
  placement already uses (no admission-time reservation, a real check when
  the value is actually available). This is recorded here because it
  reached the working tree before being caught, not because it shipped.
- **Symmetric header-name collision check added:** review found the initial
  implementation checked query-parameter-name collisions but not
  header-name collisions, asymmetric with the RFC's own stated requirement.
  Fixed by adding the equivalent check against `request.headers` for
  `HEADER_NAMED` placement.
- **Bundled, unrelated pre-existing defect fixes** (not part of RFC 0018,
  but discovered while getting `make test` green and directly analogous to
  work already underway in this area): `CompiledPagination`'s constructors
  and `ValidatePagination` both failed to reject a page-size value declared
  without a page-size parameter name (RFC 0017 gap); a stale `0.9.0`
  version-string expectation in `test/sql/duckdb_api.test`,
  `test/python/source_demo_contract.py`,
  `test/python/source_demo_contracts/graphql_repository.py`, and four
  `examples/*.py` files left over from the `0.10.0` release cut. Both are
  tracked as pre-existing, out-of-scope defects incidentally fixed here
  because they blocked verifying this goal's own changes.
- **GraphQL scope, confirmed conservative and now diagnosed:** `api_key` is
  REST-only (GraphQL admission remains bearer-or-anonymous, unmodified).
  Review confirmed this fails clean (no crash, no silent success) but
  initially with no compile-time diagnostic; a `CompileAuthentication` check
  was added rejecting the combination with a precise
  `UNSUPPORTED_DECLARATION` diagnostic instead of leaving the author to
  discover it only via a generic runtime policy error.

### Product options discovered

- A full `credential_kinds` mechanical freeze section (analogous to
  `pagination_strategies`, with its own schema-cross-check function and
  mutation tests) was not built — no existing `MANDATORY_EXCLUSIONS` check
  required it, and the simpler `exclusions`-list rewording was judged
  sufficient for this goal. Worth doing if a third credential kind is ever
  proposed.
- True GraphQL `api_key` support (most plausibly header-only, since GraphQL
  requests have no query-string surface) remains a real, undelivered
  candidate if a future connector needs it.

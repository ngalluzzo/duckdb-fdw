# RFC 0018: Add a static API-key credential kind to duckdb_api/v1

```yaml
rfc: "0018"
title: "Add a static API-key credential kind to duckdb_api/v1"
status: "Accepted"
rfc_type: "Product"
sponsor_team: "Connector Experience"
technical_decision_owner: "Lead agent"
product_approver: "Nic Galluzzo"
authors:
  - "Lead agent"
required_reviewers:
  - "Connector Experience"
  - "Relational Semantics"
  - "Remote Runtime"
  - "Query Experience"
  - "Engineering Enablement"
affected_teams:
  - "Connector Experience"
  - "Relational Semantics"
  - "Remote Runtime"
  - "Query Experience"
  - "Engineering Enablement"
linked_outcome_or_objective: "Proactive capability-gap closure ahead of ROADMAP.md's 1.0.0 gate (>=10 connector providers; 2 exist today), so the next connector author is not constrained to bearer-only APIs."
supersedes: "Not applicable"
```

## Summary

`duckdb_api/v1` admits exactly one credential kind, `bearer`, with a fixed
`Authorization` header placement. This RFC proposes a second static credential
kind, `api_key`, placed either as an author-named fixed HTTP header or an
author-named fixed URL query parameter, reusing bearer's existing
redaction, destination, and policy-narrowing model without introducing OAuth,
token refresh, or any dynamic/computed credential value.

## Sponsorship and context

- **RFC type:** Product. The decision extends `duckdb_api/v1`'s closed
  connector-package credential contract, a public, author-facing surface.
- **Sponsoring team:** Connector Experience, which owns
  `docs/CONNECTOR_SPECIFICATIONS.md`'s Credentials section and the package
  schema/compiler this decision extends.
- **Linked outcome or objective:** `ROADMAP.md`'s `1.0.0` release gate commits
  to at least 10 connector providers (2 exist: GitHub, Rick and Morty). An
  architecture maturity assessment found `duckdb_api/v1` supports bearer
  credentials only, and a large share of free/public REST APIs — the kind of
  low-risk, fixture-friendly API this project has picked so far — gate access
  with a static API key instead. Closing this gap before choosing provider #3
  avoids constraining that choice to bearer-only APIs, the same pattern RFC
  0016 followed for the pagination gap Rick and Morty surfaced.
- **Why now:** Deciding this before a third package is authored means the
  provider choice is not artificially narrowed by a credential-shape gap
  discovered mid-authoring, the way `response_next` was discovered mid-`0.9.0`.

## Problem

`docs/CONNECTOR_SPECIFICATIONS.md`'s Credentials section and its backing
schema (`src/connector/package/assets/connector-package-v1.schema.json`,
`$defs/credential`) declare `kind`, `secret_field`, and `placement` as JSON
Schema `const` values — not an enum, a single fixed literal each
(`"bearer"`, `"token"`, `"authorization_header"`). The compiler
(`DecodeCredential` in `src/connector/package/package_manifest_schema.cpp:66-82`)
enforces this with three `RequireValue(..., PackageDiagnosticCode::
UNSUPPORTED_DECLARATION, ...)` calls. A connector author targeting an API that
requires `X-Api-Key: <value>` or `?api_key=<value>` instead of
`Authorization: Bearer <value>` cannot express it at all today; the package
fails closed at the schema phase.

This is not a defect — RFC 0013 scoped credentials to what GitHub, the only
real package it had evidence against, actually needed. It is the same shape of
gap RFC 0016 decided for pagination: a real, common API shape the closed v1
contract cannot represent, requiring "a later accepted contract" per
`docs/CONNECTOR_SPECIFICATIONS.md`'s own compatibility-boundary rule.

The credential-kind boundary reaches further than the schema. Confirmed by
direct source inspection:

- `src/include/duckdb_api/scan_plan.hpp:168-169` declares
  `enum class PlannedAuthenticator { NONE, BEARER };` and
  `enum class PlannedCredentialPlacement { NONE, AUTHORIZATION_HEADER };` —
  genuinely closed enums, not open for a new arm without a source change.
- `src/semantics/scan_plan_explain.cpp`'s `AuthenticatorName` (line 303) and
  `PlacementName` (line 313) are exhaustive switches over those enums; each
  throws `std::logic_error` on an unhandled value (confirmed pattern:
  `AuthenticatorName` at line 310, `throw std::logic_error("scan plan
  contains an unknown authenticator")`). This is the identical fail-closed
  shape RFC 0016 found in Relational Semantics for pagination strategy — a
  compiled generation using an unhandled enum value crashes rather than
  producing a typed diagnostic until this switch gains an arm.
- Runtime admission carries authentication as a plain `bool requires_bearer`
  in three places, not a kind-plus-placement descriptor:
  `HasAuthentication`/`HasSupportedRestAuthority`
  (`src/runtime/execution/rest_authority_admission.cpp:9-33`),
  `AdmittedRestRequestProfile`/`AdmittedPaginatedRestRequestProfile`
  (`src/include/duckdb_api/internal/runtime/execution/http_plan_admission.hpp:54,
  72, 107, 135`), and GraphQL's parallel `HasAuthority`/
  `AdmittedGraphqlRequestProfile` (`src/runtime/execution/
  graphql_plan_admission.cpp:283-341`,
  `src/include/duckdb_api/internal/runtime/execution/
  graphql_plan_admission.hpp:60, 67, 88`).
- `BearerAuthenticator::AppendBearer`
  (`src/runtime/authentication/bearer_authenticator.cpp:65-92`) hard-codes the
  header name `"Authorization"` and the `"Bearer "` value prefix. There is no
  generic "decorate this request with this placement" service today; a new
  credential kind needs its own authenticator, not a parameter to this one.
- `ScanAuthorization` (`src/include/duckdb_api/authorization.hpp:29-67`) is a
  closed two-alternative capability (`ANONYMOUS`, `BEARER`); a third
  alternative requires a source change to this move-only opaque type, not a
  data field.
- **Confirmed by Relational Semantics review, not in the original draft:**
  two additional required-change sites exist beyond explanation.
  `ValidateAuthentication` (`src/semantics/scan_planner_validation.cpp:341-374`)
  throws for any `REQUIRED` relation unless `Authenticator() == BEARER` and
  `Placement() == AUTHORIZATION_HEADER` exactly — a compiled `api_key`
  relation fails at plan construction today, before ever reaching `EXPLAIN`.
  `ScanPlanBuilder::Build` (`src/semantics/scan_planner.cpp:91-104`)
  unconditionally hard-codes `authenticator = PlannedAuthenticator::BEARER`
  and `placement = PlannedCredentialPlacement::AUTHORIZATION_HEADER` rather
  than reading the compiled connector's own `CompiledAuthenticator`/
  `CompiledCredentialPlacement` (`connector_catalog.hpp:113-119`, the same
  closed two-value shape one layer down). Both are genuine plan-construction
  logic, not explanation.
- **Confirmed by Remote Runtime review, not in the original draft:** a
  fourth admission site exists. `BearerAuthenticator`'s internal
  `SameHeaders` (`bearer_authenticator.cpp:16-27`) hard-codes an exclusion
  for header name `"authorization"` (case-insensitive) when checking that a
  request is still in its pre-authorization state; this check runs inside
  all three `IsAdmitted*Request` functions. Header-placement `api_key`
  needs this generalized to exclude an arbitrary author-declared header
  name, not only `Authorization`.
- Query's only package-path secret-to-capability construction site,
  `ResolveDuckdbApiSecret`
  (`src/query/duckdb/secret_integration.cpp:156-163`, declared in
  `src/include/duckdb_api/duckdb_secret.hpp:39`), unconditionally calls
  `ScanAuthorization::GithubUserBearer(std::move(token))` — it has no
  parameter carrying which credential kind the executing relation declared,
  and no branch. (Confirmed distinct from the legacy `0.7.0` compatibility
  bridge: `authorization.hpp:40-44` documents `GithubUserBearer`/
  `GithubUserBearerTokenByteLimit` as a "Bounded 0.7 source-compatibility
  bridge" over the protocol-neutral `Bearer`/`BearerTokenByteLimit` — but the
  real v1 package path, `relation_execution.cpp:75-81`'s
  `OpenAuthorizedStream`, calls this exact same `ResolveDuckdbApiSecret`
  function, so the hard-coded-to-bearer behavior is live on the package path
  today, not only on the deprecated dispatcher.)

No decision-critical feasibility unknown remains: every one of these sites is
an existing, working bearer-only implementation this RFC extends by direct
structural analogy, the same relationship `response_next` had to `link_next`.

## Decision drivers and invariants

- **Must preserve:** every invariant `bearer` currently enforces — exact
  destination/authenticator/placement validation before decoration; the
  credential value never entering package source, compiled explanation,
  `ScanPlan`, diagnostics, fixtures, digests, or catalog introspection
  (`docs/ARCHITECTURE.md`'s execution/authorization section); connector policy
  narrowing but never widening host policy; `ScanAuthorization`'s move-only,
  non-copyable, non-renderable opaque-capability shape.
- **Must enable:** declaring a static API-key credential as a fixed HTTP
  header (author-named, e.g. `X-Api-Key`) or a fixed URL query parameter
  (author-named, e.g. `api_key`), validated, compiled, and executed through
  the same production path as `bearer`.
- **Must not introduce:** OAuth flows, token acquisition, refresh, expiry, or
  any dynamic/computed credential value; a general "arbitrary caller header"
  facility (the header/param *name* is author-declared at compile time, the
  *value* is still resolved from exactly one named DuckDB secret at execution,
  exactly as bearer works today); a credential source other than the existing
  `duckdb_api` DuckDB secret type; retry, cache, or rate-limit-waiting
  behavior tied to credential kind.

## Proposed decision

### The key structural fact this proposal rests on

`bearer` is already "one static secret value, one fixed placement, validated
against one fixed destination set" — `api_key` is the same shape with two
differences: the placement can be a query parameter as well as a header, and
the header/parameter *name* is author-declared instead of fixed to
`Authorization`. Nothing about the value's lifecycle, resolution, or
redaction changes. This RFC proposes generalizing the three already-existing
closed enums/booleans (`PlannedAuthenticator`, `PlannedCredentialPlacement`,
and Runtime's `requires_bearer` booleans) to a second closed alternative
rather than introducing a new capability class.

### Schema and compiler

**Revised by Connector Experience review.** The original draft sketched
`api_key` as one shape with `header_name`/`query_param` as "required iff
placement" siblings. Direct inspection of
`connector-package-v1.schema.json` shows this repository's actual closed-set
idiom is different: REST pagination's `disabledPagination`/`linkPagination`/
`responsePagination` (and `response_next`'s own `$defs`) are three fully
separate closed sibling `$defs`, each with its own `const` discriminator,
referenced through one `oneOf` — not a single shape with conditionally
required fields (`if`/`then`/`dependentRequired` do not appear anywhere in
this schema file today). `test/python/contract_freeze_tests.py`'s existing
`test_schema_rest_closed_set_is_read_from_oneof` mechanically walks exactly
that sibling-`$defs`-per-`oneOf`-branch shape. This RFC therefore proposes
`api_key` as two closed sibling `$defs` — `apiKeyHeaderCredential` (`kind:
api_key`, `placement: header`, required `header_name`) and
`apiKeyQueryCredential` (`kind: api_key`, `placement: query`, required
`query_param`) — referenced from `$defs/credential`'s `oneOf` alongside the
unchanged `bearerCredential` shape, matching the established idiom exactly
rather than introducing a new one:

```yaml
credentials:
  - id: github_token
    kind: bearer
    secret_field: token
    placement: authorization_header
    destinations: [{scheme: https, host: api.github.com, port: 443}]
  - id: service_api_key
    kind: api_key
    secret_field: token
    placement: header          # apiKeyHeaderCredential
    header_name: X-Api-Key     # required only on this sibling shape
    destinations: [{scheme: https, host: example.com, port: 443}]
  - id: service_api_key_query
    kind: api_key
    secret_field: token
    placement: query           # apiKeyQueryCredential
    query_param: api_key       # required only on this sibling shape
    destinations: [{scheme: https, host: example.com, port: 443}]
```

`header_name` reuses `IsHeaderName`/`IsHeaderValue`
(`package_http_schema.cpp`); `query_param` reuses `IsQueryName`
(`package_rest_schema.cpp`) — the two existing author-declared-name
validators, confirmed by review to be the correct precedent (the RFC's
original citation of `package_rest_schema.cpp` for header validation was
imprecise; header-name validation actually lives in `package_http_schema.cpp`).
`DecodeCredential`'s discriminated decode follows the same
`RequireMapping(allowed-superset, required-subset)`-plus-per-branch-`Field()`
pattern `DecodeAuth` (`package_relation_schema.cpp`) and `DecodeQueryField`
(`package_rest_schema.cpp`) already use for their own discriminated shapes —
confirmed by review as a clean, already-proven parallel, not new compiler
machinery. `secret_field` stays a fixed logical field name (`token`), not a
DuckDB secret name or value, exactly as bearer's is. The `bearer` branch's
existing `const` shape is unchanged byte-for-byte — this is additive to the
closed set, not a change to the existing alternative.

### Compiled IR

`PlannedAuthenticator` gains `API_KEY`; `PlannedCredentialPlacement` gains
`HEADER_NAMED` and `QUERY_NAMED`, each carrying the compiled author-declared
name as plan data (not a fixed literal, unlike `AUTHORIZATION_HEADER`).
`AuthenticatorName`/`PlacementName` in `src/semantics/scan_plan_explain.cpp`
gain the corresponding arms; `EXPLAIN` renders the kind and placement name
(e.g. `authenticator:api_key,placement:header:X-Api-Key`) but never the value,
matching bearer's existing explanation shape.

### Runtime execution

`HasAuthentication`/`HasSupportedRestAuthority`
(`rest_authority_admission.cpp`) and GraphQL's `HasAuthority`
(`graphql_plan_admission.cpp`) generalize their `bool requires_bearer` output
parameter to a small closed credential-requirement value (kind plus, for
`api_key`, the placement name) carried into `AdmittedRestRequestProfile`,
`AdmittedPaginatedRestRequestProfile`, and `AdmittedGraphqlRequestProfile` in
place of today's raw boolean field. A new `ApiKeyAuthenticator`
(parallel to `BearerAuthenticator`, in
`src/runtime/authentication/`) consumes `ScanAuthorization`'s API-key
alternative and the admitted profile's declared name:

- **Header placement** appends one header with the author-declared name and
  the raw secret value (no `"Bearer "`-style prefix), subject to the same
  aggregate header-byte budget `AppendBearer` already enforces
  (`request_header_budget.hpp`). **Revised by Remote Runtime review:**
  `BearerAuthenticator`'s internal `SameHeaders` helper
  (`bearer_authenticator.cpp:16-27`), which all three `IsAdmitted*Request`
  pre-authorization checks depend on, hard-codes an exclusion for header name
  `"authorization"` specifically. `ApiKeyAuthenticator` needs its own
  equivalent pre-authorization check excluding its own author-declared header
  name — a fourth site the original draft did not name.
- **Query placement** appends one `form_urlencoded` query parameter with the
  author-declared name to the already-admitted target. **Revised by Remote
  Runtime review, correcting a misattribution in the original draft:** no
  compile-time query-field-name-uniqueness check exists in
  `package_rest_schema.cpp`'s `DecodeRestRequestSchema` today — the real
  uniqueness check lives in Remote Runtime's admission path,
  `rest_request_materialization.cpp`'s `TryCopyPermanentQuery`/
  `TryCopyLegacyQuery`, which each build a `std::set<std::string> names`
  across fixed/input/conditional query bindings. The API-key query
  parameter's name must be checked against that same set at admission time
  (Runtime's responsibility, not solely a compile-time schema concern,
  consistent with this codebase's existing defense-in-depth pattern of every
  admission function re-deriving its own invariants rather than trusting the
  compiler) — a compile-time check in `DecodeCredential` for author
  ergonomics is additionally worthwhile but does not replace the Runtime-side
  check. Separately, `FormUrlEncode` (`rest_request_materialization.cpp`) is
  defined in an anonymous namespace and not exported through that file's
  header; `ApiKeyAuthenticator` needs it exported (or an equivalent function
  added) rather than assuming free reuse. **Critical redaction constraint,
  confirmed sound today but must be preserved:** `EXPLAIN`'s `AppendQuery`
  (`scan_plan_explain.cpp`) walks only `PlannedRestOperation::
  query_parameters` — a documented "Native 0.7 compatibility mirror," never
  the real package-path `query_bindings` field, which `EXPLAIN` never
  renders. Implementation must keep the API-key query-parameter name and
  value entirely out of `query_bindings`/`query_parameters`; folding it into
  that structure merely to reuse the existing collision check would inherit
  `EXPLAIN`'s exposure of that structure and leak the parameter (at minimum
  its name; the risk is architectural, not yet a confirmed value leak) into
  diagnostic output.

`ScanAuthorization` (`authorization.hpp`) gains a third alternative,
`API_KEY`, holding one opaque secret-value snapshot exactly like `BEARER`
does — move-only, non-copyable, no plaintext accessor, released idempotently
on every exit path.

### Query Experience: resolved by review

`ResolveDuckdbApiSecret` (`secret_integration.cpp:156`) is hard-coded to
construct `ScanAuthorization::GithubUserBearer(...)` regardless of which
relation is executing, and its signature carries no credential-kind
parameter. The original draft posed two designs and proposed the first
without Query Experience's confirmation; Query Experience review has now
supplied that confirmation, with one required refinement.

**Decided: kind-neutral resolution, Runtime-side dispatch, via an additive
factory.** `ResolveDuckdbApiSecret` returns one generic opaque credential
value without Query inspecting which kind the relation declared; Runtime's
already-plan-carried `PlannedAuthenticator` decides at admission/execution
time whether the opaque value is decorated as a bearer header or an api-key
header/query placement. This preserves `docs/ARCHITECTURE.md`'s
responsibility boundary that Query "does not own... request policy" and
requires no change to `CompiledQueryRegistrationView`'s public shape.
Confirmed precedent for Runtime-side dispatch from plan data already exists:
`OpenAuthorizedStream` hands the full plan and an opaque `ScanAuthorization`
to `executor->OpenWithAuthorization`, and it is Runtime's own
`BearerAuthenticator::AuthorizeRest`/`AuthorizeGraphql`/
`AuthorizePaginatedRest` — not Query — that reads plan facts and materializes
the request; Query's own `scan_plan_explanation.cpp` switches on
`PlannedProtocol` only to render already-resolved facts for `EXPLAIN`, never
to make an execution decision.

**Required refinement from review:** do not collapse the existing
`ScanAuthorization::Bearer`/`GithubUserBearer` factories into one generic
constructor. Those factories are called by name at 40+ existing test sites,
and `BearerAuthenticator::CopyToken` internally self-validates
`kind == BEARER` before releasing the token — a type-tag safety net
independent of plan trust. Collapsing would force renaming every existing
call site and remove that internal invariant unless a parallel tag is kept
anyway, making it a materially larger and riskier change than adding a
credential kind. This RFC instead commits to an **additive** new factory
(`ScanAuthorization::Credential(std::string&&)` or an equivalently named
kind-neutral constructor) alongside the unchanged existing `Bearer`/
`GithubUserBearer` factories, with `ScanAuthorization`'s internal `Kind` still
distinguishing `BEARER`/`API_KEY` so each authenticator keeps its own
type-tag check.

### Public behavior

Adds one new accepted `kind` value (`api_key`) and one new `placement` axis
(`header` or `query`, each with its required companion name field) to
`docs/CONNECTOR_SPECIFICATIONS.md`'s Credentials section. No existing accepted
package (`connectors/github`, `connectors/rickandmorty`) changes behavior —
this is an addition to the closed set. `EXPLAIN` gains `api_key` as a possible
`authenticator` fact value alongside `bearer`, and a placement fact that names
the declared header or query-parameter name (never the value).

### Shared interfaces

Covered in full above (Compiled IR, Runtime execution, Query Experience). In
summary: Connector Experience (schema/compiler, using the closed-sibling-`$defs`
idiom review corrected this RFC onto), Relational Semantics
(`PlannedAuthenticator`/`PlannedCredentialPlacement`, their exhaustive explain
switches, **and** `ValidateAuthentication`/`ScanPlanBuilder::Build`'s
plan-construction logic — a real surface an earlier draft understated), Remote
Runtime (admission booleans generalized to a credential-requirement value,
new `ApiKeyAuthenticator`, `ScanAuthorization`'s third alternative, the
`SameHeaders` generalization, and the query-name-collision check Runtime
already owns at admission time), and Query Experience (`ResolveDuckdbApiSecret`
resolved to a kind-neutral additive factory, confirmed by review) are all
genuinely affected — the same four-team surface shape RFC 0016 found for
pagination, reached here by direct inspection and corrected through review
rather than assumed by analogy alone.

### Operational behavior

No new resource, cancellation, retry, or caching model. The same aggregate
request-header-byte budget (`request_header_budget.hpp`) and the same
8 KiB-class secret-size ceiling apply to the API-key value as to the bearer
token (exact ceiling is an implementation decision, not below bearer's
existing `BearerTokenByteLimit`). No new credential *source*: only the
existing `duckdb_api` DuckDB secret type, resolved by exact logical name at
execution initialization exactly as bearer is today.

## Topology impact

| Team | Role in this RFC | Interface or charter impact | Interaction mode | Exit condition |
| --- | --- | --- | --- | --- |
| Connector Experience | Sponsor and schema/compiler owner | `$defs/credential` gains two new closed sibling shapes (`apiKeyHeaderCredential`, `apiKeyQueryCredential`) in its `oneOf`, matching the pagination idiom; `DecodeCredential` gains a discriminated branch mirroring `DecodeAuth`/`DecodeQueryField`; no charter text change | X-as-a-Service (existing) | Package authors can declare `api_key` and get the same diagnostic quality as `bearer` |
| Relational Semantics | Required implementation participant | `PlannedAuthenticator`/`PlannedCredentialPlacement` gain arms; `AuthenticatorName`/`PlacementName` in `scan_plan_explain.cpp` gain arms; **and**, confirmed by review, `ValidateAuthentication` (`scan_planner_validation.cpp:341-374`) and `ScanPlanBuilder::Build` (`scan_planner.cpp:91-104`) must stop hard-coding `BEARER`/`AUTHORIZATION_HEADER` and instead read the compiled connector's own credential kind, or a compiled `api_key` relation fails plan construction (or crashes at explain) today | Collaboration | A fixture proves an `api_key` relation both plans successfully (not rejected by `ValidateAuthentication`) and explains correctly for both placements |
| Remote Runtime | Primary implementer | New `ApiKeyAuthenticator`; `requires_bearer` booleans generalized to a credential-requirement value across REST and GraphQL admission; `ScanAuthorization` gains a third alternative; `SameHeaders`'s hard-coded `"authorization"` exclusion (`bearer_authenticator.cpp:16-27`) generalizes to an arbitrary declared header name; the query-name-collision check already owned by `rest_request_materialization.cpp`'s `TryCopyPermanentQuery`/`TryCopyLegacyQuery` extends to the credential's declared name; `FormUrlEncode` needs exporting for reuse | Collaboration | The generalized admission and authenticator pass the same malformed/cross-origin/redaction fixture oracles bearer already has, for both header and query placement, with the credential's query name never entering `query_bindings`/`query_parameters` |
| Query Experience | Required implementation participant, design confirmed by review | `ResolveDuckdbApiSecret` gains an additive kind-neutral credential factory (not a collapse of `Bearer`/`GithubUserBearer`); Runtime dispatches placement from `PlannedAuthenticator`, so `CompiledQueryRegistrationView`'s public shape is unchanged | X-as-a-Service (design question resolved; implementation is a bounded, already-proven-pattern change) | The additive factory and unchanged registration-view shape are proven end to end without renaming existing `Bearer`/`GithubUserBearer` call sites |
| Engineering Enablement | Facilitator | Public-surface-inventory and `release/1.0.0/freeze.json`/`freeze.md` gain the new credential-kind enumeration, **including** identifying and rewording the `authenticators_beyond_anonymous_and_capability_scoped_bearer` mandatory-exclusion entry (`freeze.json:125`) that `api_key` would otherwise contradict; fixture-coverage variant set extended, **including a header-name-collision category symmetric to the query-param one** (confirmed by review: `package_compile_helpers.cpp`'s `CompileHeaders` already has a case-insensitive fixed-header uniqueness check the original draft's coverage set omitted) | Facilitation | Connector Experience, Remote Runtime, Relational Semantics, and Query Experience maintain the corrected oracle independently, and the freeze mutation suite covers both the new candidate-revision entry and the amended exclusion |

## Correctness, security, and lifecycle analysis

- **Relational semantics and conservative fallback:** Credential kind carries
  no predicate, ordering, or limit authority, and `PlanBaseDomain`-class
  occurrence reasoning (the pagination-specific concern RFC 0016 raised) does
  not apply here. **Corrected by review:** this is not confined to the
  explain switches, as an earlier draft claimed — `ValidateAuthentication`
  (`scan_planner_validation.cpp:341-374`) and `ScanPlanBuilder::Build`
  (`scan_planner.cpp:91-104`) are real plan-construction sites that must stop
  assuming `BEARER`/`AUTHORIZATION_HEADER` unconditionally, or every compiled
  `api_key` relation fails before a plan is ever produced.
- **Authentication, credentials, network policy, and privacy:** The primary
  concern of this RFC. Destination and placement validation happen before
  decoration exactly as bearer's does today; connector policy narrows but
  never widens host policy; the credential value never enters diagnostics,
  explanation, digests, fixtures, or catalog introspection. The query-param
  placement is the one genuinely new risk surface: it requires the encoded
  value never be logged, cached by an intermediary as part of a URL, or
  echoed in `EXPLAIN`'s rendered target — `EXPLAIN` must render only the
  declared parameter *name*, never a materialized query string containing the
  live value, mirroring how bearer's header value is named but never printed.
- **Resource budgets, backpressure, and cancellation:** Reuses the existing
  aggregate header-byte and per-page/per-scan ceilings unchanged; a
  query-parameter value contributes to the existing URL/target-length
  ceiling rather than a new one.
- **Replay, retries, caching, and duplicate prevention:** Not affected; v1
  performs one attempt regardless of credential kind.
- **Concurrency, immutability, and state ownership:** Not affected; each
  scan resolves and owns one independent move-only `ScanAuthorization`
  exactly as today.
- **FFI, initialization, reload, shutdown, and failure containment:** Not
  affected.
- **Diagnostics, redaction, metrics, and progress reporting:** Reuses the
  existing `DUCKDB_API_UNSUPPORTED_DECLARATION`/`MISSING_FIELD`/
  `INVALID_IDENTIFIER` codes for the new schema fields; reuses the existing
  `authentication`/`authorization` `ErrorStage` categories. A new
  diagnostic check is required for query-parameter-name collision with a
  declared query field (candidate code: `DUCKDB_API_DUPLICATE_ID`, since it
  is structurally a name collision within one operation's declared fields).

## Compatibility and migration

Additive only. No existing accepted package (`connectors/github`,
`connectors/rickandmorty`) changes behavior or requires a version bump; both
are bearer/anonymous-only today and the `bearer` schema branch is unchanged.
This is a pre-`1.0.0` `MINOR` under `ROADMAP.md`'s versioning model.

## Evidence and bounded trials

| Question or claim | Evidence required | Method or fixture | Result and limitations |
| --- | --- | --- | --- |
| `bearer`'s current schema, compiled-IR, and execution shape is fully closed at exactly the sites this RFC proposes to extend | Direct source inspection | Read `connector-package-v1.schema.json`, `package_manifest_schema.cpp`, `scan_plan.hpp`, `scan_plan_explain.cpp`, `rest_authority_admission.cpp`, `graphql_plan_admission.cpp`, `bearer_authenticator.cpp`, `authorization.hpp`, `secret_integration.cpp`, `relation_execution.cpp` | Confirmed: every cited enum/boolean/function is exactly as described (line numbers above), and `ResolveDuckdbApiSecret` is live on the real v1 package path (`relation_execution.cpp:80`), not only the deprecated dispatcher. |
| Query-parameter credential placement can reuse the existing REST query-field encoder and an existing collision check | Direct source inspection (resolved by Remote Runtime review) | Read `package_rest_schema.cpp`'s query-field declaration, `rest_request_materialization.cpp`'s `TryCopyPermanentQuery`/`TryCopyLegacyQuery`, and `FormUrlEncode` | Resolved: no compile-time uniqueness check exists in the schema/compiler layer, but Remote Runtime's admission path already builds a `std::set<std::string> names` across fixed/input/conditional query bindings — the credential's declared query name extends that same check. `FormUrlEncode` exists but is not exported from its header and needs to be for reuse. |
| A real API exists that would validate this design (API-key-in-header or API-key-in-query, no auth beyond a static key) | Candidate-provider survey | Not yet performed — this RFC decides the design in the abstract, as RFC 0016 initially did for `response_next` before Rick and Morty was chosen | Pending: choosing the actual provider #3 that will exercise `api_key` is separate PM-reserved work per the precedent set by Rick and Morty's selection, tracked as a Follow-on goal, not this RFC. |

## Alternatives considered

1. **Add `api_key` now, scoped to static header/query placement (proposed).**
   Benefit: closes the largest evidenced credential-shape gap with no new
   authority beyond bearer's existing model (still one static secret value,
   one fixed destination set, no dynamic behavior). Drawback: real,
   non-trivial work across four teams' existing exhaustive switches, the same
   shape of cost RFC 0016 found for pagination.
2. **Add a general OAuth2 client-credentials or authorization-code flow
   instead.** Benefit: covers a broader class of enterprise/production APIs.
   Drawback: introduces token acquisition, refresh, expiry, and scope state —
   an entirely different lifecycle and security-review surface, explicitly
   excluded by this RFC's guardrails and by `ROADMAP.md`'s `1.0.0` exclusion
   list ("authenticators beyond anonymous and capability-scoped bearer
   behavior unless a later accepted RFC"). Not selected; a credible future
   RFC on its own if a real target API needs it.
3. **Support only header placement, defer query placement.** Benefit:
   smaller surface, avoids the query-field-collision question entirely.
   Drawback: a materially large share of free/public API-key APIs use query
   placement (`?api_key=`, `?apikey=`), so deferring it likely reopens this
   exact RFC the next time a candidate provider needs it — the same
   "resurfaces at higher cost" pattern RFC 0016 flagged for deferring
   pagination.
4. **Fixed, non-author-declared header/parameter name (e.g. always
   `X-Api-Key`).** Benefit: smaller schema surface. Drawback: real APIs use
   materially inconsistent names for this (`X-Api-Key`, `apikey`, `api_key`,
   `token`, `Api-Key`); a fixed name would fail to cover common cases
   immediately, defeating the goal.
5. **Retain current bearer-only behavior.** Rejected: leaves the evidenced
   gap undocumented and continues to constrain provider choice, the outcome
   this RFC exists to avoid.

## Drawbacks and failure modes

- Four teams (Connector Experience, Relational Semantics, Remote Runtime,
  Query Experience) each extend an existing exhaustive switch or hard-coded
  path they own — not a new pattern for any of them, but real work in six
  distinct sites across those four teams once review's corrections are
  included (two more than the original draft's four-site estimate: Relational
  Semantics' `ValidateAuthentication`/`ScanPlanBuilder::Build`, and Remote
  Runtime's `SameHeaders` generalization), mirroring RFC 0016's actual cost
  after review corrected an initially narrower estimate.
- The Query Experience design question is resolved by review (kind-neutral
  resolution via an additive factory, not a collapse of existing factories);
  no design ambiguity remains, but the additive-factory constraint must be
  honored during implementation rather than simplified back into a collapse
  for convenience.
- Query-parameter placement is a strictly larger security-review surface than
  header placement: a header value is exceedingly unlikely to be
  logged/cached by generic HTTP tooling by default, while a query-string
  value is a more common accidental-logging target (access logs, proxy
  logs, browser history — though none of those apply to this server-side
  extension's own request path, only to any intermediary the declared origin
  operates). This does not change the extension's own redaction guarantees
  provided the credential name/value is kept out of `query_bindings`/
  `query_parameters` as required above, but should be named explicitly in
  author-facing documentation as a caveat about the destination API's own
  operational practices, not this project's.
- The query-field-name collision check is confirmed to already exist, but in
  Remote Runtime's admission path, not Connector Experience's compiler — an
  earlier draft misattributed this. Connector Experience may additionally add
  a compile-time check for author ergonomics, but this is not required for
  correctness since Runtime's admission-time check is authoritative and
  already exists.
- The `release/1.0.0/freeze.json` mandatory exclusion
  `authenticators_beyond_anonymous_and_capability_scoped_bearer` (line 125)
  textually contradicts `api_key`'s graduation; the freeze artifact and its
  mutation-test suite (`test/python/contract_freeze_tests.py`) must be
  updated together with implementation, not as an afterthought, or the
  fail-closed mutation gate goes stale or self-contradictory.

## Acceptance and verification

- **End-to-end demonstration:** a new offline fixture package (parallel to
  `connectors/github`'s bearer fixtures) declares an `api_key` credential in
  both placements, loads through `duckdb_api_load_connector`, and a
  controlled fixture-transport observation proves the exact header or query
  parameter carries the value — never the `Authorization` header, never in
  diagnostics or `EXPLAIN`.
- **Automated oracle:** schema/compiler fixtures for `kind: api_key` (valid
  header, valid query, missing `header_name`/`query_param`, wrong-placement
  field combination, invalid name grammar, policy-widening); a Runtime
  admission/execution fixture proving exact placement and full redaction for
  both header and query, structurally parallel to bearer's existing
  authorization tests; a query-parameter-name-collision fixture (`api_key`'s
  declared query name equals an existing fixed/input/conditional query field
  name) proving admission-time rejection via Remote Runtime's existing
  `TryCopyPermanentQuery`/`TryCopyLegacyQuery` name-set mechanism, **plus a
  symmetric header-name-collision fixture** (`api_key`'s declared header name
  equals an existing fixed header, exercising `CompileHeaders`'s existing
  case-insensitive uniqueness check) — both required per Engineering
  Enablement review, which found the original draft's coverage set
  asymmetric; a compatibility fixture proving GitHub (bearer-only) and Rick
  and Morty (anonymous-only) are unaffected; a Relational Semantics fixture
  proving both that an `api_key` relation successfully plans (not rejected by
  `ValidateAuthentication`) and that `AuthenticatorName`/`PlacementName`
  render `api_key` for both placements without reaching the unhandled-enum
  crash path.
- **Quality gates:** `make build`, `make test`, `make demo`;
  `scripts/verify-source-identities.py`; `scripts/verify-public-surface-
  inventory.py` and its test; `scripts/verify-contract-freeze.py` and its
  test (this changes the frozen `1.0.0` candidate's credential-kind
  enumeration).
- **Independent review:** `$topology-consult` across all five required
  reviewers below; `$adversarial-review` given this touches credential
  handling directly, with particular attention to the query-placement
  redaction and collision concerns above.
- **Interaction exit:** the generalized admission/authenticator/explain
  surfaces pass the full bearer-equivalent fixture oracle for both new
  placements without any consuming team needing another team's internal
  representation.

## Contract propagation

| Source of truth or artifact | Impact | Required update | Completion evidence |
| --- | --- | --- | --- |
| `docs/ARCHITECTURE.md` | Not affected | No architectural-invariant change; execution/authorization section's existing description already generalizes ("an authenticator", "a placement") | Not applicable |
| `docs/CONNECTOR_SPECIFICATIONS.md` | Affected | Extend the Credentials section with the `api_key` `oneOf` branch, header/query placement grammar, and name-collision rule | Pending implementation |
| `docs/RUNTIME_CONTRACTS.md` | Affected | Document the generalized `PlannedAuthenticator`/`PlannedCredentialPlacement` shape, the new `ApiKeyAuthenticator`, and `ScanAuthorization`'s third alternative | Pending implementation |
| `docs/TEAM_TOPOLOGY.md` and active charters | Not affected | No interface or accountability boundary moves; Query's kind-neutral-vs-kind-aware question is resolved within its existing charter, not by moving a boundary | Not applicable |
| `docs/PRODUCT_DELIVERY.md`, `AGENTS.md`, and skills | Not affected | No change | Not applicable |
| `ROADMAP.md` | Affected | Record the `api_key` credential addition as `0.11.0`, the next `0.Y.0` minor after `0.10.0` | Completed by this RFC's acceptance (the `0.11.0` section is added in the same change) |
| `release/1.0.0/freeze.json` and `release/1.0.0/freeze.md` | Affected | Add `api_key` to the frozen credential-kind enumeration once implemented, following the same `accepted_candidate_revisions`-then-graduation pattern RFC 0016 established for `response_next`; **additionally, confirmed by Engineering Enablement review,** reword or scope the mandatory exclusion `authenticators_beyond_anonymous_and_capability_scoped_bearer` (`freeze.json:125`), which `api_key` otherwise textually contradicts, and add new mutation-test cases in `test/python/contract_freeze_tests.py` for both the new `accepted_candidate_revisions` entry and the amended exclusion (mirroring RFC 0016's `test_accepted_candidate_revisions_synthetic_entry_passes`/`_drift_fails`) | Pending implementation |
| Examples, diagnostics, fixtures, and tests | Affected | New fixture-coverage variant set (see Acceptance and verification); no existing package's fixtures change | Pending implementation |

## Unresolved questions

- **Resolved by review (was decision-critical):** kind-neutral
  `ScanAuthorization` resolution with Runtime-side dispatch, via an additive
  factory that does not collapse or rename the existing `Bearer`/
  `GithubUserBearer` factories. `CompiledQueryRegistrationView`'s public
  shape is unchanged.
- Non-blocking: exact diagnostic code for query-parameter-name collision
  with a declared query field (`DUCKDB_API_DUPLICATE_ID` proposed by analogy
  to existing identifier-collision handling) — resolved at implementation
  time.
- Non-blocking: exact byte ceiling for an API-key value (proposed: reuse
  `ScanAuthorization::BearerTokenByteLimit()`'s existing 8 KiB ceiling rather
  than defining a new one) — resolved at implementation time.
- Not this RFC's to answer: which specific real API becomes connector
  provider #3 and exercises this design. Tracked as a Follow-on goal,
  consistent with Rick and Morty's precedent as a separate PM decision.

## Review record

| Required reviewer | Team | Result | Evidence or objection | Disposition by decision owner |
| --- | --- | --- | --- | --- |
| Connector Experience perspective | Connector Experience | Needs evidence | Verified the schema's actual closed-set idiom is sibling `$defs` per `oneOf` branch (as pagination already uses), not the conditionally-required-sibling-fields shape the draft sketched; confirmed `DecodeAuth`/`DecodeQueryField` are the correct compiler precedent for a clean `DecodeCredential` extension; corrected the header-validation file citation (`package_http_schema.cpp`, not `package_rest_schema.cpp`). No objection to the design itself. | Accepted; Schema and compiler section rewritten to the closed-sibling-`$defs` idiom (`apiKeyHeaderCredential`/`apiKeyQueryCredential`), citations corrected, Topology impact row updated. |
| Relational Semantics perspective | Relational Semantics | Objected | Found two required-change sites an earlier draft claimed were "not affected": `ValidateAuthentication` (`scan_planner_validation.cpp:341-374`) throws today for any compiled `api_key` relation before a plan is ever produced, and `ScanPlanBuilder::Build` (`scan_planner.cpp:91-104`) unconditionally hard-codes bearer/header rather than reading the compiled connector's own credential kind. This is genuine plan-construction logic, not explanation. | Accepted; this was the most consequential correction, mirroring RFC 0016's Relational Semantics finding for pagination. Problem, Shared interfaces, Topology impact, and Correctness analysis sections all revised to name both sites as required implementation work with fixture coverage proving successful planning, not only successful explanation. |
| Remote Runtime perspective | Remote Runtime | Needs evidence | Confirmed the three `requires_bearer` boolean sites and assessed generalization as sound; found a fourth undocumented site (`SameHeaders`'s hard-coded `"authorization"` exclusion); resolved the query-name-collision mechanism's actual location (Remote Runtime's `TryCopyPermanentQuery`/`TryCopyLegacyQuery`, not Connector Experience's compiler, correcting a misattribution); found `FormUrlEncode` is not exported for reuse; confirmed the redaction boundary is sound today provided the credential's query name/value never enters `query_bindings`/`query_parameters`. No objection to the design itself. | Accepted; Runtime execution, Shared interfaces, Topology impact, Correctness analysis, and Drawbacks sections all revised to name the fourth site, correct the collision-check attribution, require `FormUrlEncode` export, and state the `query_bindings` exclusion as a hard implementation constraint. |
| Query Experience perspective | Query Experience | Approved | Independently confirmed `ResolveDuckdbApiSecret`'s hard-coded bearer call and its live-on-package-path status; confirmed Option 1 (kind-neutral resolution) preserves Query's documented "no request policy" boundary and matches existing Runtime-side-dispatch precedent (`BearerAuthenticator::AuthorizeRest`/`AuthorizeGraphql`/`AuthorizePaginatedRest`); identified that collapsing `Bearer`/`GithubUserBearer` into one generic factory (which the draft posed as an equal-cost variant of Option 1) would in fact ripple across 40+ existing test call sites and remove `CopyToken`'s internal type-tag self-check — a materially bigger, riskier change than the draft implied. | Accepted; Query Experience section rewritten to record the design as decided (Option 1) via an explicitly additive factory, not a collapse; Topology impact and Follow-on goals updated accordingly. |
| Engineering Enablement perspective | Engineering Enablement | Needs evidence | Found the proposed fixture-coverage set was asymmetric: a query-param-name-collision fixture was proposed with no symmetric header-name-collision fixture, even though `package_compile_helpers.cpp`'s `CompileHeaders` already has a case-insensitive fixed-header uniqueness check to exercise — the same shape of gap RFC 0016's Enablement review found for `next_url_path`'s non-string-value category. Also found `release/1.0.0/freeze.json:125`'s mandatory exclusion `authenticators_beyond_anonymous_and_capability_scoped_bearer` textually contradicts `api_key`'s eventual graduation and the draft's contract-propagation plan did not name it or require new mutation-test cases. | Accepted; Acceptance and verification, Topology impact, Contract propagation, and Drawbacks sections all revised to require the symmetric header-collision fixture and to name the exact exclusion entry and required mutation-test cases. |

All five required reviewers returned evidence-backed dispositions; four found
real, citable gaps in the original draft (mirroring RFC 0016's own review
history), and every one was dispositioned by directly revising this RFC's
technical content, not by overriding or deferring it. No reviewer objected to
the underlying design — a second static credential kind structurally parallel
to `bearer`, reusing its redaction, destination, and policy-narrowing model —
being unsound; every objection was about completeness of the plan (missing
implementation sites, a missing fixture category, a misattributed mechanism,
an under-specified factory design).

## Decision and rationale

- **Technical decision owner:** Lead agent.
- **Product approval:** Received — Nic Galluzzo, 2026-07-22, in the linked
  goal shaping conversation. The reserved decisions are recorded there: support
  both header and query placement in the first cut, and header/query names
  are author-declared rather than fixed. No further product-manager-reserved
  choice is introduced by this RFC or by review — the schema idiom, the
  Relational Semantics plan-construction sites, the Remote Runtime admission
  sites, the Query Experience factory design, and the fixture/freeze
  corrections are all lead-agent technical decisions within the authority
  `docs/RFC_PROCESS.md` and each team's charter already grant.
- **Decision:** The `api_key` credential design is **accepted** as revised by
  this review round: two closed sibling schema shapes
  (`apiKeyHeaderCredential`/`apiKeyQueryCredential`); `PlannedAuthenticator`/
  `PlannedCredentialPlacement` gain `API_KEY`/`HEADER_NAMED`/`QUERY_NAMED`;
  `ValidateAuthentication` and `ScanPlanBuilder::Build` read the compiled
  connector's own credential kind instead of hard-coding bearer;
  `ApiKeyAuthenticator` is added alongside a generalized `SameHeaders`-style
  pre-authorization check and reuse of Remote Runtime's existing query-name
  uniqueness check; `ScanAuthorization` gains an additive kind-neutral
  resolution factory without renaming existing bearer factories; and the
  fixture-coverage and `release/1.0.0/freeze.json` contract-propagation plans
  include the header-collision category and exclusion-text correction review
  required.
- **Rationale:** Every reviewer independently verified the RFC's central
  structural claim — that `api_key` is `bearer`'s existing static-credential
  shape with two closed variations (query placement, author-declared name) —
  by reading the actual source this RFC cites, not by trusting its prose. The
  corrections produced are exactly the kind of completeness gaps a
  citation-grounded review process exists to catch (RFC 0016 set this
  precedent for pagination), and none of them revealed the underlying design
  to be unsound, over-broad, or in conflict with an engineering invariant.
  Accepting now, with the corrections folded directly into this RFC's text,
  keeps faith with `docs/CONNECTOR_SPECIFICATIONS.md`'s own rule that adding a
  new credential kind "requires a later accepted contract" — this RFC is that
  contract.
- **Material objections:** Four of five reviewers (Connector Experience,
  Relational Semantics, Remote Runtime, Engineering Enablement) raised
  evidence-backed objections; all four are dispositioned above by direct
  revision of this RFC's technical content, matching RFC 0016's precedent for
  handling completeness objections within a single review round. None was
  rejected or deferred.
- **Superseded by:** Not applicable.

## Follow-on goals

| Outcome or objective | Accountable team | Supporting teams and interaction modes | Activation condition |
| --- | --- | --- | --- |
| Implement `api_key` credentials (schema, compiler, compiled IR, `ValidateAuthentication`/`ScanPlanBuilder::Build` credential-kind-aware planning, generalized Runtime admission and `SameHeaders` check, `ApiKeyAuthenticator`, `ScanAuthorization` additive factory, header- and query-name-collision fixtures, `release/1.0.0/freeze.json` exclusion correction, docs) | Connector Experience | Remote Runtime (Collaboration), Relational Semantics (Collaboration), Query Experience (X-as-a-Service; design already confirmed by review), Engineering Enablement (Facilitation) | RFC 0018 accepted with product approval and required review recorded (satisfied 2026-07-22) |
| Choose and author connector provider #3 exercising `api_key` (a real API with no auth-shape or pagination-shape gap already resolved by RFC 0016) | Connector Experience | Engineering Enablement (Facilitation) | This RFC's implementation goal delivered; PM selects the target API per the Rick-and-Morty precedent |

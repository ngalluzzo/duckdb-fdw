# Relational Semantics plan: capability-scoped authenticated relation

## Outcome and status

Extend the permanent, deterministic `ScanRequest -> ScanPlan` service to plan
both `github.duckdb_login_search_page` and `github.authenticated_user` from one
immutable native catalog without I/O. The authenticated plan defines the one
successful `/user` object as its complete base domain and records
`EXACTLY_ONE_ON_SUCCESS` source cardinality. That cardinality is not `LIMIT 1`,
a row estimate, or a decoder budget and grants no early row-removal authority.

Status: **Planned; collaboration Open**. RFC 0006 is Accepted at `28d9a83`.
Query Experience remains accountable for the product outcome; Relational
Semantics provides the planning service until the exits below support
X-as-a-Service.

## Permanent ownership

| File | Semantics responsibility |
| --- | --- |
| `src/include/duckdb_api/scan_plan.hpp` | Public immutable plan types, source cardinality, ownership, opaque credential reference, applied auth obligation, and safe explanation contract |
| `src/scan_plan.cpp` | Accessors, invariant-preserving rendering, and deterministic credential-plaintext-free snapshots |
| `src/scan_planner.cpp` | Sole side-effect-free relation selection, validation, and construction path |
| `test/cpp/scan_plan_contract_tests.cpp` | Public type shape, immutability, golden plans, cardinality-versus-limit distinction, and determinism properties |
| `test/cpp/scan_planner_tests.cpp` | Relation selection, conservative behavior, metadata/request counterexamples, policy mapping, and credential-boundary properties |
| `test/cpp/support/live_scan_request.hpp` | Semantics-local anonymous/authenticated request fixtures over Query's public request type |

The existing representation/planner split remains: the header and
`scan_plan.cpp` own handoff meaning, while `scan_planner.cpp` owns validation
and construction. No auth-specific catch-all module is added.

Semantics does not edit Connector metadata, Query's `ScanRequest`, DuckDB
secret registration or lookup, adapter lifecycle, Runtime authentication or
transport, build configuration, or shared contracts in this parallel
workstream. Integration owns coherent RFC propagation into architecture,
connector, and runtime contracts using this workstream's evidence.

## Dependencies and consumers

**Connector Experience provides** the immutable `CompiledConnector` catalog:

- exact identities, schemas, structural requests, source domains, and ceilings
  for both relations;
- the anonymous zero-to-many operation with authentication disabled; and
- the authenticated `EXACTLY_ONE_ON_SUCCESS` operation plus required logical
  credential and exact bearer / HTTPS `api.github.com:443` /
  `Authorization` placement policy.

The catalog contains logical policy only—never a DuckDB secret name, secret
value, provider object, or runtime authenticator. Semantics performs exact
relation lookup without constructing Connector internals or reading YAML. An
unknown, duplicate, ambiguous, or inconsistent relation fails; the planner
never falls back from authenticated to anonymous.

**Query Experience provides** the native capability profile and
protocol-neutral `ScanRequest`: exact relation identity, full projection,
`TRUE` predicate, empty ordering, unset limit/offset, verified cancellation,
secret-manager capability, and an optional typed logical credential reference.
The reference is present only for `authenticated_user` and contains the named
DuckDB secret identity.

The reference is a credential selector, not an `explicit_input`, column,
predicate binding, partition, pushdown, raw secret, principal, or capability.
Anonymous-with-reference and authenticated-without-reference requests fail.
Missing secret-manager capability rejects authenticated planning without
changing anonymous relational meaning. Query alone owns argument validation
and execution-time resolution.

**Query and Remote Runtime consume** the public `ScanPlan`. Query copies the
plan into immutable bind state and resolves its logical reference at each
execution. Runtime receives a separate authorized-secret capability and
executes the typed operation and applied obligation without reclassifying
cardinality or relational ownership.

## Public `ScanPlan` contract

The constructor remains planner-private; copy construction supports immutable
bind state, while assignment and partial initialization remain forbidden. Both
plans record selected relation and operation, complete projection closure,
explicit base domain, `TRUE` remote/residual predicates, DuckDB ownership,
applied network/resource bounds, classification reason, and safe explanation.

The authenticated plan additionally records:

- a single-success-object base domain and typed
  `EXACTLY_ONE_ON_SUCCESS` operation cardinality;
- one opaque logical credential reference;
- authentication enabled; and
- one small plan-owned obligation for bearer use at the exact operation
  destination in the exact `Authorization` header.

The obligation is normalized plan data, not Connector's policy representation
or Runtime's authenticator/capability. It exposes no DuckDB secret type,
provider, persistence state, token, principal snapshot, refresh state, or
handle. Query retains the plan as the one authoritative copy of the logical
reference, so prepared secret replacement changes the next resolved principal
without mutating the plan.

`EXACTLY_ONE_ON_SUCCESS` means one accepted successful response yields exactly
one base row. Zero or multiple records, malformed/schema-invalid content, and
authentication, authorization, policy, or transport failure fail the query;
none becomes an empty relation. A one-record resource ceiling is separate
enforcement and does not implement this cardinality law.

## Conservative construction

Both plans preserve the 0.3 relational posture:

- `TRUE` is remote and residual predicate relative to the explicit base domain;
- DuckDB alone owns filter, ordering, limit, and offset;
- remote/runtime ordering, limit, and offset delegation remain `NONE`;
- projection remains the full three-column closure while unavailable; and
- pagination, providers, retry, and cache remain disabled.

The authenticated plan must simultaneously report
`EXACTLY_ONE_ON_SUCCESS`, no remote/runtime limit, and DuckDB limit ownership.
Neither `/user`, its one-object response shape, nor its one-record budget may
be reclassified as pushed `LIMIT 1`. The anonymous plan preserves its bounded
zero-to-three-item response-page domain and disabled auth. Missing or
unsupported metadata is retained in DuckDB or rejected; unavailable SQL
structure is never reconstructed.

Planning consumes immutable Connector and Query values plus host ceilings. It
does not call DuckDB, inspect the secret catalog, resolve type/provider/storage,
read environment/files, use mutable globals, or acquire network authority.
Focused Semantics targets remain link- and include-independent of Secret
Manager, adapter, transport, decoder, and runtime-auth implementation.

## Negative and property oracles

Focused tests prove:

- identical inputs yield byte-identical snapshots independent of locale and
  unrelated environment state;
- exact relation names select the correct domain, operation, cardinality,
  credential requirement, and policy with no cross-relation fallback;
- the anonymous golden plan remains credential-free and zero-to-many, while
  the authenticated golden plan is exact-one with the closed auth obligation;
- changing only the logical secret name changes only the reference/explanation,
  never selection, cardinality, predicates, ownership, request, or policy;
- missing/empty authenticated references, anonymous references, selectors in
  `explicit_inputs`, and unavailable required capability fail conservatively;
- identity ambiguity and altered schema, extractor, cardinality, auth state,
  authenticator, host, placement, policy, or resource facts fail before plan
  construction;
- incomplete projection, non-`TRUE` unavailable predicates, ordering, bounds,
  or unsupported delegation remain rejected as in 0.3;
- exact-one cardinality and the one-record budget are asserted separately while
  filter-before-limit order remains DuckDB-owned; and
- planner inputs, plan fields, snapshots, errors, and fixtures never contain a
  credential value or authorized-secret handle; a token sentinel never enters
  Semantics evidence.

Consumer oracles complete the proof without moving ownership: Query proves
offline bind/describe/explain/prepare, prepared rotation/drop, and DuckDB
relational operators; Runtime proves one-row success, zero/multiple failure,
fail-closed auth/policy/schema behavior, and consumption of the typed plan plus
separate capability.

## Code documentation and verification

Adjacent public-header documentation records producer/consumers, construction
authority, immutability and copy lifetime, private pre-`1.0` compatibility,
base-domain scope, cardinality-versus-limit meaning, logical-reference
identity/rotation semantics, absence of credential bytes, applied-policy
ownership, residual ownership, conservative rejection, and explanation versus
runtime authority. Planner comments explain exact relation selection, policy
mapping, no anonymous fallback, and no early limit; they do not explain Secret
Manager or transport mechanics.

Implementation runs the two focused Semantics targets, then `make build`,
`make test`, and `make demo`; integration runs source-identity, native-
dependency, and fresh `make verify PROFILE=debug` gates. Independent Semantics
and test-oracle review challenge hidden `LIMIT 1`, fallback, selector/value
conflation, lookup or retention, policy-internal leakage, ownership drift, and
consumer reclassification.

## Collaboration to X-as-a-Service exit

- **Connector Experience:** both golden plans and metadata counterexamples use
  only the public immutable catalog; Semantics neither constructs/imports
  provider internals nor receives a secret name in Connector metadata.
- **Query Experience:** the public capability profile and `ScanRequest` drive
  both plans; Query constructs no plan fields, retains one plan/reference,
  performs no planning-time lookup, and passes prepared and relational oracles.
- **Remote Runtime:** direct tests consume public operation/cardinality/auth
  obligation/budgets plus the separate capability and do not derive relational
  meaning or import planner construction/explanation internals.

All three exits remain **Open** until final declarations, includes,
construction points, focused and consumer tests, and contract propagation
support them. Then the complete immutable, credential-plaintext-free
`ScanPlan` is the normal Semantics X-as-a-Service boundary for both native
relations. Material changes to cardinality, reference meaning, auth obligation,
ownership, or dependency direction return through RFC review.

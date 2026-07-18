# RFC 0006: Add one capability-scoped authenticated relation

```yaml
rfc: "0006"
title: "Add one capability-scoped authenticated relation"
status: "Accepted"
rfc_type: "Product"
sponsor_team: "Query Experience"
technical_decision_owner: "Lead agent"
product_approver: "ngalluzzo"
authors:
  - "Query Experience agent"
required_reviewers:
  - "auth_rfc_connector_review"
  - "auth_rfc_semantics_review"
  - "auth_rfc_runtime_review"
affected_teams:
  - "Query Experience"
  - "Connector Experience"
  - "Relational Semantics"
  - "Remote Runtime"
linked_outcome_or_objective: "First capability-scoped authenticated relation"
supersedes: "none"
```

## Summary

Add a second fixed GitHub relation, `github.authenticated_user`, whose execution
resolves one explicitly named temporary DuckDB secret and sends its token only
as a bearer credential to `https://api.github.com/user`. Bind, describe,
explain, and prepare retain only the logical secret name and perform no secret
lookup or network I/O. Each execution resolves the current secret value into a
query-scoped capability, so replacement affects later prepared executions
without placing plaintext in connector metadata, plans, provider interfaces,
diagnostics, or retained scan state.

## Sponsorship and context

- **RFC type:** Product.
- **Sponsoring team:** Query Experience.
- **Linked outcome or objective:** First capability-scoped authenticated
  relation.
- **Why now:** `0.3.0` proves one bounded anonymous HTTPS relation through the
  permanent DuckDB path. Authentication is the next fundamental product risk:
  a DuckDB user needs to query a resource whose meaning depends on their
  identity without granting ambient credential or network authority.

The affected user creates a named in-memory DuckDB secret and queries their own
GitHub identity through ordinary SQL. This RFC does not generalize connector
authoring or authentication families; it decides the smallest user-visible
credential path over the existing live runtime.

## Problem

The accepted native profile has no secret type, secret-manager access,
authenticated relation, or runtime credential capability. Its sole relation
sets `authentication_enabled = false`, its `ScanPlan` disables
authentication, and `ScanExecutor::Open` receives only a plan and cancellation
view.

A caller therefore cannot ask the extension for `GET /user`, which GitHub
requires to be authenticated. Passing a token directly as a table-function
argument would let credential plaintext enter bind data, copied prepared state,
plan snapshots, and generic provider interfaces. Reading `GITHUB_TOKEN` or
selecting a scoped secret implicitly would instead grant ambient authority and
make prepared behavior depend on undeclared process or catalog state.

Observed facts are:

- GitHub documents `GET /user` as the authenticated-user endpoint. It returns
  `200` for success, `401` when authentication is required, and `403` when
  forbidden. Fine-grained personal access tokens and GitHub App user access
  tokens need no endpoint permission for this operation.
- GitHub documents bearer authentication through the `Authorization` header.
- DuckDB secrets are typed and extension-registered. Temporary secrets are
  in-memory and are the default; persistent secrets are stored unencrypted on
  disk.
- Pinned DuckDB 1.5.4 exposes extension registration for `SecretType` and
  `CreateSecretFunction`, exact-name lookup through `SecretManager`, and a
  table-function global-initialization callback with `ClientContext`.

No decision-critical feasibility unknown remains. The exact implementation and
failure-path oracles remain delivery evidence, not permission to widen this
decision.

## Decision drivers and invariants

- **Must preserve:** the complete anonymous
  `github.duckdb_login_search_page` SQL/schema/domain behavior; deterministic
  network-free bind and planning; immutable compiled metadata and plans;
  DuckDB ownership of filter, ordering, limit, and offset; strict conversion;
  bounded cancellation and close; the fixed supported native cell; and
  redacted failure containment.
- **Must enable:** one DuckDB user to create a named temporary `duckdb_api`
  secret and obtain exactly one typed row for the authenticated GitHub
  principal from a fixed `GET /user` request.
- **Must not introduce:** a token table-function argument; plaintext in
  `CompiledConnector`, `ScanRequest`, `ScanPlan`, bind/copy state, or public
  provider APIs; implicit secret selection; an environment provider or ambient
  environment read; persistent-secret support or encryption claims; caller
  URLs; alternate hosts; redirects; proxy authority; OAuth; GitHub App token
  exchange; arbitrary headers; pagination; retries; caching; connector YAML;
  distribution work; or a public native ABI.
- **Must constrain credentials:** the token is usable only by the fixed bearer
  authenticator, in the `Authorization` header, for HTTPS
  `api.github.com:443`. Connector policy may narrow this capability but cannot
  widen host policy.
- **Must keep execution coherent:** one execution resolves one secret snapshot
  before request transmission. Concurrent or later executions resolve their
  own snapshots; a running execution does not change principal mid-request.

## Proposed decision

The native product registers a DuckDB secret type named `duckdb_api` and one
provider named `config`. The provider accepts exactly one sensitive `TOKEN`
value, marks it redacted in DuckDB's `KeyValueSecret` representation, rejects a
missing, `NULL`, or empty value after DuckDB evaluates the declared `VARCHAR`
option, and rejects persistent creation.
It registers no `env` or credential-chain provider and performs no lookup
outside the supplied `CREATE SECRET` options.

The connector snapshot contains two fixed relations. The existing anonymous
relation remains available. The new `authenticated_user` relation declares an
exactly-one-on-success REST GET, a required logical credential, bearer
authentication, and an exact secret policy permitting only host
`api.github.com`, authenticator `bearer`, and header `Authorization`. The
snapshot contains no DuckDB secret name or token.

The adapter treats the SQL `secret` value as an explicit logical secret name.
Bind validates only presence and non-emptiness for the authenticated relation,
stores the name in immutable copied bind state, and builds the plan without
calling DuckDB's Secret Manager. Execution initialization uses the current
`ClientContext` to perform exact-name lookup, validates type `duckdb_api`,
provider `config`, temporary storage, and a non-empty token, then creates one
move-only execution-scoped authorized-secret capability.

The capability crosses into Remote Runtime as an opaque handle, not as a
plaintext string in `ScanExecutor`, `BatchStream`, connector, or planner data.
Only the fixed bearer authenticator can consume it while constructing the
authorized request. Any transient header and dependency-owned credential
buffers belong to that execution and are released on success, failure,
cancellation, or close. The RFC does not claim secure memory zeroization:
DuckDB's temporary secret entry intentionally retains the token for its
lifetime, and DuckDB or libcurl may leave released buffer bytes in process
memory.

### Public behavior

The user creates an explicitly temporary secret:

```sql
CREATE TEMPORARY SECRET github_default (
    TYPE duckdb_api,
    PROVIDER config,
    TOKEN 'github-token-value'
);
```

The accepted query is:

```sql
SELECT id, login, site_admin
FROM duckdb_api_scan(
    connector := 'github',
    relation := 'authenticated_user',
    secret := 'github_default'
);
```

On HTTP `200`, the fixed JSON object produces exactly one row with required,
non-null `id BIGINT`, `login VARCHAR`, and `site_admin BOOLEAN`. Required
extraction does not add DuckDB-visible `NOT NULL` metadata. The request is:

```text
GET /user HTTP/1.1
Host: api.github.com
Accept: application/vnd.github+json
Authorization: Bearer [resolved token]
User-Agent: duckdb-api/0.4.0
X-GitHub-Api-Version: 2022-11-28
```

GitHub's `2022-11-28` REST API version remains supported through
March 10, 2028. Keeping that already proven product input avoids coupling this
authentication decision to the separate breaking-change migration to
`2026-03-10`.

The existing anonymous query remains valid without `secret`:

```sql
FROM duckdb_api_scan(
    connector := 'github',
    relation := 'duckdb_login_search_page'
);
```

For the anonymous relation, supplying `secret` is rejected at bind rather than
ignored. For `authenticated_user`, omitting `secret`, passing `NULL`, or passing
an empty name fails at bind without secret lookup. Resolution queries only
DuckDB's temporary `memory` storage: a persistent-only name is not found, and a
same-named persistent or alternate-storage entry is ignored. A nonexistent,
wrong-type, wrong-provider, malformed, or empty in-memory secret fails during
execution initialization before network I/O. HTTP `401` and `403` are
authentication/authorization failures, never empty relations.

`DESCRIBE`, `EXPLAIN`, and `PREPARE` validate the relation and retain only the
secret name; they neither resolve it nor acquire network authority. Each
execution resolves the name again. Replacing `github_default` changes the
principal used by the next execution of an existing prepared statement;
dropping it makes that next execution fail before I/O. A secret replacement or
drop after a scan has initialized does not mutate that running scan's
execution-scoped capability.

Secret values necessarily enter the user's `CREATE SECRET` statement and
DuckDB's temporary in-memory secret entry. DuckDB warns that client history can
retain `CREATE SECRET` text, and a privileged user can opt into unredacted
secret introspection. This product promises that `duckdb_api` does not copy the
token into its plans, explanations, diagnostics, logs, rows, retained scan
state, fixtures, or evidence; it does not claim to protect secrets from the
DuckDB process owner or hostile SQL with equivalent authority.

### Shared interfaces

- **Connector Experience provides:** an immutable relation catalog rather than
  one singleton relation; a credential requirement and binding policy for
  `authenticated_user`; typed `EXACTLY_ONE_ON_SUCCESS` cardinality; and the
  fixed structural `/user` request. The metadata contains logical policy only,
  never a DuckDB secret name or credential value.
- **Query Experience provides:** the optional named `secret` SQL argument;
  relation-specific argument validation; bind/copy state that retains the
  logical name only; DuckDB 1.5.4 secret-type/provider registration; exact-name
  execution-time resolution; a move-only query-scoped secret capability; and
  one translation of safe authentication errors at the DuckDB boundary.
- **Relational Semantics provides:** a `ScanPlan` that records the selected
  relation, exactly-one-on-success cardinality, an opaque logical secret
  reference, the required bearer/host/header authorization policy, and
  authentication enabled. It remains constructible from immutable metadata
  and `ScanRequest` without resolving the secret. It assigns all relational
  work to DuckDB and carries no credential bytes.
- **Remote Runtime provides:** an extended executor-open contract accepting an
  execution-scoped authorized-secret capability, a fixed bearer authenticator,
  policy validation before decoration and transmission, and structured
  authentication/authorization failures. Runtime may consume the opaque
  capability only for the plan-declared authenticator, final destination, and
  placement; it cannot expose a general secret reader to transport, decoder,
  or provider code.

`CompiledConnector`, `ScanRequest`, `ScanPlan`, and their snapshots remain
credential-plaintext-free. `BatchStream` remains DuckDB-free and does not
retain a secret handle after close. No interface described here is a public
native ABI.

### Operational behavior

- Extension load registers the `duckdb_api` secret type and `config` provider
  before registering `duckdb_api_scan`. A collision, incomplete registration,
  or unsupported Secret Manager API aborts load before the scan function is
  registered.
- Secret lookup occurs once per scan during DuckDB global initialization. It is
  local catalog access, not bind/planning work and not network I/O. Executor
  open validates the plan and secret capability without opening a socket; the
  existing first-pull network boundary remains.
- Destination and placement policy are checked before adding the bearer
  header. The final authority is exactly HTTPS `api.github.com:443` and the
  credential placement is exactly `Authorization`. Redirects remain disabled;
  a `3xx` fails and no second destination receives the header.
- Proxy, netrc, cookies, caller headers, environment lookup, and filesystem
  credential lookup remain disabled. TLS peer and hostname verification and
  post-DNS address policy remain unchanged.
- The authenticated relation uses the existing one-attempt, five-second,
  one-response, one-concurrency, bounded-decode profile. It has no pagination,
  retry, cache, or fallback. A live failure never returns the anonymous
  relation or stale rows.
- Every scan owns its secret capability, HTTP header list, transfer, response,
  decoder, and stream. Cancel, close, and destruction release them
  idempotently and without throwing. No token enters process-global curl state,
  cookies, connection cache keys, or retained telemetry.
- Dynamic extension unload/reload remains unsupported. Secret replacement and
  drop are ordinary DuckDB catalog operations, not extension reload.

## Topology impact

| Team | Role in this RFC | Interface or charter impact | Interaction mode | Exit condition |
| --- | --- | --- | --- | --- |
| Query Experience | Sponsor and product-outcome owner | Owns SQL ergonomics, secret registration and lookup at the DuckDB boundary, prepared execution semantics, and user-visible failures | Collaboration | Accepted success, missing-secret, rotation, drop, cancellation, and redaction narratives pass while adapter code depends only on Connector, Semantics, and Runtime team APIs |
| Connector Experience | Immutable policy-metadata provider | Generalizes the native snapshot to a relation catalog and adds logical credential/auth binding metadata without activating YAML | Collaboration, then X-as-a-Service | Both relations compile and explain through a deterministic metadata oracle; consumers neither construct policy internals nor receive a token or package syntax |
| Relational Semantics | Planning provider | Adds exactly-one cardinality and an opaque credential requirement to the immutable offline plan | Collaboration, then X-as-a-Service | Planner oracles prove both relations, no secret resolution, DuckDB relational ownership, and conservative rejection; consumers do not reclassify authentication or cardinality |
| Remote Runtime | Execution and authentication provider | Adds the authorized-secret capability, fixed bearer decoration, host/placement enforcement, lifecycle, and redacted error service | Collaboration, then X-as-a-Service | DuckDB-free runtime tests prove bearer success, denial, redirect non-forwarding, isolation, cancellation, and redaction; Query consumes only the documented executor capability |

No accountability boundary moves and no charter or topology update is needed.
Query gains only DuckDB-specific secret-manager coupling. Connector owns
declarative credential policy, Semantics owns its plan meaning, and Runtime
owns credential use and transport enforcement. The collaboration ends only
when final source and test dependencies preserve those directions.

## Correctness, security, and lifecycle analysis

- **Relational semantics and conservative fallback:** The successful
  `GET /user` response object is the complete base domain and produces exactly
  one row. The `secret` argument is a credential selector, not a relation
  column, predicate binding, partition, or pushdown. Remote and residual
  predicates are `TRUE`; DuckDB owns filter, ordering, limit, and offset. Auth,
  policy, HTTP, decode, or schema failure fails the query and cannot become zero
  rows. Missing planning capabilities reject or retain work conservatively.
- **Authentication, credentials, network policy, and privacy:** Exact-name
  selection prevents ambient scope matching. Type/provider/persistence/token
  validation precedes capability creation; destination and placement validation
  precede bearer decoration. The capability cannot authorize another host,
  header, authenticator, or operation. No token is placed in URL, query,
  connector metadata, plan, diagnostic, metric, fixture, or output. Redirects
  and arbitrary destinations remain denied.
- **Resource budgets, backpressure, and cancellation:** Existing hard request,
  response, header, decompression, record, string, JSON, memory, batch, wall
  time, and concurrency ceilings remain. The one-row response passes through
  the existing pull stream. Cancellation covers authorization preparation,
  transfer, decoding, and delivery; capability/header cleanup follows every
  exit.
- **Replay units, retries, caching, and duplicate prevention:** There is one
  request and no retry or cache. The single successful response object is
  emitted once. A credential replacement never causes the active request to be
  replayed; it affects only a later execution.
- **Concurrency, immutability, and state ownership:** Connector metadata,
  request, and plan are immutable and secret-free. Each scan resolves and owns
  one independent move-only secret capability. Concurrent executions can use
  different current secret snapshots without shared mutable auth state. No
  execution mutates a prepared plan.
- **FFI, initialization, reload, shutdown, and failure containment:** Pinned
  DuckDB callbacks provide `ClientContext` at execution initialization and
  allow extension registration of the secret type/provider. DuckDB exceptions
  and secret objects stay at the adapter edge; runtime receives only its
  capability. Existing native exception containment, process-resident libcurl
  initialization, idempotent stream close, and unsupported dynamic unload
  remain. Registration failure does not leave a partially available function.
- **Diagnostics, redaction, metrics, and progress:** Add stable
  `authentication` and `authorization` categories. Safe diagnostics may contain
  connector, relation, logical secret name, expected secret type/provider, and
  remote status, but never token, `Authorization` value, response body, curl
  request dump, or unrestricted upstream message. Credential sentinels are
  checked against plan snapshots, DuckDB errors, runtime errors, captured logs,
  evidence, and installed-artifact strings. No public auth metric or progress
  claim is added.

## Compatibility and migration

The existing `duckdb_api_scan(connector := 'github', relation :=
'duckdb_login_search_page')` query, schema, relation domain, and anonymous
request policy continue to work. `secret` becomes a recognized named argument
only for `authenticated_user`; it remains an error for the anonymous relation,
so credentials cannot be silently ignored. Existing prepared anonymous
statements remain credential-free.

This is an additive pre-`1.0` minor release. No stored connector packages or
user data migrate. The new secret type can coexist with other DuckDB secret
types; a registration-name collision fails closed. Lookup is storage-qualified
to temporary `memory`, so excluded persistent or alternate backends neither
satisfy nor interfere with the name. Only temporary `duckdb_api` secrets are
supported, so the extension creates no credential files and makes no at-rest
encryption promise.

Rollback means loading `0.3.0`, which retains the anonymous relation but does
not register the `duckdb_api` secret type or authenticated relation. Temporary
secrets are DuckDB-instance state and are not a portable rollback artifact.
Unsupported DuckDB capability profiles fail registration or execution rather
than reading environment variables, accepting plaintext SQL arguments, or
falling back to anonymous behavior. The supported cell remains the exact
source-built DuckDB 1.5.4 `osx_arm64` profile; this RFC does not claim Community
distribution or a broader native ABI.

## Evidence and bounded trials

| Question or claim | Evidence required | Method or fixture | Result and limitations |
| --- | --- | --- | --- |
| Does GitHub expose the required fixed authenticated relation? | Official endpoint, auth, status, schema, and permission contract | [GitHub `GET /user` documentation](https://docs.github.com/en/rest/users/users#get-the-authenticated-user) and [REST authentication documentation](https://docs.github.com/en/rest/authentication/authenticating-to-the-rest-api) | Confirmed: bearer auth is supported; `GET /user` returns the required `id`, `login`, and `site_admin` fields; fine-grained PATs need no endpoint permission; `401` and `403` are explicit failure statuses. Live identity remains compatibility evidence, not the deterministic oracle. |
| Is the selected GitHub API version currently supported? | Official supported-version policy | [GitHub API versions](https://docs.github.com/en/rest/about-the-rest-api/api-versions) | Confirmed: `2022-11-28` is supported through March 10, 2028. A later version migration remains separate compatibility work. |
| Can an extension register a custom temporary secret type/provider? | Pinned DuckDB registration and creation APIs plus syntax evidence | DuckDB 1.5.4 commit `08e34c447b`: [`ExtensionLoader`](https://github.com/duckdb/duckdb/blob/08e34c447bae34eaee3723cac61f2878b6bdf787/src/main/extension/extension_loader.cpp), [`SecretType` and `CreateSecretFunction`](https://github.com/duckdb/duckdb/blob/08e34c447bae34eaee3723cac61f2878b6bdf787/src/include/duckdb/main/secret/secret.hpp), [redacted config-provider example](https://github.com/duckdb/duckdb/blob/08e34c447bae34eaee3723cac61f2878b6bdf787/src/main/secret/default_secrets.cpp), and [custom secret tests](https://github.com/duckdb/duckdb/blob/08e34c447bae34eaee3723cac61f2878b6bdf787/test/secrets/test_custom_secret_storage.cpp) | Confirmed at source level: extension registration, `config` provider parameters, redacted key-value secrets, explicit `CREATE TEMPORARY SECRET`, and persistence mode inspection are available. This proves the pinned cell, not other DuckDB versions or the portable C API. |
| Can prepared bind remain secret-free while execution sees current catalog state? | Separate bind and execution callbacks with current `ClientContext` and exact-name lookup | DuckDB 1.5.4 [`TableFunctionInitInput` callbacks](https://github.com/duckdb/duckdb/blob/08e34c447bae34eaee3723cac61f2878b6bdf787/src/include/duckdb/function/table_function.hpp) and [`SecretManager::GetSecretByName`](https://github.com/duckdb/duckdb/blob/08e34c447bae34eaee3723cac61f2878b6bdf787/src/main/secret/secret_manager.cpp) | Confirmed at source level: global initialization receives `ClientContext`, and exact-name lookup accepts the current system-catalog transaction. Prepared rotation/drop behavior still requires the deterministic product oracle before delivery closes. |
| Does DuckDB itself make the intended storage and redaction claims? | Host documentation | [DuckDB Secrets Manager](https://duckdb.org/docs/stable/configuration/secrets_manager) and [DuckDB security guidance](https://duckdb.org/docs/stable/operations_manual/securing_duckdb/overview) | Confirmed: temporary secrets are in-memory; persistent secrets are unencrypted; sensitive display is redacted by default but privileged unredacted access exists. This RFC narrows support to temporary secrets and does not claim hostile-process isolation. |
| Does the permanent path enforce capability scope and prepared rotation without leakage? | Exact request/row and zero-leak failure oracles | Private non-installable DuckDB composition using the real registered secret type, exact-name lookup, production adapter/planner/runtime path, and controlled loopback service | Delivery requirement: prove zero requests for bind/describe/prepare and missing/wrong secrets; exact bearer header at the server; token A then replaced token B on one prepared statement; drop failure; no redirect forwarding; `401`/`403`; cancellation/close; and sentinel absence from every safe surface. Synthetic tokens only. |
| Does the public endpoint still accept the delivered path? | One current-service compatibility execution | Operator-supplied short-lived fine-grained token, temporary DuckDB secret, and the accepted SQL | Delivery requirement: observe one typed row without retaining token or personal response data in repository evidence. This opt-in check cannot replace the controlled oracle and is not a CI credential requirement. |

The primary-source inspection resolves the decision-critical mechanism. The two
delivery requirements prove the implementation and upstream compatibility;
they do not authorize alternate providers, hosts, or credential sources.

## Alternatives considered

### Pass the token as a table-function argument

This is mechanically small and avoids registering a secret type. It places
plaintext in SQL, bind/copy state, prepared statements, and potentially plans
and diagnostics, and it gives Query and Semantics credential-handling load.
It violates the capability and redaction boundaries and is rejected.

### Select a DuckDB secret implicitly by scope or default name

DuckDB supports scope matching for file-like destinations. Implicit selection
would be convenient, but the query would not declare which credential it
authorizes, and catalog changes could silently change principal. Query would
also need host-path conventions unrelated to this fixed relation. Explicit
exact-name lookup is selected.

### Reuse DuckDB's generic `http` secret type

The generic type includes proxy, TLS, arbitrary header, environment-provider,
and path-scope concepts and may introduce `httpfs` coupling or autoloading.
Accepting it would grant a broader capability than this relation needs and
would make policy enforcement depend on another extension's provider surface.
A narrow `duckdb_api/config/TOKEN` type is selected.

### Read `GITHUB_TOKEN` or add an environment provider

This reduces user SQL but introduces ambient process authority, hidden prepared
behavior, and a second credential source. It is explicitly outside the
approved outcome and is rejected.

### Support persistent `duckdb_api` secrets

Persistence improves restart ergonomics, but DuckDB documents its persistent
store as unencrypted. Supporting it would add storage, migration, and security
claims unrelated to proving authenticated execution. The provider rejects it.

### Implement OAuth, GitHub App exchange, or refresh

These approaches can improve organizational credential management but add
token acquisition, refresh, scopes, replay, caching, and lifecycle state. They
are independently demonstrable products and are excluded from this bearer-only
goal.

### Retain only the anonymous relation

This avoids credential risk but leaves private or identity-dependent API access
unproven and delivers no new user outcome. It is rejected.

## Drawbacks and failure modes

The proposal adds native C++ coupling to DuckDB's internal Secret Manager API
on the exact supported cell. Query owns that adapter coupling and its
compatibility evidence. Connector and Semantics gain new logical auth metadata
and plan meaning; Runtime gains the high-risk responsibility of transient
credential use, placement enforcement, and cleanup.

The token exists in the user's SQL input, DuckDB's in-memory secret entry, and
transient request buffers. The extension cannot guarantee zeroization or
protect against the process owner, a debugger, a core dump, unredacted DuckDB
secret introspection, or client-side SQL history. Documentation must not
overstate this boundary.

Prepared execution intentionally binds a secret identity, not a principal
snapshot: replacement can change the returned user without re-prepare. This
supports rotation but can surprise a caller who expected credentials to freeze
at prepare time. Each execution is internally stable, and the deterministic
oracle must make this behavior explicit.

GitHub can reject, rate-limit, deprecate, or change the live endpoint. Safe
status diagnostics and controlled fixtures remain authoritative. The fixed
API version has a published support end date, so a later compatibility decision
will be required; this RFC does not silently switch versions. Supporting only
one bearer-authenticated relation does not establish arbitrary authenticated
REST or connector-package authoring.

## Acceptance and verification

- **End-to-end demonstration:** create a temporary `duckdb_api/config` secret,
  query `github.authenticated_user`, and observe one typed identity row. Prove
  ordinary and prepared execution, replacement between executions, drop after
  prepare, anonymous coexistence, and bounded redacted failures. Perform one
  operator-supplied GitHub check without retaining credential or personal row
  contents in evidence.
- **Automated oracle:** a private controlled service validates the exact fixed
  request and synthetic bearer value through the permanent adapter path.
  Request counts prove bind/describe/explain/prepare and pre-I/O failures are
  offline. Rotation returns token-specific synthetic identities; redirect,
  wrong host/placement, missing/wrong secret, empty token, `401`, `403`,
  malformed/oversized response, interruption, close, and recovery cases prove
  denial and lifecycle. Artifact and string canaries prove no loopback authority
  or credential sentinel enters the public artifact or evidence.
- **Quality gates:** `ruby scripts/validate-agent-assets.rb`,
  `scripts/verify-source-identities.py`,
  `python3 -I -B scripts/test-native-dependencies.py`, `make build`, `make test`,
  `make demo`, and a fresh `make verify PROFILE=debug`, including focused
  Connector, Semantics, Runtime, Query, TLS, lifecycle, SQL, direct-load,
  artifact-inventory, and source-identity checks.
- **Independent review:** Connector Experience reviews immutable secret policy
  and absence of credentials; Relational Semantics reviews cardinality and
  offline plan meaning; Remote Runtime reviews least authority, redaction,
  concurrency, and cleanup. A separate adversarial review covers credential
  exfiltration, redirects/DNS, prepared rotation, error leakage, cancellation,
  and false-positive oracles before completion.
- **Interaction exit:** audit final declarations, includes, construction
  points, runtime calls, tests, and adjacent code documentation against every
  topology exit row. Passing end to end does not close collaboration if Query
  constructs auth headers, Runtime reads DuckDB secret objects, or consumers
  reinterpret Connector/Semantics internals.

## Contract propagation

| Source of truth or artifact | Impact | Required update | Completion evidence |
| --- | --- | --- | --- |
| `docs/ARCHITECTURE.md` | Affected | Advance the native profile to `0.4.0`; document both relations, public SQL, explicit temporary-secret behavior, prepared resolution, capability boundary, fixed request, exclusions, and threat-model limits | Pending implementation change |
| `docs/CONNECTOR_SPECIFICATIONS.md` | Affected | Extend the native metadata boundary to the two-relation snapshot and its logical credential/auth policy while keeping YAML, environment sources, package loading, and distribution inactive | Pending implementation change |
| `docs/RUNTIME_CONTRACTS.md` | Affected | Define the native secret reference, exactly-one plan, authorized-secret capability, DuckDB execution-time resolution, bearer enforcement, diagnostics, ownership, and lifecycle | Pending implementation change |
| `docs/TEAM_TOPOLOGY.md` and active charters | Not affected | Existing team APIs and accountabilities already place SQL/adapter work in Query, metadata in Connector, plan meaning in Semantics, and authentication execution in Runtime | Required-review record and final interaction-exit audit pending |
| `docs/PRODUCT_DELIVERY.md`, `AGENTS.md`, and skills | Not affected | Existing RFC, credential-risk review, contract propagation, and topology-exit rules cover this delivery | No update required |
| Examples, diagnostics, fixtures, and tests | Affected | Add safe temporary-secret/query guidance, auth/authorization categories, deterministic synthetic credential fixtures, prepared rotation/drop and denial tests, public/private artifact canaries, and opt-in live proof | Pending implementation change |

The RFC records rationale. The contracts and executable evidence define the
delivered behavior.

## Unresolved questions

None. Secure memory zeroization, hostile-process or hostile-SQL isolation, API
version migration, additional secret providers, and persistent credentials are
explicitly outside the decision rather than unresolved implementation choices.

## Review record

| Required reviewer | Team | Result | Evidence or objection | Disposition by decision owner |
| --- | --- | --- | --- | --- |
| `auth_rfc_connector_review` | Connector Experience | Approved | The immutable relation catalog and logical bearer/host/header policy preserve Connector's credential-free `CompiledConnector` boundary and do not activate YAML. Delivery must prove stable two-relation metadata, no token or DuckDB secret name in the snapshot, and low-friction consumer use. | Approved. The deterministic metadata oracle, credential-absence checks, and Connector interaction-exit audit are incorporated as acceptance requirements, not objections. |
| `auth_rfc_semantics_review` | Relational Semantics | Approved | Exactly-one-on-success cardinality, fail-closed auth errors, the opaque secret reference, offline planning, and DuckDB ownership of all relational operators preserve the semantic contract. Delivery must prove both relation plans and that no consumer reclassifies auth or cardinality. | Approved. The planner oracles and Semantics interaction-exit audit are incorporated as acceptance requirements, not objections. |
| `auth_rfc_runtime_review` | Remote Runtime | Approved | Exact-name execution-time resolution, a move-only per-scan capability, fixed bearer placement at `api.github.com`, redirect denial, and explicit redaction/lifetime limits satisfy least-authority Runtime obligations. Delivery must prove rotation isolation, non-forwarding, cancellation, cleanup, and sentinel absence. | Approved. The security/lifecycle oracles and Runtime interaction-exit audit are incorporated as acceptance requirements, not objections. |
| `runtime_rfc_memory_lookup_review` | Remote Runtime | Approved | Storage-qualified temporary-`memory` lookup is the least-authority Query-to-Runtime source: persistent-only entries are not found, same-name persistent state is ignored, and only the opaque move-only capability crosses the boundary. Focused resolution and adapter tests prove excluded storages are never queried and rejected secrets never enter Runtime. | Approved as a post-acceptance clarification correcting two contradictory ambiguity clauses. The decision is narrowed, not changed; existing regression oracles remain required, and the Runtime interaction exit is satisfied for this boundary. |

All required perspectives approved the decision. The lead agent accepts their
delivery conditions as verification and interaction-exit requirements; RFC
acceptance does not claim implementation or contract-propagation completion.

## Decision and rationale

- **Technical decision owner:** Lead agent.
- **Product approval:** Approved by `ngalluzzo` on 2026-07-18: one named
  temporary DuckDB secret; fixed `github.authenticated_user` over `GET /user`;
  explicit `secret := 'github_default'`; bearer-only `Authorization` placement
  at `api.github.com`; preservation of the anonymous relation; and exclusion of
  YAML, OAuth, caller URLs, redirects, pagination, retries, caching, environment
  lookup, and persistent-secret claims.
- **Rationale:** Accepted by the lead agent. Official GitHub evidence
  establishes the fixed authenticated endpoint, bearer placement, response
  fields, failure statuses, token permissions, and current API-version support.
  Pinned DuckDB 1.5.4 source establishes custom temporary-secret registration,
  exact-name lookup, and execution initialization with the current
  `ClientContext`. Connector Experience approved the credential-free metadata
  boundary, Relational Semantics approved the offline exactly-one plan and
  DuckDB ownership, and Remote Runtime approved the least-authority capability,
  host/placement, redaction, and lifecycle boundary. The proposal is the
  narrowest path that delivers the approved authenticated-user outcome while
  preserving the anonymous relation and every explicit exclusion.
- **Material objections:** None. All three reviewers approved; their stated
  delivery evidence and interaction-exit conditions are incorporated into
  acceptance verification rather than dispositioned as objections.
- **Superseded by:** Not applicable.

## Follow-on goals

| Outcome or objective | Accountable team | Supporting teams and interaction modes | Activation condition |
| --- | --- | --- | --- |
| First capability-scoped authenticated relation | Query Experience | Connector Experience, Relational Semantics, and Remote Runtime in Collaboration, then X-as-a-Service at their recorded exits | RFC 0006 Accepted with product approval and required review recorded |

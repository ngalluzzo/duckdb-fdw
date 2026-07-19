# Goal: First capability-scoped authenticated relation

Follow `docs/PRODUCT_DELIVERY.md`.

Status: **Delivered**. The permanent product path, contract propagation, team
interaction exits, controlled evidence, independent review, cached gate, fresh
gate, and the operator-supplied short-lived fine-grained GitHub compatibility
execution are complete. Query Experience owns the delivered outcome through
the accepted provider services.

## PM brief

### Outcome

For a DuckDB user, enable querying one authenticated GitHub relation through a
named DuckDB secret so that private API access works without exposing
credentials or granting ambient network authority.

### Why now

`0.3.0` proved the permanent live REST mechanism. Authentication is the next
fundamental product risk and unlocks identity-dependent API access before the
project invests in pagination, declarative connector authoring, or
distribution expansion.

### Product guardrails

- Must: use one explicitly named temporary DuckDB secret and resolve its
  current value only when query execution initializes.
- Must: bind the credential to bearer authentication in the `Authorization`
  header for HTTPS `api.github.com:443` and preserve the anonymous
  `github.duckdb_login_search_page` relation.
- Must: return the fixed authenticated identity as required `id BIGINT`,
  `login VARCHAR`, and `site_admin BOOLEAN` values through ordinary DuckDB
  SQL, with bounded redacted failures.
- Must not: introduce a token table-function argument, implicit secret
  selection, YAML, OAuth, GitHub App exchange, caller-selected URLs or headers,
  redirects, pagination, retries, caching, environment lookup, persistent
  secrets, or encryption and memory-zeroization claims.
- Preserve: offline deterministic bind and planning, immutable metadata and
  plans, DuckDB ownership of relational operators, strict conversion, bounded
  cancellation and close, the exact supported native cell, and the threat-model
  limits accepted in RFC 0006.

### Success signals

- A user creates a named temporary `duckdb_api` secret and receives exactly one
  typed row from `github.authenticated_user`.
- Preparing, describing, or explaining the query neither resolves the secret
  nor performs network I/O; replacing or dropping the named secret affects the
  next execution of an already prepared statement.
- Missing, invalid, unauthorized, forbidden, interrupted, and closed executions
  fail safely and do not expose the token through plans, diagnostics, logs,
  output, evidence, or retained scan state.
- The anonymous relation continues to work without a secret and rejects a
  supplied secret rather than silently ignoring it.

## Agent commitment

### Observable interpretation

The user creates:

```sql
CREATE TEMPORARY SECRET github_default (
    TYPE duckdb_api,
    PROVIDER config,
    TOKEN 'github-token-value'
);
```

and executes:

```sql
SELECT id, login, site_admin
FROM duckdb_api_scan(
    connector := 'github',
    relation := 'authenticated_user',
    secret := 'github_default'
);
```

Bind validates the relation-specific arguments and retains only the logical
secret name. Each execution resolves that name from the current DuckDB secret
catalog into a single scan-scoped capability. Remote Runtime may use the
capability only for the plan-declared bearer authenticator, destination, and
header placement. A successful fixed `GET /user` returns one strict typed row;
credential, policy, HTTP, decode, or schema failures fail the query rather than
becoming an empty result or anonymous fallback.

### Acceptance evidence

- Demonstration: build and load the permanent artifact, create the temporary
  secret, execute the accepted SQL with an operator-supplied short-lived token,
  and observe one row with the three declared logical types without retaining
  the token or personal row contents in repository evidence.
- Automated oracle: a private non-installable composition uses DuckDB's real
  secret registration and exact-name lookup with synthetic tokens and a
  controlled service. It proves offline bind/describe/explain/prepare; exact
  authenticated success; prepared replacement and drop; concurrent scan
  isolation; anonymous coexistence; missing, wrong, empty, unauthorized,
  forbidden, redirect, cancellation, close, and recovery behavior; and token
  sentinel absence from every safe surface and the public artifact.
- Quality gates: focused Connector, planner, Runtime security/lifecycle, DuckDB
  adapter, controlled-product, SQL, direct-load, artifact-inventory, source-
  identity, and dependency tests; cached `make build`, `make test`, and
  `make demo`; and one fresh `make verify PROFILE=debug` product cell.
- Independent review: fresh Connector metadata, relational correctness,
  Runtime credential/security/lifecycle, DuckDB adapter/FFI, and test-oracle
  perspectives, plus adversarial re-review after any material correction.

### Contract and invariant impact

- `docs/ARCHITECTURE.md`, `docs/CONNECTOR_SPECIFICATIONS.md`, and
  `docs/RUNTIME_CONTRACTS.md` must agree on the native `0.4.0` two-relation
  profile, logical secret policy, offline plan, execution-scoped capability,
  fixed request, diagnostics, lifecycle, and explicit exclusions.
- `CompiledConnector`, `ScanRequest`, `ScanPlan`, the authorized-secret
  capability, `ScanExecutor`, structured errors, product composition, and the
  DuckDB registration/bind/init path change under their existing team owners.
  None becomes a public native ABI.
- Credentials remain bound to approved authenticators, placements, and hosts.
  Connector policy can narrow but never widen host policy. No credential bytes
  enter connector metadata, relational plans, bind copies, explanations,
  batches, diagnostics, or retained telemetry.
- Bind and planning remain deterministic and side-effect free; a successful
  response produces exactly one base row; every DuckDB relational operator
  remains DuckDB-owned; work remains bounded, cancelable, and subject to
  backpressure; conversion remains strict and lossless.

### Team and RFC routing

- Accountable stream: Query Experience.
- Connector Experience — **Collaboration, then X-as-a-Service:** provide the
  immutable two-relation catalog and logical credential/binding policy without
  a DuckDB secret name, token, or YAML activation. Exit when deterministic
  metadata evidence passes and consumers use only the documented
  `CompiledConnector` service.
- Relational Semantics — **Collaboration, then X-as-a-Service:** provide the
  offline exactly-one-on-success plan with an opaque secret reference and
  DuckDB relational ownership. Exit when planner oracles cover both relations
  and consumers neither resolve secrets nor reclassify plan meaning.
- Remote Runtime — **Collaboration, then X-as-a-Service:** provide the opaque
  authorized-secret capability, fixed bearer execution, host/placement
  enforcement, cancellation, cleanup, and redacted structured errors. Exit
  when DuckDB-free security/lifecycle tests pass and Query contains no auth
  decoration or Runtime internals.
- Engineering Enablement — **Facilitation:** integrate the provider handoffs
  into responsibility-specific build targets, the `0.4.0` source and release
  identities, installed/private artifact inventories, and cached/fresh gates.
  Exit when every provider can run and diagnose its own focused target and the
  shared product gate without an Enablement approval queue.
- RFC: RFC 0006 is Accepted with product approval and all required affected-
  team reviews recorded. Its contract propagation and acceptance evidence are
  completion requirements for this active goal.

### Unknowns and first trial

- Unknown: none identified. Official GitHub documentation and pinned DuckDB
  1.5.4 source establish endpoint, secret registration, exact-name lookup, and
  execution-initialization feasibility.
- Trial: no disposable experiment is needed. The first evidence is the thin
  permanent path through real DuckDB secret registration, the accepted team
  interfaces, and the private controlled product composition.

### Delivery path

1. Propagate RFC 0006 into the authoritative contracts and land the accepted
   provider interfaces with deterministic responsibility-specific evidence.
2. Integrate the permanent DuckDB query path and prove the complete controlled
   success, rotation, denial, redaction, and lifecycle narrative while
   preserving the anonymous relation.
3. Prove current GitHub compatibility, complete independent review and
   interaction-exit audits, and pass the cached and fresh product gates.

## Delivery evidence record

The permanent `0.4.0` path is implemented through the team-owned catalog,
planner, authorization/runtime, DuckDB secret/adapter, composition, build, and
release modules. The accepted SQL creates an explicitly named temporary
`duckdb_api` secret, resolves its current value only at execution
initialization, and returns the fixed `id BIGINT`, `login VARCHAR`, and
`site_admin BOOLEAN` row from `github.authenticated_user`. The anonymous
relation remains credential-free.

Evidence on committed product tree `861b14b75f0cae8aaa76e333429e5f7cc9752060`:

- cached `make test PROFILE=debug` passed all focused native suites, 58 SQL
  assertions, public artifact reconstruction, the 20-request anonymous matrix,
  and the 20-request authenticated matrix;
- fresh `make verify PROFILE=debug` executed 757 build steps without developer
  cache reuse and reproduced the same evidence; its public artifact SHA-256 is
  `4b9fa78a8282b191e4d577c83a9a554463260790bfe6f7394de5b237e6b51bb5`;
- source identities are native
  `89e32b32074aba6046959ad6a47d872cdbcbcce6c9f4acb77c9c986e7df690d0`,
  controlled
  `620f25af26070565a70709d2d4c03b7374d1557a338f7bf0579a9177a727ad2e`,
  and canonical public contract
  `02e6eb66801e665ed6be5db70706505842ee726090ecc39025d592cad95023b5`;
- independent exact-tree reviews closed the outbound-header resource defect
  and the public-contract evidence gap, then reported no remaining P0-P3
  findings for the frozen repair diff; and
- a safe live execution used an operator-supplied fine-grained personal access
  token with GitHub-advertised expiration in 719 hours, loaded `duckdb_api
  0.4.0` in DuckDB 1.5.4, and observed exactly one row with the declared schema
  without emitting or retaining the token or personal row contents in
  evidence.

Connector Experience, Relational Semantics, and Remote Runtime have exited
Collaboration to their documented X-as-a-Service boundaries. Engineering
Enablement has exited Facilitation. Query Experience owns the assembled public
outcome without routine cross-team edits.

The final live execution satisfies RFC 0006's credential-pedigree oracle. The
credential was classified in memory as a fine-grained personal access token,
GitHub advertised its finite expiration, and the accepted SQL succeeded through
the same fresh artifact whose deterministic controlled evidence establishes
the complete request, denial, rotation, lifecycle, and zero-leak behavior. The
ignored operator `.env`, token, response identity, and personal row contents
remain outside repository evidence.

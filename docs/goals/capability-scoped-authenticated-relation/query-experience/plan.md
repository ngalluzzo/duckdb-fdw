# Query Experience plan: capability-scoped authenticated relation

## Outcome and status

Deliver RFC 0006's public DuckDB experience for one explicitly named temporary
secret and the fixed `github.authenticated_user` relation while preserving the
anonymous relation. Query Experience owns DuckDB secret-type/provider
registration, relation-specific SQL argument behavior, offline bind and copy,
execution-time exact-name lookup, adapter lifecycle and diagnostics, product
composition, and the user-visible controlled and public acceptance oracles.

Status: **Implementation delivered; provider interactions satisfied**. The
permanent source, controlled evidence, contract propagation, independent
review, cached/fresh gates, and live mechanism proof are complete. Product
acceptance remains pending only on the RFC's exact short-lived fine-grained
credential pedigree or an approved evidence deviation.

## Ownership boundary

Query Experience owns the DuckDB-specific edge:

- register secret type `duckdb_api` and provider `config` before the table
  function, declaring one redacted `TOKEN VARCHAR` option and rejecting
  persistent, missing, `NULL`, or empty credentials;
- expose optional named SQL argument `secret VARCHAR`, require a nonempty name
  only for `authenticated_user`, and reject it for the anonymous relation;
- retain only the logical secret name in immutable bind/copy state and perform
  no secret lookup during bind, `DESCRIBE`, `EXPLAIN`, or `PREPARE`;
- resolve the current exact named secret once during global scan
  initialization, validate its type, provider, temporary storage, and token,
  and transfer a move-only execution-scoped capability through Runtime's team
  API;
- preserve prepared rotation/drop semantics: later executions resolve again,
  while an initialized scan keeps its original capability;
- translate safe bind, authentication, authorization, cancellation, structured
  execution, and unknown failures exactly once at the DuckDB boundary;
- assemble production and private controlled compositions exclusively from
  Connector, Semantics, and Runtime provider APIs; and
- own the public SQL narrative and black-box product evidence.

Query Experience does **not** construct `Authorization`, select bearer
placement, validate a destination, operate libcurl, parse JSON, implement
redirect or address policy, classify relational meaning, define connector auth
metadata, or expose a general credential reader. It does not edit Connector
Experience, Relational Semantics, or Remote Runtime plans. A missing provider
contract is returned to its owner rather than copied, shimmed, or reinterpreted
inside Query.

Root build/release scripts, dependency pins, shared source-identity machinery,
and another team's focused tests remain outside this workstream. Their owners
may consume the Query source and oracle entry points during integration.

## Permanent source responsibilities

| Artifact or cohesive module | Query-owned responsibility | Boundary proof |
| --- | --- | --- |
| Dedicated DuckDB secret integration module, planned as `src/include/duckdb_api/duckdb_secret.hpp` and `src/duckdb_secret.cpp` | Register the narrow temporary-only type/provider; use pinned DuckDB `SecretManager` for exact-name execution lookup; validate the host secret entry; create only Runtime's opaque scan-scoped capability; convert DuckDB secret failures into Query-owned safe results | The module contains the only `SecretManager`, `SecretType`, `CreateSecretFunction`, `KeyValueSecret`, and `CatalogTransaction` coupling. It contains no header construction, host, URL, curl, decoder, or relational-plan logic |
| `src/include/duckdb_api/scan_request.hpp` and `src/scan_request.cpp` | Extend Query's protocol-neutral request with the selected relation and logical secret reference, using conservative adapter capabilities and no secret value or I/O authority | Snapshots and copies contain the name/identity only; focused tests prove deterministic construction and token-sentinel absence |
| `src/duckdb_api_adapter.cpp` | Keep table-function registration, relation-aware named-argument validation, bind/plan invocation, immutable copy, execution initialization, stream pull, typed `DataChunk` transfer, cancellation/close, and one-time error translation | Adapter includes the dedicated DuckDB secret interface plus public Connector/Semantics/Runtime APIs only; it contains no DuckDB secret implementation, auth decoration, transport, policy, or decoder internals |
| `src/include/duckdb_api_extension.hpp` and `src/duckdb_api_extension.cpp` | Preserve the installed extension identity and exception boundary; order checked product construction, secret registration, and table-function publication so registration failure exposes no scan function | The public and private entry points call the same Query registration surface; no credential, controlled authority, or test selector enters the installed entry point |
| `src/include/duckdb_api/product_composition.hpp` and `src/product_composition.cpp` | Assemble the canonical Connector catalog and Remote Runtime executor service as immutable provider objects; carry no DuckDB secret entry, token, auth header, or Runtime implementation type | Composition includes only public provider headers and has no caller-selected authority, environment/file lookup, or controlled-test branch |

The dedicated secret module prevents DuckDB Secret Manager coupling from
turning the existing adapter into another catch-all. Each module receives one
primary reason to change: host secret integration, scan-request construction,
DuckDB callback adaptation, extension publication, or product composition.

No Query production object retains token plaintext in registration, function
info, bind data, `ScanRequest`, `ScanPlan`, global scan state, or product
composition. The temporary DuckDB secret entry and Runtime's active request
capability are the explicit RFC 0006 storage exceptions; secure memory
zeroization is not claimed.

## Test and product-evidence responsibilities

| Evidence surface | Query-owned evidence |
| --- | --- |
| New focused `test/cpp/duckdb_secret_tests.cpp` | Type/provider registration, redacted display, temporary-only enforcement, option validation, exact-name lookup, wrong/ambiguous type or provider, missing/drop behavior, current-value replacement, and safe exception translation against pinned DuckDB 1.5.4 |
| `test/cpp/scan_request_tests.cpp` | Relation-specific request identity, logical secret-name copy/determinism, conservative capabilities, anonymous/authenticated distinction, and proof that token sentinels cannot enter request snapshots |
| `test/cpp/duckdb_adapter_tests.cpp` | Provider-fake bind/schema/copy/init tests; optional argument rules; zero secret resolution during bind/describe/explain/prepare; one resolution per execution; prepared replacement/drop; repeated and concurrent scan isolation; typed batches; DuckDB-owned relational operations; cancellation, close, context lifetime, and stable safe error mapping |
| `test/cpp/support/query_runtime_scenarios.hpp` and `.cpp` | Query-only fake `ScanExecutor`/`BatchStream` consumers updated to Runtime's accepted opaque-capability interface, without reproducing bearer, host, redirect, curl, or decoding behavior |
| `test/cpp/support/controlled_product_composition.hpp` and `.cpp` plus `test/cpp/controlled_duckdb_api_extension.cpp` | Private non-installable assembly of Connector's controlled catalog, Semantics' production planner, Runtime's controlled executor, and Query's unchanged secret registration/adapter boundary |
| New split `test/python/authenticated_relation_product_contract.py` and `test/python/authenticated_relation_product/` suites | Black-box DuckDB acceptance narrative split by relational behavior, secret lifecycle, failures/redaction, and reusable controlled-service support rather than one monolithic suite |
| `test/sql/duckdb_api.test` | Public function signature, both relation schemas, anonymous/secret binder rules, temporary secret-type inventory, safe static diagnostics, and version identity without relying on a live credential or public row value |
| Public example and direct-load contract | A no-environment, no-file, no-command-line-token flow that obtains an operator token interactively, creates the temporary secret, returns the three logical types, and records no token or personal row data as evidence |
| `release/0.4.0/public_contract.json` | Machine-readable function parameters, two relation identifiers and schemas, secret type/provider/temporary-only boundary, diagnostics, compatibility cell, and explicit exclusions for inventory and direct-load consumers |

The private product oracle uses synthetic token A and token B values. Its
controlled service may observe the header only as a black-box request fact; the
test must not print or persist it. Token-specific synthetic response identities
prove that an existing prepared statement uses A, then the replaced B, and
fails without I/O after drop. `DESCRIBE`, `EXPLAIN`, `PREPARE`, missing/wrong
secret, and rejected persistent creation produce zero controlled-service
requests. A redirect target must observe no second request.

The same permanent registration, bind/copy, plan, executor-open, stream,
translation, and `DataChunk` path must serve public and controlled artifacts.
Private composition may select loopback only at construction; the installed
artifact must contain no loopback authority, synthetic token, secret test seam,
environment key, URL/header override, or private extension symbol.

The public GitHub execution is operator-supplied, opt-in compatibility evidence
only. Repository and CI gates do not require a real credential, and recorded
evidence asserts schema and row count without storing the user's identity.

## Provider dependencies and dependency direction

| Provider | Query consumes | Query must not do | Dependency-ready evidence |
| --- | --- | --- | --- |
| Connector Experience | Immutable connector catalog, relation lookup, schema, logical credential requirement, and secret binding identity | Copy `/user`, host, header, bearer, extractor, or policy constants; construct Connector internals; activate YAML | Both relation snapshots and credential-absence oracles pass through the public `CompiledConnector` API |
| Relational Semantics | Side-effect-free `BuildConservativeScanPlan` service accepting Query's `ScanRequest` and returning the complete immutable plan | Resolve a secret; construct or mutate a plan; infer cardinality, authentication, host/placement, relational ownership, or explanation meaning | Both relation plans, missing/extra secret counterexamples, offline properties, and token-sentinel absence pass independently |
| Remote Runtime | Opaque move-only authorized-secret capability contract, executor/stream/batch/control APIs, structured authentication and authorization failures, production executor factory, and controlled test executor factory | Read a token into a generic string API; build `Authorization`; inspect host/placement; import runtime `internal/` headers; reproduce redirect, curl, transport, decode, or cleanup algorithms | DuckDB-free bearer/host/redirect/redaction/lifecycle tests pass and the public capability can be consumed without Runtime-internal knowledge |

Query's `ScanRequest` change is its service contribution to Semantics. Query
publishes that contract without importing planner logic. Provider branches land
or are made available to this worktree before Query integrates against them;
Query does not create temporary compatibility fields to guess their eventual
shape.

Build-target and source-identity integration is a downstream lead-owned handoff.
Query identifies new permanent and test source entry points, but does not edit
another workstream's CMake, Make, pin, or verification files to make its branch
appear independently complete.

## Parallel tracks and dependency sequence

| Query track | Can proceed independently | Provider dependency before completion | Integration result |
| --- | --- | --- | --- |
| DuckDB secret registration and exact-name resolver | Yes, against pinned DuckDB source and focused Query tests | Runtime's final opaque capability constructor/consumer boundary | One cohesive host adapter with no request-decoration knowledge |
| SQL argument, immutable bind/copy, and Query diagnostics | Yes for binder rules and existing anonymous behavior | Connector relation lookup and Semantics request/plan contract | Offline relation-aware bind data retaining only the secret name |
| Adapter lifecycle and provider-fake tests | Yes through Query-owned fakes | Runtime executor-open signature and error categories | One-resolution-per-execution, cancellation, close, and safe error proof |
| Private/public product composition and black-box oracles | No; this is the integration track | Accepted Connector, Semantics, and Runtime implementations plus lead-owned build targets | Complete controlled narrative and opt-in public compatibility proof through unchanged Query registration |

Tracks use disjoint Query-owned modules and responsibility-specific tests. If a
track needs routine edits in Connector, Semantics, Runtime, or build-owned
files, the interaction remains open and the dependency returns to that owner;
file overlap is treated as boundary evidence, not solved by joint editing.

## Code documentation obligations

- Document the secret type/provider registration boundary beside its
  declaration: Query ownership, temporary-only behavior, redacted option,
  DuckDB 1.5.4 coupling, registration order, collision/failure behavior, and
  absence of environment or persistence support.
- Document the exact-name resolver's inputs, output capability, one-use
  authority, `ClientContext`/catalog transaction lifetime, replacement/drop
  semantics, error ownership, and the fact that it neither selects host/header
  policy nor guarantees zeroization.
- Document bind/copy state beside the adapter declaration: retained secret name
  only, no lookup or network, immutable prepared meaning, current-value
  execution semantics, and which DuckDB callback owns each state transition.
- Document global scan initialization, stream ownership, cancellation, close,
  exception containment, and credential-capability release where the adapter
  applies them.
- Document product/private composition entry points as provider assembly only,
  including why controlled authority is non-installable and absent from the
  public artifact.
- Keep Connector metadata rationale, relational proof, bearer decoration,
  network policy, curl, and decoder lifecycle documentation behind their
  provider APIs. Do not duplicate it in Query comments.

## Query acceptance evidence

- The exact public SQL returns one `BIGINT`, `VARCHAR`, `BOOLEAN` row after a
  temporary named secret is created; the anonymous SQL remains unchanged.
- `DESCRIBE`, `EXPLAIN`, and `PREPARE` succeed without secret lookup or request.
  Missing/empty arguments fail at bind; missing, wrong, dropped, or malformed
  stored secrets fail during execution initialization before executor I/O.
- A prepared statement executes with synthetic token A, executes with token B
  after `CREATE OR REPLACE TEMPORARY SECRET`, and fails before I/O after
  `DROP SECRET`. An already initialized scan remains bound to its original
  capability, and concurrent scans do not share mutable credentials.
- Controlled success observes the exact one-request behavior and one strict
  row. Unauthorized, forbidden, redirect, decode, schema, resource,
  interruption, early close, connection/result teardown, and recovery paths
  produce bounded redacted outcomes without token leakage or anonymous
  fallback.
- Token sentinels are absent from `CompiledConnector`, `ScanRequest`,
  `ScanPlan`, snapshots, bind and execution errors, captured logs, rows,
  evidence files, public symbols/strings, and retained post-close Query state.
- Query focused tests, the complete controlled product oracle, public contract,
  source demo, installed-artifact inventory, cached product gates, and fresh
  product gate pass after provider and build integration.
- Independent Query/FFI and false-positive-oracle review plus the required
  credential/security adversarial review report no unresolved findings.

## Interaction exits

- **Connector Experience — Satisfied; X-as-a-Service.** Query resolves both
  relations and binds their schemas through the public immutable catalog,
  contains no copied request/auth policy or Connector implementation include,
  and controlled/public contract oracles agree with Connector's independent
  metadata evidence. Re-enter Collaboration if these conditions cease to hold.
- **Relational Semantics — Satisfied; X-as-a-Service.** Query constructs only
  its protocol-neutral request and consumes the immutable plan through public
  services, retains the returned immutable `ScanPlan`, and neither resolves
  credentials nor
  reclassifies cardinality, authentication, host/placement, or relational
  ownership. Provider and Query oracles agree for both relations. Re-enter
  Collaboration if these conditions cease to hold.
- **Remote Runtime — Satisfied; X-as-a-Service.** Query uses the public
  authorization/executor/stream surface without Runtime internal imports or
  header, host, curl, transport, or decoder construction. Query hands one
  opaque scan-scoped capability to the documented executor and consumes only
  public stream/batch/control/error APIs. Focused Runtime and Query lifecycle
  oracles pass independently. Re-enter Collaboration if these conditions cease
  to hold.
- **Query Experience outcome — Acceptance pending.** The permanent public
  artifact exposes the accepted secret and SQL surfaces, the complete
  controlled and opt-in public narratives pass, contract propagation and
  independent review are complete, and cached/fresh gates support every
  acceptance signal without provider-boundary violations. Only the live
  credential-pedigree decision remains open.

The final audit inspected includes, construction points, state ownership,
tests, adjacent code documentation, and artifact inventory on committed tree
`861b14b`. Provider interactions are satisfied. The Query outcome remains open
only for product management's decision on the live credential-pedigree evidence
recorded in the parent goal.
